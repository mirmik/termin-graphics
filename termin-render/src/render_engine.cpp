#include <termin/render/render_engine.hpp>

#include <cstdlib>
#include <functional>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <tcbase/tc_log.hpp>
#include "tc_profiler.h"
#include "tc_project_settings.h"

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/render_runtime.hpp"
#include "tgfx/tgfx2_interop.h"
#include "termin/render/tgfx2_bridge.hpp"

extern "C" {
#include "render/tc_frame_graph.h"
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
#include "core/tc_scene.h"
#include "core/tc_scene_render_state.h"
#include "core/tc_scene_render_mount.h"
#include "core/tc_component.h"
}

namespace termin {

static constexpr const char* RESOURCE_FORMAT_RENDER_TARGET = "render_target";

using RenderTimingClock = std::chrono::steady_clock;

struct RenderPassTimingStats {
    uint64_t count = 0;
    double total_ms = 0.0;
};

struct RenderEngineTimingStats {
    RenderTimingClock::time_point window_start = RenderTimingClock::now();
    uint64_t calls = 0;
    double total_ms = 0.0;
    double frame_graph_ms = 0.0;
    double specs_ms = 0.0;
    double allocate_ms = 0.0;
    double begin_frame_ms = 0.0;
    double clear_targets_ms = 0.0;
    double assemble_resources_ms = 0.0;
    double clear_resources_ms = 0.0;
    double pass_total_ms = 0.0;
    double end_frame_ms = 0.0;
    std::unordered_map<std::string, RenderPassTimingStats> pass_stats;
};

static bool render_engine_timing_enabled() {
#ifdef __ANDROID__
    return true;
#else
    const char* env = std::getenv("TERMIN_RENDER_ENGINE_TIMING");
    return env && env[0] && env[0] != '0';
#endif
}

static double timing_ms(RenderTimingClock::time_point begin, RenderTimingClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

static RenderEngineTimingStats& render_engine_timing_stats() {
    static RenderEngineTimingStats stats;
    return stats;
}

static void maybe_report_render_engine_timing() {
    if (!render_engine_timing_enabled()) {
        return;
    }

    RenderEngineTimingStats& stats = render_engine_timing_stats();
    const auto now = RenderTimingClock::now();
    const double window_seconds = std::chrono::duration<double>(now - stats.window_start).count();
    if (window_seconds < 2.0 || stats.calls == 0) {
        return;
    }

    const double inv_calls = 1.0 / static_cast<double>(stats.calls);
    tc::Log::info(
        "[RenderEngine timing] calls=%llu callsPerSec=%.1f avgMs{total=%.2f frameGraph=%.2f specs=%.2f allocate=%.2f beginFrame=%.2f clearTargets=%.2f assemble=%.2f clearResources=%.2f passes=%.2f endFrame=%.2f}",
        static_cast<unsigned long long>(stats.calls),
        static_cast<double>(stats.calls) / window_seconds,
        stats.total_ms * inv_calls,
        stats.frame_graph_ms * inv_calls,
        stats.specs_ms * inv_calls,
        stats.allocate_ms * inv_calls,
        stats.begin_frame_ms * inv_calls,
        stats.clear_targets_ms * inv_calls,
        stats.assemble_resources_ms * inv_calls,
        stats.clear_resources_ms * inv_calls,
        stats.pass_total_ms * inv_calls,
        stats.end_frame_ms * inv_calls
    );

    std::vector<std::pair<std::string, RenderPassTimingStats>> passes;
    passes.reserve(stats.pass_stats.size());
    for (const auto& entry : stats.pass_stats) {
        passes.push_back(entry);
    }
    std::sort(
        passes.begin(),
        passes.end(),
        [](const auto& a, const auto& b) {
            return a.second.total_ms > b.second.total_ms;
        }
    );

    const size_t max_passes = std::min<size_t>(passes.size(), 10);
    for (size_t i = 0; i < max_passes; ++i) {
        const auto& [name, pass] = passes[i];
        const double avg_ms = pass.count > 0
            ? pass.total_ms / static_cast<double>(pass.count)
            : 0.0;
        tc::Log::info(
            "[RenderEngine timing] pass[%zu] name='%s' calls=%llu avgMs=%.2f totalMs=%.2f",
            i,
            name.c_str(),
            static_cast<unsigned long long>(pass.count),
            avg_ms,
            pass.total_ms
        );
    }

    stats = RenderEngineTimingStats{};
    stats.window_start = now;
}

static bool is_external_color_output(const char* name) {
    return name && (
        strcmp(name, "OUTPUT") == 0 ||
        strcmp(name, "DISPLAY") == 0 ||
        strcmp(name, "RT_COLOR") == 0
    );
}

static bool is_external_depth_output(const char* name) {
    return name && strcmp(name, "RT_DEPTH") == 0;
}

static bool is_external_output_resource(const char* name) {
    return is_external_color_output(name) || is_external_depth_output(name);
}

static bool is_external_graph_input_resource(
    const char* name,
    const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts
) {
    if (!name || name[0] == '\0') {
        return false;
    }
    for (const auto& [render_target_name, ctx] : render_target_contexts) {
        (void)render_target_name;
        auto it = ctx.external_textures.find(name);
        if (it != ctx.external_textures.end() && it->second) {
            return true;
        }
    }
    return false;
}

static tgfx::PixelFormat resolve_fbo_color_format(
    const std::string& format,
    const RenderTargetContext& default_rt_ctx,
    const tgfx::IRenderDevice& device
) {
    if (format == RESOURCE_FORMAT_RENDER_TARGET) {
        if (default_rt_ctx.output_color_format != tgfx::PixelFormat::Undefined) {
            return default_rt_ctx.output_color_format;
        }
        if (!default_rt_ctx.output_color_tex) {
            tc::Log::warn(
                "RenderEngine::render_scene_pipeline_offscreen: FBO format '%s' requested but output_color_tex is invalid; using rgba8",
                RESOURCE_FORMAT_RENDER_TARGET
            );
            return tgfx::PixelFormat::RGBA8_UNorm;
        }
        tgfx::TextureDesc output_desc = device.texture_desc(default_rt_ctx.output_color_tex);
        if (output_desc.format == tgfx::PixelFormat::Undefined) {
            tc::Log::warn(
                "RenderEngine::render_scene_pipeline_offscreen: output_color_tex has undefined format; using rgba8"
            );
            return tgfx::PixelFormat::RGBA8_UNorm;
        }
        return output_desc.format;
    }
    const tgfx::PixelFormat parsed = tgfx::pixel_format_from_name(
        format, tgfx::PixelFormat::Undefined);
    if (parsed != tgfx::PixelFormat::Undefined) {
        return parsed;
    }

    tc::Log::warn(
        "RenderEngine::render_scene_pipeline_offscreen: unknown FBO color format '%s'; using rgba8",
        format.c_str()
    );
    return tgfx::PixelFormat::RGBA8_UNorm;
}

// Ensure the scene extensions that passes depend on are registered before
// the first pipeline runs. Idempotent: each extension init is a no-op if the
// type is already present. This lets standalone Python scripts that create
// a RenderEngine directly (without going through EngineCore) still have
// skybox / render_mount / etc. accessible.
static void ensure_scene_extensions_for_render() {
    tc_scene_render_mount_extension_init();
    tc_scene_render_state_extension_init();
}

RenderEngine::RenderEngine() {
    ensure_scene_extensions_for_render();
}

// Out-of-line destructor so unique_ptr<tgfx::*> members can use forward
// declarations in the header; the full tgfx2 types are visible here.
RenderEngine::~RenderEngine() = default;

tgfx::RenderContext2* RenderEngine::tgfx2_ctx() {
    return tgfx2_runtime_ ? &tgfx2_runtime_->context() : nullptr;
}

tgfx::IRenderDevice* RenderEngine::tgfx2_device() {
    return tgfx2_runtime_ ? &tgfx2_runtime_->device() : nullptr;
}

RenderPipelineCacheStats RenderEngine::pipeline_cache_stats() const {
    if (!tgfx2_runtime_) {
        return {};
    }

    const tgfx::PipelineCacheStats cache_stats = tgfx2_runtime_->cache_stats();
    RenderPipelineCacheStats out;
    out.hit_count = cache_stats.hit_count;
    out.miss_count = cache_stats.miss_count;
    out.create_pipeline_count = cache_stats.create_pipeline_count;
    out.unique_vertex_layout_signature_count =
        cache_stats.unique_vertex_layout_signature_count;
    out.cached_pipeline_count = cache_stats.cached_pipeline_count;
    out.vertex_layout_signature_hashes = cache_stats.vertex_layout_signature_hashes;
    return out;
}

void RenderEngine::ensure_tgfx2() {
    // TODO: tgfx context initialization should happen at the application
    // top level, not inside RenderEngine.
    // Diagnostic escape hatch: setting TERMIN_DISABLE_TGFX2=1 keeps the tgfx2
    // stack un-initialised, so ctx.ctx2 stays nullptr. Used to isolate whether
    // a rendering regression is caused by the tgfx2 path or not.
    static const bool disable_tgfx2 = []() {
        const char* env = std::getenv("TERMIN_DISABLE_TGFX2");
        return env && env[0] && env[0] != '0';
    }();
    if (disable_tgfx2) {
        return;
    }

    if (tgfx2_runtime_) {
        return;
    }

    // Prefer the host-owned device if it's already registered as the
    // process-wide interop target (Tgfx2Context.from_window did this
    // at editor startup). Creating a second device here would mint a
    // new HandlePool — scene textures end up on one pool, the UI /
    // FBOSurface on another, and blit_to_texture silently drops
    // cross-pool calls. Symptom was an entirely grey viewport with
    // no Vulkan/GL errors.
    if (auto* host_dev = static_cast<tgfx::IRenderDevice*>(tgfx2_interop_get_device())) {
        tgfx2_runtime_ = std::make_unique<tgfx::RenderRuntime>(*host_dev);
    } else {
        // No host — standalone render test / headless case. Create our
        // own device and register it as the interop target so any
        // later Python helper that checks interop lands on it.
        // Backend selected by TERMIN_BACKEND env-var (default Vulkan when compiled).
        tgfx2_runtime_ = tgfx::RenderRuntime::create_from_env();
    }
}

void RenderEngine::render_scene_pipeline_offscreen(
    RenderPipeline& pipeline,
    tc_scene_handle scene,
    const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts,
    const std::vector<Light>& lights,
    const std::string& default_render_target
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: pipeline is null");
        return;
    }
    ensure_tgfx2();
    tgfx::IRenderDevice* device = tgfx2_device();
    tgfx::RenderContext2* ctx2 = tgfx2_ctx();
    if (!device) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: tgfx2 device unavailable");
        return;
    }
    if (render_target_contexts.empty()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: no render target contexts");
        return;
    }

    const bool collect_render_timing = render_engine_timing_enabled();
    const auto total_begin = RenderTimingClock::now();
    double frame_graph_ms = 0.0;
    double specs_ms = 0.0;
    double allocate_ms = 0.0;
    double begin_frame_ms = 0.0;
    double clear_targets_ms = 0.0;
    double assemble_resources_ms = 0.0;
    double clear_resources_ms = 0.0;
    double pass_total_ms = 0.0;
    double end_frame_ms = 0.0;
    std::unordered_map<std::string, RenderPassTimingStats> local_pass_stats;

    std::string default_target = default_render_target;
    if (default_target.empty()) {
        default_target = render_target_contexts.begin()->first;
    }

    auto default_it = render_target_contexts.find(default_target);
    if (default_it == render_target_contexts.end()) {
        default_it = render_target_contexts.begin();
    }
    const RenderTargetContext& default_rt_ctx = default_it->second;

    int default_width = default_rt_ctx.render_rect.width;
    int default_height = default_rt_ctx.render_rect.height;

    tc_profiler_begin_section("Get Frame Graph");
    const auto frame_graph_begin = RenderTimingClock::now();
    tc_frame_graph* fg = tc_pipeline_get_frame_graph(pipeline.handle());
    if (!fg) {
        tc_profiler_end_section();
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: failed to get frame graph");
        return;
    }

    if (tc_frame_graph_get_error(fg) != TC_FG_OK) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: frame graph error: %s",
                       tc_frame_graph_get_error_message(fg));
        tc_profiler_end_section();
        return;
    }
    tc_profiler_end_section();
    frame_graph_ms = timing_ms(frame_graph_begin, RenderTimingClock::now());

    // Bring up tgfx2 stack BEFORE FBO allocation so FBOPool::ensure can
    // attach persistent tgfx2 wrappers on the very first frame.
    ensure_tgfx2();

    tc_profiler_begin_section("Collect Specs");
    const auto specs_begin = RenderTimingClock::now();
    auto specs = pipeline.collect_specs();

    std::unordered_map<std::string, ResourceSpec> spec_map;
    for (const auto& spec : specs) {
        auto it = spec_map.find(spec.resource);
        if (it == spec_map.end()) {
            spec_map[spec.resource] = spec;
        } else {
            ResourceSpec& existing = it->second;
            if (spec.samples > 1 && existing.samples == 1) {
                existing.samples = spec.samples;
            }
            if (spec.format && !existing.format) {
                existing.format = spec.format;
            }
            if (spec.clear_color && !existing.clear_color) {
                existing.clear_color = spec.clear_color;
            }
            if (spec.clear_depth && !existing.clear_depth) {
                existing.clear_depth = spec.clear_depth;
            }
        }
    }
    tc_profiler_end_section();
    specs_ms = timing_ms(specs_begin, RenderTimingClock::now());

    tc_profiler_begin_section("Allocate Resources");
    const auto allocate_begin = RenderTimingClock::now();
    FBOMap resources;
    PipelineRenderCache& pipeline_cache = pipeline.cache();
    pipeline_cache.texture_alias_to_canonical.clear();
    // OUTPUT/DISPLAY no longer travel through the FBOMap — they're
    // native tgfx2 textures owned by the caller (ViewportRenderState)
    // and plumbed straight into tex2_writes below.

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (pipeline_cache.fbo_compositions.find(canon) != pipeline_cache.fbo_compositions.end()) {
            continue;
        }

        if (is_external_output_resource(canon) ||
            is_external_graph_input_resource(canon, render_target_contexts)) {
            // OUTPUT/DISPLAY/RT_* are viewport-owned native textures, and
            // External RT resources are supplied through RenderTargetContext.
            // Never allocate an internal FBO/texture for these names: doing
            // so makes debuggers and fallback resolvers see a viewport-sized
            // dummy resource instead of the actual external texture.
            continue;
        }

        const ResourceSpec* spec = nullptr;
        auto it = spec_map.find(canon);
        if (it != spec_map.end()) {
            spec = &it->second;
        } else {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count && !spec; j++) {
                auto ait = spec_map.find(aliases[j]);
                if (ait != spec_map.end()) {
                    spec = &ait->second;
                }
            }
        }

        std::string resource_type = "fbo";
        if (spec && !spec->resource_type.empty()) {
            resource_type = spec->resource_type;
        }

        if (resource_type == "shadow_map_array") {
            auto& shadow_array = pipeline.shadow_arrays()[canon];
            if (!shadow_array) {
                int resolution = 1024;
                if (spec && spec->size) {
                    resolution = spec->size->first;
                }
                shadow_array = std::make_unique<ShadowMapArrayResource>(resolution);
            }

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = shadow_array.get();
            }
            continue;
        }

        if (resource_type == "color_texture") {
            int tex_width = default_width;
            int tex_height = default_height;
            std::string format;
            if (spec) {
                if (spec->size) {
                    tex_width = spec->size->first;
                    tex_height = spec->size->second;
                }
                if (spec->format) {
                    format = *spec->format;
                }
            }

            tgfx::TextureUsage usage =
                tgfx::TextureUsage::Sampled |
                tgfx::TextureUsage::ColorAttachment |
                tgfx::TextureUsage::CopySrc |
                tgfx::TextureUsage::CopyDst;
            tgfx::PixelFormat color_format =
                resolve_fbo_color_format(format, default_rt_ctx, *device);
            tgfx::TextureDesc texture_desc;
            texture_desc.width = static_cast<uint32_t>(tex_width);
            texture_desc.height = static_cast<uint32_t>(tex_height);
            texture_desc.format = color_format;
            texture_desc.usage = usage;
            if (!pipeline_cache.texture_pool.ensure(
                    *device, canon, texture_desc)) {
                tc::Log::error(
                    "RenderEngine::render_scene_pipeline_offscreen: failed to allocate color_texture '%s'",
                    canon);
            }

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
                if (std::string(aliases[j]) != canon) {
                    pipeline_cache.texture_alias_to_canonical[aliases[j]] = canon;
                }
            }
            continue;
        }

        if (resource_type == "depth_texture") {
            int tex_width = default_width;
            int tex_height = default_height;
            if (spec && spec->size) {
                tex_width = spec->size->first;
                tex_height = spec->size->second;
            }
            tgfx::TextureUsage usage =
                tgfx::TextureUsage::Sampled |
                tgfx::TextureUsage::DepthStencilAttachment |
                tgfx::TextureUsage::CopySrc |
                tgfx::TextureUsage::CopyDst;
            tgfx::TextureDesc texture_desc;
            texture_desc.width = static_cast<uint32_t>(tex_width);
            texture_desc.height = static_cast<uint32_t>(tex_height);
            texture_desc.format = tgfx::PixelFormat::D32F;
            texture_desc.usage = usage;
            if (!pipeline_cache.texture_pool.ensure(
                    *device, canon, texture_desc)) {
                tc::Log::error(
                    "RenderEngine::render_scene_pipeline_offscreen: failed to allocate depth_texture '%s'",
                    canon);
            }

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
                if (std::string(aliases[j]) != canon) {
                    pipeline_cache.texture_alias_to_canonical[aliases[j]] = canon;
                }
            }
            continue;
        }

        if (resource_type != "fbo") {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
                if (std::string(aliases[j]) != canon) {
                    pipeline_cache.texture_alias_to_canonical[aliases[j]] = canon;
                }
            }
            continue;
        }

        int fbo_width = default_width;
        int fbo_height = default_height;
        int samples = 1;
        std::string format;
        TextureFilter filter = TextureFilter::LINEAR;

        if (spec) {
            if (spec->size) {
                fbo_width = spec->size->first;
                fbo_height = spec->size->second;
            }
            samples = spec->samples > 0 ? spec->samples : 1;
            if (spec->format) format = *spec->format;
            filter = spec->filter;
        }

        FBOPool& fbo_pool = pipeline.fbo_pool();

        tgfx::PixelFormat color_fmt = resolve_fbo_color_format(format, default_rt_ctx, *device);
        tgfx::RenderTargetPoolDesc target_desc;
        target_desc.width = fbo_width;
        target_desc.height = fbo_height;
        target_desc.samples = samples;
        target_desc.color_format = color_fmt;
        target_desc.has_depth = true;
        target_desc.depth_format = tgfx::PixelFormat::D32F;

        fbo_pool.ensure_native(*device, canon, target_desc);
        (void)filter;

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = nullptr;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }
    tc_profiler_end_section();
    allocate_ms = timing_ms(allocate_begin, RenderTimingClock::now());

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    const bool owns_tgfx2_frame = ctx2 && !ctx2->in_frame();
    const auto begin_frame_begin = RenderTimingClock::now();
    if (owns_tgfx2_frame) {
        ctx2->begin_frame();
    }
    begin_frame_ms = timing_ms(begin_frame_begin, RenderTimingClock::now());

    tc_profiler_begin_section("Clear Render Target Contexts");
    const auto clear_targets_begin = RenderTimingClock::now();
    if (ctx2) {
        for (const auto& [render_target_name, rt_ctx] : render_target_contexts) {
            if (!rt_ctx.clear_color_enabled && !rt_ctx.clear_depth_enabled) {
                continue;
            }
            if (!rt_ctx.output_color_tex && !rt_ctx.output_depth_tex) {
                tc::Log::error(
                    "RenderEngine::render_scene_pipeline_offscreen: render target context '%s' requested clear but output textures are missing",
                    render_target_name.c_str());
                continue;
            }

            const float* clear_color_ptr =
                rt_ctx.clear_color_enabled ? rt_ctx.clear_color : nullptr;
            ctx2->begin_pass(
                rt_ctx.output_color_tex,
                rt_ctx.output_depth_tex,
                clear_color_ptr,
                rt_ctx.clear_depth,
                rt_ctx.clear_depth_enabled);
            ctx2->set_viewport(
                0, 0,
                std::max(1, rt_ctx.render_rect.width),
                std::max(1, rt_ctx.render_rect.height));
            ctx2->end_pass();
        }
    }
    tc_profiler_end_section();
    clear_targets_ms = timing_ms(clear_targets_begin, RenderTimingClock::now());

    // Assemble per-resource tgfx2 texture maps from the pool. Native
    // path: handles are owned by IRenderDevice, persistent across
    // frames without any wrap/destroy churn.
    const auto assemble_resources_begin = RenderTimingClock::now();
    std::unordered_map<std::string, tgfx::TextureHandle> tex2_resources;
    std::unordered_map<std::string, tgfx::TextureHandle> tex2_depth_resources;
    if (ctx2 && device) {
        FBOPool& fbo_pool = pipeline.fbo_pool();
        for (const auto& [name, res] : resources) {
            tgfx::TextureHandle color_handle = fbo_pool.get_color_tgfx2(name);
            if (color_handle) tex2_resources[name] = color_handle;
            tgfx::TextureHandle depth_handle = fbo_pool.get_depth_tgfx2(name);
            if (depth_handle) tex2_depth_resources[name] = depth_handle;
        }

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];
            tgfx::TextureHandle handle = pipeline_cache.texture_pool.get(canon);
            if (!handle) {
                continue;
            }
            const tgfx::TextureDesc desc = device->texture_desc(handle);
            const bool depth_texture = tgfx::is_depth_format(desc.format);

            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                if (depth_texture) {
                    tex2_depth_resources[aliases[j]] = handle;
                    if (tex2_resources.find(aliases[j]) == tex2_resources.end()) {
                        tex2_resources[aliases[j]] = handle;
                    }
                } else {
                    tex2_resources[aliases[j]] = handle;
                }
            }
        }

        for (const auto& [view_name, view] : pipeline_cache.resource_views) {
            if (is_external_output_resource(view.parent.c_str())) {
                continue;
            }
            if (view.attachment == AttachmentKind::Color) {
                auto it = tex2_resources.find(view.parent);
                if (it != tex2_resources.end() && it->second) {
                    tex2_resources[view_name] = it->second;
                } else {
                    tc::Log::error(
                        "RenderEngine: color view '%s' parent '%s' is missing",
                        view_name.c_str(), view.parent.c_str());
                }
            } else {
                auto it = tex2_depth_resources.find(view.parent);
                if (it != tex2_depth_resources.end() && it->second) {
                    tex2_resources[view_name] = it->second;
                    tex2_depth_resources[view_name] = it->second;
                } else {
                    tc::Log::error(
                        "RenderEngine: depth view '%s' parent '%s' is missing",
                        view_name.c_str(), view.parent.c_str());
                }
            }
        }

        for (const auto& [fbo_name, composition] : pipeline_cache.fbo_compositions) {
            auto composition_input_is_external = [&](const std::string& name) {
                if (is_external_output_resource(name.c_str())) {
                    return true;
                }
                auto view_it = pipeline_cache.resource_views.find(name);
                return view_it != pipeline_cache.resource_views.end() &&
                       is_external_output_resource(view_it->second.parent.c_str());
            };
            if (composition_input_is_external(composition.color) ||
                composition_input_is_external(composition.depth)) {
                // Viewport-owned textures are only available once a concrete
                // render target context is selected. Per-pass resolvers below handle
                // those compositions without populating the global maps here.
                continue;
            }

            auto color_it = tex2_resources.find(composition.color);
            if (color_it != tex2_resources.end() && color_it->second) {
                tex2_resources[fbo_name] = color_it->second;
            } else {
                tc::Log::error(
                    "RenderEngine: composed FBO '%s' color input '%s' is missing",
                    fbo_name.c_str(), composition.color.c_str());
            }

            tgfx::TextureHandle depth_handle;
            auto depth_it = tex2_depth_resources.find(composition.depth);
            if (depth_it != tex2_depth_resources.end() && depth_it->second) {
                depth_handle = depth_it->second;
            } else {
                auto depth_as_color_it = tex2_resources.find(composition.depth);
                if (depth_as_color_it != tex2_resources.end() && depth_as_color_it->second) {
                    depth_handle = depth_as_color_it->second;
                }
            }
            if (depth_handle) {
                tex2_depth_resources[fbo_name] = depth_handle;
            } else {
                tc::Log::error(
                    "RenderEngine: composed FBO '%s' depth input '%s' is missing",
                    fbo_name.c_str(), composition.depth.c_str());
            }
        }
    }
    assemble_resources_ms = timing_ms(assemble_resources_begin, RenderTimingClock::now());

    // Pre-frame clear phase via transient ctx2 passes. Replaces legacy
    // graphics->bind_framebuffer + clear_color_depth loop — see
    // render_view_to_fbo for the rationale.
    tc_profiler_begin_section("Clear Resources");
    const auto clear_resources_begin = RenderTimingClock::now();
    if (ctx2) {
        for (const auto& spec : specs) {
            if (spec.resource_type != "fbo" && !spec.resource_type.empty()) {
                continue;
            }
            if (!spec.clear_color && !spec.clear_depth) {
                continue;
            }

            auto ct = tex2_resources.find(spec.resource);
            auto dt = tex2_depth_resources.find(spec.resource);
            tgfx::TextureHandle color_tex =
                (ct != tex2_resources.end()) ? ct->second : tgfx::TextureHandle{};
            tgfx::TextureHandle depth_tex =
                (dt != tex2_depth_resources.end()) ? dt->second : tgfx::TextureHandle{};
            if (!color_tex && !depth_tex) continue;

            float clear_rgba[4] = {0, 0, 0, 1};
            const float* clear_color_ptr = nullptr;
            if (spec.clear_color) {
                const auto& cc = *spec.clear_color;
                clear_rgba[0] = static_cast<float>(cc[0]);
                clear_rgba[1] = static_cast<float>(cc[1]);
                clear_rgba[2] = static_cast<float>(cc[2]);
                clear_rgba[3] = static_cast<float>(cc[3]);
                clear_color_ptr = clear_rgba;
            }
            float clear_depth_val =
                spec.clear_depth ? static_cast<float>(*spec.clear_depth) : 1.0f;
            bool clear_depth_enabled = spec.clear_depth.has_value();

            int fb_w = spec.size ? spec.size->first : default_width;
            int fb_h = spec.size ? spec.size->second : default_height;

            ctx2->begin_pass(
                color_tex, depth_tex,
                clear_color_ptr, clear_depth_val, clear_depth_enabled
            );
            ctx2->set_viewport(0, 0, fb_w, fb_h);
            ctx2->end_pass();
        }
    }
    tc_profiler_end_section();
    clear_resources_ms = timing_ms(clear_resources_begin, RenderTimingClock::now());

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);
        const auto pass_begin = RenderTimingClock::now();

        // Per-pass state reset — no-op on backends with explicit state
        // (Vulkan), restores GL baseline on OpenGL.
        device->reset_state();

        std::string pass_render_target_name = default_target;
        if (pass->viewport_name && pass->viewport_name[0] != '\0') {
            pass_render_target_name = pass->viewport_name;
        }

        auto rt_it = render_target_contexts.find(pass_render_target_name);
        if (rt_it == render_target_contexts.end()) {
            rt_it = default_it;
        }
        const RenderTargetContext& rt_ctx = rt_it->second;

        const char* reads[16];
        const char* writes[8];
        size_t read_count = tc_pass_get_reads(pass, reads, 16);
        size_t write_count = tc_pass_get_writes(pass, writes, 8);

        Tex2Map pass_tex2_reads;
        Tex2Map pass_tex2_writes;
        Tex2Map pass_tex2_depth_reads;
        Tex2Map pass_tex2_depth_writes;
        ShadowArrayMap pass_shadow_arrays;

        std::function<tgfx::TextureHandle(const std::string&)> resolve_depth_resource;
        std::function<tgfx::TextureHandle(const std::string&)> resolve_color_resource;

        resolve_color_resource = [&](const std::string& name) -> tgfx::TextureHandle {
            const char* canonical_c = tc_frame_graph_canonical_resource(fg, name.c_str());
            std::string canonical = canonical_c ? canonical_c : name;
            if (canonical != name) {
                tgfx::TextureHandle canonical_handle = resolve_color_resource(canonical);
                if (canonical_handle) {
                    return canonical_handle;
                }
            }
            if (is_external_color_output(name.c_str())) {
                return rt_ctx.output_color_tex;
            }
            auto ext_it = rt_ctx.external_textures.find(name);
            if (ext_it != rt_ctx.external_textures.end()) {
                return ext_it->second;
            }
            auto view_it = pipeline_cache.resource_views.find(name);
            if (view_it != pipeline_cache.resource_views.end()) {
                const ResourceView& view = view_it->second;
                if (view.attachment == AttachmentKind::Color) {
                    return resolve_color_resource(view.parent);
                }
                return resolve_depth_resource(view.parent);
            }
            auto comp_it = pipeline_cache.fbo_compositions.find(name);
            if (comp_it != pipeline_cache.fbo_compositions.end()) {
                return resolve_color_resource(comp_it->second.color);
            }
            auto it = tex2_resources.find(name);
            return it != tex2_resources.end() ? it->second : tgfx::TextureHandle{};
        };

        resolve_depth_resource = [&](const std::string& name) -> tgfx::TextureHandle {
            const char* canonical_c = tc_frame_graph_canonical_resource(fg, name.c_str());
            std::string canonical = canonical_c ? canonical_c : name;
            if (canonical != name) {
                tgfx::TextureHandle canonical_handle = resolve_depth_resource(canonical);
                if (canonical_handle) {
                    return canonical_handle;
                }
            }
            if (is_external_color_output(name.c_str()) || is_external_depth_output(name.c_str())) {
                return rt_ctx.output_depth_tex;
            }
            auto view_it = pipeline_cache.resource_views.find(name);
            if (view_it != pipeline_cache.resource_views.end()) {
                const ResourceView& view = view_it->second;
                if (view.attachment == AttachmentKind::Depth) {
                    return resolve_depth_resource(view.parent);
                }
                return tgfx::TextureHandle{};
            }
            auto comp_it = pipeline_cache.fbo_compositions.find(name);
            if (comp_it != pipeline_cache.fbo_compositions.end()) {
                tgfx::TextureHandle depth = resolve_depth_resource(comp_it->second.depth);
                return depth ? depth : resolve_color_resource(comp_it->second.depth);
            }
            auto it = tex2_depth_resources.find(name);
            return it != tex2_depth_resources.end() ? it->second : tgfx::TextureHandle{};
        };

        auto collect_shadow_array = [&](const char* name) {
            auto it = resources.find(name);
            if (it != resources.end() && it->second) {
                auto* arr = dynamic_cast<ShadowMapArrayResource*>(it->second);
                if (arr) {
                    pass_shadow_arrays[name] = arr;
                    // Also expose the array's first cascade as a regular
                    // tex2 read, so generic passes (e.g. FrameDebugger)
                    // can sample / blit shadow_maps without knowing about
                    // the ShadowArrayMap side-channel. ColorPass still
                    // iterates shadow_arrays for the full cascade set.
                    if (!arr->entries.empty() &&
                        arr->entries[0].depth_tex2) {
                        pass_tex2_reads[name] = arr->entries[0].depth_tex2;
                    }
                }
            }
        };

        for (size_t j = 0; j < read_count; j++) {
            const char* read_name = reads[j];
            if (is_external_color_output(read_name)) {
                if (rt_ctx.output_color_tex) {
                    pass_tex2_reads[read_name] = rt_ctx.output_color_tex;
                }
                if (rt_ctx.output_depth_tex) {
                    pass_tex2_depth_reads[read_name] = rt_ctx.output_depth_tex;
                }
                continue;
            }
            if (is_external_depth_output(read_name)) {
                if (rt_ctx.output_depth_tex) {
                    pass_tex2_depth_reads[read_name] = rt_ctx.output_depth_tex;
                }
                continue;
            }
            collect_shadow_array(read_name);
            auto ext_it = rt_ctx.external_textures.find(read_name);
            if (ext_it != rt_ctx.external_textures.end() && ext_it->second) {
                pass_tex2_reads[read_name] = ext_it->second;
                continue;
            }
            tgfx::TextureHandle color_handle = resolve_color_resource(read_name);
            if (color_handle) {
                pass_tex2_reads[read_name] = color_handle;
            }
            tgfx::TextureHandle depth_handle = resolve_depth_resource(read_name);
            if (depth_handle) {
                pass_tex2_depth_reads[read_name] = depth_handle;
            }
        }

        for (size_t j = 0; j < write_count; j++) {
            const char* write_name = writes[j];
            if (is_external_color_output(write_name)) {
                // OUTPUT/DISPLAY come straight from RenderTargetContext's
                // owned native textures — no FBO wrap needed anymore.
                if (rt_ctx.output_color_tex) {
                    pass_tex2_writes[write_name] = rt_ctx.output_color_tex;
                }
                if (rt_ctx.output_depth_tex) {
                    pass_tex2_depth_writes[write_name] = rt_ctx.output_depth_tex;
                }
            } else if (is_external_depth_output(write_name)) {
                if (rt_ctx.output_depth_tex) {
                    pass_tex2_depth_writes[write_name] = rt_ctx.output_depth_tex;
                }
            } else {
                collect_shadow_array(write_name);
                tgfx::TextureHandle color_handle = resolve_color_resource(write_name);
                if (color_handle) {
                    pass_tex2_writes[write_name] = color_handle;
                }
                tgfx::TextureHandle depth_handle = resolve_depth_resource(write_name);
                if (depth_handle) {
                    pass_tex2_depth_writes[write_name] = depth_handle;
                }
            }
        }

        ExecuteContext ctx;
        ctx.ctx2 = ctx2;
        ctx.tex2_reads = std::move(pass_tex2_reads);
        ctx.tex2_writes = std::move(pass_tex2_writes);
        ctx.tex2_depth_reads = std::move(pass_tex2_depth_reads);
        ctx.tex2_depth_writes = std::move(pass_tex2_depth_writes);
        ctx.shadow_arrays = std::move(pass_shadow_arrays);
        ctx.render_rect = rt_ctx.render_rect;
        ctx.scene = TcSceneRef(scene);
        ctx.render_target_name = rt_ctx.name;
        ctx.internal_entities = rt_ctx.internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&rt_ctx.camera);
        ctx.lights = lights;
        ctx.layer_mask = rt_ctx.layer_mask;
        ctx.render_category_mask = rt_ctx.render_category_mask;

        tc_pass_execute(pass, &ctx);

        // Per-pass sync — no-op on explicit-submission backends.
        tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        if (sync_mode == TC_RENDER_SYNC_FLUSH) {
            device->flush();
        } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
            device->finish();
        }

        tc_profiler_end_section();
        const double pass_ms = timing_ms(pass_begin, RenderTimingClock::now());
        pass_total_ms += pass_ms;
        RenderPassTimingStats& pass_stats = local_pass_stats[pass_name];
        pass_stats.count += 1;
        pass_stats.total_ms += pass_ms;
    }
    tc_profiler_end_section();

    const auto end_frame_begin = RenderTimingClock::now();
    if (owns_tgfx2_frame) {
        ctx2->end_frame();
    }
    end_frame_ms = timing_ms(end_frame_begin, RenderTimingClock::now());

    if (collect_render_timing) {
        RenderEngineTimingStats& stats = render_engine_timing_stats();
        stats.calls += 1;
        stats.total_ms += timing_ms(total_begin, RenderTimingClock::now());
        stats.frame_graph_ms += frame_graph_ms;
        stats.specs_ms += specs_ms;
        stats.allocate_ms += allocate_ms;
        stats.begin_frame_ms += begin_frame_ms;
        stats.clear_targets_ms += clear_targets_ms;
        stats.assemble_resources_ms += assemble_resources_ms;
        stats.clear_resources_ms += clear_resources_ms;
        stats.pass_total_ms += pass_total_ms;
        stats.end_frame_ms += end_frame_ms;
        for (const auto& [name, local_stats] : local_pass_stats) {
            RenderPassTimingStats& pass_stats = stats.pass_stats[name];
            pass_stats.count += local_stats.count;
            pass_stats.total_ms += local_stats.total_ms;
        }
        maybe_report_render_engine_timing();
    }
}

} // namespace termin
