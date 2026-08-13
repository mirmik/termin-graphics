#include <termin/render/render_engine.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tc_profiler.h"
#include "tc_project_settings.h"
#include <tcbase/tc_log.hpp>

#include "termin/render/execute_context.hpp"
#include "termin/render/frame_graph_capture.hpp"
#include "termin/render/frame_graph_resource_registry.hpp"
#include "termin/render/tgfx2_bridge.hpp"
#include "tgfx/tgfx2_interop.h"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/graphics_host.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/shader_artifact_resolver.hpp"

extern "C" {
#include "render/tc_frame_graph.h"
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
}

namespace termin {

    static constexpr const char* RESOURCE_FORMAT_RENDER_TARGET = "render_target";

    static bool begin_clear_texture_pass(tgfx::RenderContext2& ctx,
                                         tgfx::IRenderDevice& device,
                                         tgfx::TextureHandle color,
                                         tgfx::TextureHandle depth,
                                         const termin::LinearColor* clear_color,
                                         float clear_depth,
                                         bool clear_depth_enabled) {
        uint32_t array_layers = 1;
        if (color) {
            array_layers = device.texture_desc(color).array_layers;
        } else if (depth) {
            array_layers = device.texture_desc(depth).array_layers;
        }
        if (array_layers <= 1) {
            ctx.begin_pass(color, depth, clear_color, clear_depth, clear_depth_enabled);
            return true;
        }

        tgfx::MultiviewRenderPassDesc pass;
        pass.view_count = array_layers;
        pass.color_final_state = tgfx::MultiviewColorFinalState::ColorAttachment;
        if (color) {
            tgfx::ColorAttachmentDesc attachment;
            attachment.texture = color;
            attachment.load = clear_color ? tgfx::LoadOp::Clear : tgfx::LoadOp::Load;
            if (clear_color) {
                attachment.clear_color = *clear_color;
            }
            pass.colors.push_back(attachment);
        }
        if (depth) {
            pass.depth.texture = depth;
            pass.depth.load = clear_depth_enabled ? tgfx::LoadOp::Clear : tgfx::LoadOp::Load;
            pass.depth.clear_depth = clear_depth;
            pass.has_depth = true;
        }
        return ctx.begin_multiview_pass(pass);
    }

    using PassDependencyCollector = size_t (*)(tc_pass*, const char**, size_t);

    static std::vector<const char*> collect_pass_dependencies(tc_pass* pass, PassDependencyCollector collect) {
        size_t count = collect(pass, nullptr, 0);
        std::vector<const char*> values;
        while (count > 0) {
            values.resize(count);
            size_t actual = collect(pass, values.data(), count);
            if (actual <= count) {
                values.resize(actual);
                return values;
            }
            count = actual;
        }
        return values;
    }

    static std::vector<const char*> collect_canonical_resources(tc_frame_graph* fg) {
        std::vector<const char*> values(tc_frame_graph_get_canonical_resources(fg, nullptr, 0));
        size_t count = tc_frame_graph_get_canonical_resources(fg, values.data(), values.size());
        values.resize(count);
        return values;
    }

    static std::vector<const char*> collect_alias_group(tc_frame_graph* fg, const char* canonical) {
        std::vector<const char*> values(tc_frame_graph_get_alias_group(fg, canonical, nullptr, 0));
        size_t count = tc_frame_graph_get_alias_group(fg, canonical, values.data(), values.size());
        values.resize(count);
        return values;
    }

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
        uint64_t render_item_scene_traversals = 0;
        uint64_t render_item_producers = 0;
        uint64_t render_items = 0;
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
        tc::Log::info("[RenderEngine timing] calls=%llu callsPerSec=%.1f avgMs{total=%.2f frameGraph=%.2f specs=%.2f "
                      "allocate=%.2f beginFrame=%.2f clearTargets=%.2f assemble=%.2f clearResources=%.2f passes=%.2f "
                      "endFrame=%.2f} avgRenderItems{sceneTraversals=%.2f producers=%.2f items=%.2f}",
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
                      stats.end_frame_ms * inv_calls,
                      static_cast<double>(stats.render_item_scene_traversals) * inv_calls,
                      static_cast<double>(stats.render_item_producers) * inv_calls,
                      static_cast<double>(stats.render_items) * inv_calls);

        std::vector<std::pair<std::string, RenderPassTimingStats>> passes;
        passes.reserve(stats.pass_stats.size());
        for (const auto& entry : stats.pass_stats) {
            passes.push_back(entry);
        }
        std::sort(passes.begin(), passes.end(), [](const auto& a, const auto& b) {
            return a.second.total_ms > b.second.total_ms;
        });

        const size_t max_passes = std::min<size_t>(passes.size(), 10);
        for (size_t i = 0; i < max_passes; ++i) {
            const auto& [name, pass] = passes[i];
            const double avg_ms = pass.count > 0 ? pass.total_ms / static_cast<double>(pass.count) : 0.0;
            tc::Log::info("[RenderEngine timing] pass[%zu] name='%s' calls=%llu avgMs=%.2f totalMs=%.2f",
                          i,
                          name.c_str(),
                          static_cast<unsigned long long>(pass.count),
                          avg_ms,
                          pass.total_ms);
        }

        stats = RenderEngineTimingStats{};
        stats.window_start = now;
    }

    static bool is_external_color_output(const char* name) {
        return name && (strcmp(name, "OUTPUT") == 0 || strcmp(name, "DISPLAY") == 0 || strcmp(name, "RT_COLOR") == 0);
    }

    static bool is_external_depth_output(const char* name) {
        return name && strcmp(name, "RT_DEPTH") == 0;
    }

    static bool is_external_output_resource(const char* name) {
        return is_external_color_output(name) || is_external_depth_output(name);
    }

    using ExternalResourcePredicate = bool (*)(const char*);

    static const char*
    find_external_alias(tc_frame_graph* frame_graph, const char* resource_name, ExternalResourcePredicate predicate) {
        if (!frame_graph || !resource_name || !predicate) {
            return nullptr;
        }
        const char* canonical = tc_frame_graph_canonical_resource(frame_graph, resource_name);
        if (predicate(canonical)) {
            return canonical;
        }
        // In-place passes intentionally keep the input resource as the framegraph
        // canonical name. When their output is caller-owned (for example OUTPUT),
        // the whole alias group must still resolve to that external attachment.
        for (const char* alias : collect_alias_group(frame_graph, canonical)) {
            if (predicate(alias)) {
                return alias;
            }
        }
        return nullptr;
    }

    static bool is_external_graph_input_resource(
        const char* name, const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts) {
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

    static tgfx::PixelFormat resolve_fbo_color_format(const std::string& format,
                                                      const RenderTargetContext& default_rt_ctx,
                                                      const tgfx::IRenderDevice& device) {
        if (format == RESOURCE_FORMAT_RENDER_TARGET) {
            if (!default_rt_ctx.output_color.texture) {
                tc::Log::warn("RenderEngine::execute_pipeline: FBO format '%s' requested but output color target is "
                              "invalid; using rgba8",
                              RESOURCE_FORMAT_RENDER_TARGET);
                return tgfx::PixelFormat::RGBA8_UNorm;
            }
            tgfx::TextureDesc output_desc = device.texture_desc(default_rt_ctx.output_color.texture);
            if (output_desc.format == tgfx::PixelFormat::Undefined) {
                tc::Log::warn("RenderEngine::execute_pipeline: output color target has undefined format; using rgba8");
                return tgfx::PixelFormat::RGBA8_UNorm;
            }
            return output_desc.format;
        }
        const tgfx::PixelFormat parsed = tgfx::pixel_format_from_name(format, tgfx::PixelFormat::Undefined);
        if (parsed != tgfx::PixelFormat::Undefined) {
            return parsed;
        }

        tc::Log::warn("RenderEngine::execute_pipeline: unknown FBO color format '%s'; using rgba8", format.c_str());
        return tgfx::PixelFormat::RGBA8_UNorm;
    }

    RenderEngine::RenderEngine() = default;

    // Out-of-line destructor so unique_ptr<tgfx::*> members can use forward
    // declarations in the header; the full tgfx2 types are visible here.
    RenderEngine::~RenderEngine() = default;

    tgfx::RenderContext2* RenderEngine::tgfx2_ctx() {
        return graphics_host_ ? &graphics_host_->context() : nullptr;
    }

    tgfx::IRenderDevice* RenderEngine::tgfx2_device() {
        return graphics_host_ ? &graphics_host_->device() : nullptr;
    }

    void RenderEngine::set_graphics_host(tgfx::GraphicsHost& graphics_host) {
        if (graphics_host.is_closed()) {
            throw std::invalid_argument("RenderEngine::set_graphics_host: host is closed");
        }
        if (graphics_host_ && graphics_host_ != &graphics_host) {
            throw std::logic_error("RenderEngine::set_graphics_host: replacing the graphics domain is forbidden");
        }
        graphics_host_ = &graphics_host;
        if (shader_artifact_resolver_) {
            graphics_host_->configure_shader_artifacts(*shader_artifact_resolver_);
        }
    }

    void RenderEngine::configure_shader_artifacts(const std::string& artifact_root,
                                                  const std::string& cache_root,
                                                  const std::string& compiler_path,
                                                  bool dev_compile_enabled,
                                                  ShaderArtifactResolver::ReadCallback read_callback) {
        shader_artifact_resolver_ = std::make_unique<ShaderArtifactResolver>(
            artifact_root, cache_root, compiler_path, dev_compile_enabled, false, std::move(read_callback));
        if (graphics_host_) {
            graphics_host_->configure_shader_artifacts(*shader_artifact_resolver_);
        }
    }

    RenderPipelineCacheStats RenderEngine::pipeline_cache_stats() const {
        if (!graphics_host_) {
            return {};
        }

        const tgfx::PipelineCacheStats cache_stats = graphics_host_->cache_stats();
        RenderPipelineCacheStats out;
        out.hit_count = cache_stats.hit_count;
        out.miss_count = cache_stats.miss_count;
        out.create_pipeline_count = cache_stats.create_pipeline_count;
        out.unique_vertex_layout_signature_count = cache_stats.unique_vertex_layout_signature_count;
        out.cached_pipeline_count = cache_stats.cached_pipeline_count;
        out.vertex_layout_signature_hashes = cache_stats.vertex_layout_signature_hashes;
        return out;
    }

    void RenderEngine::ensure_tgfx2() {
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

        if (graphics_host_) {
            return;
        }

        // Never rebuild a second cache/context around a globally installed raw
        // device. Windowed composition roots must inject the exact GraphicsHost.
        if (tgfx2_interop_get_device() != nullptr) {
            tc::Log::error("RenderEngine::ensure_tgfx2: graphics domain installed without "
                           "set_graphics_host()");
            throw std::logic_error("RenderEngine requires set_graphics_host() in a windowed application");
        }
        owned_graphics_host_ = tgfx::GraphicsHost::create_application_from_env();
        graphics_host_ = owned_graphics_host_.get();
        if (shader_artifact_resolver_) {
            graphics_host_->configure_shader_artifacts(*shader_artifact_resolver_);
        }
    }

    void RenderEngine::execute_pipeline(const RenderExecution& execution) {
        if (!execution.pipeline || !execution.pipeline->is_valid()) {
            tc::Log::error("RenderEngine::execute_pipeline: pipeline is null");
            return;
        }
        RenderPipeline& pipeline = *execution.pipeline;
        const std::vector<FrameGraphCaptureRequest*>& debug_capture_requests = execution.debug_capture_requests;

        std::unordered_map<std::string, RenderTargetContext> render_target_contexts;
        render_target_contexts.reserve(execution.targets.size());
        for (const auto& [name, target] : execution.targets) {
            if (!target.context) {
                tc::Log::error("RenderEngine::execute_pipeline: target '%s' has no RenderTargetContext", name.c_str());
                return;
            }
            if (!target.render_items || !target.render_items->valid()) {
                tc::Log::error("RenderEngine::execute_pipeline: target '%s' has no published RenderItemSnapshot",
                               name.c_str());
                return;
            }
            render_target_contexts.emplace(name, *target.context);
        }
        ensure_tgfx2();
        tgfx::IRenderDevice* device = tgfx2_device();
        tgfx::RenderContext2* ctx2 = tgfx2_ctx();
        if (!device) {
            tc::Log::error("RenderEngine::execute_pipeline: tgfx2 device unavailable");
            return;
        }
        if (render_target_contexts.empty()) {
            tc::Log::error("RenderEngine::execute_pipeline: no render target contexts");
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

        std::string default_target = execution.default_render_target;
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
            tc::Log::error("RenderEngine::execute_pipeline: failed to get frame graph");
            return;
        }

        if (tc_frame_graph_get_error(fg) != TC_FG_OK) {
            tc::Log::error("RenderEngine::execute_pipeline: frame graph error: %s",
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
                if (spec.array_layers > existing.array_layers) {
                    existing.array_layers = spec.array_layers;
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
                if (spec.has_color && !existing.has_color) {
                    existing.has_color = spec.has_color;
                }
                if (spec.has_depth && !existing.has_depth) {
                    existing.has_depth = spec.has_depth;
                }
            }
        }

        PipelineRenderCache& pipeline_cache = pipeline.cache();
        auto find_resource_spec = [&](const std::string& resource) -> const ResourceSpec* {
            auto direct = spec_map.find(resource);
            if (direct != spec_map.end()) {
                return &direct->second;
            }
            for (const char* alias : collect_alias_group(fg, resource.c_str())) {
                auto aliased = spec_map.find(alias);
                if (aliased != spec_map.end()) {
                    return &aliased->second;
                }
            }
            return nullptr;
        };

        // Resolve the allocation that physically owns a color result. FBO
        // compositions and attachment views are names for an underlying color
        // resource; direct export binding must replace that resource's color
        // attachment while preserving any separately allocated depth.
        auto resolve_color_storage = [&](const std::string& resource) {
            std::string current = resource;
            for (size_t depth = 0; depth < 8; ++depth) {
                const char* canonical = tc_frame_graph_canonical_resource(fg, current.c_str());
                if (canonical) {
                    current = canonical;
                }

                auto view = pipeline_cache.resource_views.find(current);
                if (view != pipeline_cache.resource_views.end()) {
                    if (view->second.attachment != AttachmentKind::Color) {
                        return std::string{};
                    }
                    current = view->second.parent;
                    continue;
                }

                auto composition = pipeline_cache.fbo_compositions.find(current);
                if (composition != pipeline_cache.fbo_compositions.end()) {
                    current = composition->second.color;
                    continue;
                }
                return current;
            }
            tc::Log::error("RenderEngine: color resource '%s' has a cyclic view/composition chain", resource.c_str());
            return std::string{};
        };

        struct DirectColorCandidate {
            std::string storage_resource;
            tgfx::TextureHandle target;
            ColorOutputBindingPlan plan;
        };
        std::unordered_map<std::string, size_t> valid_color_consumer_counts;
        auto plan_direct_color_candidate = [&](const PipelineColorExport& color_export,
                                               DirectColorCandidate& candidate) {
            auto target_it = color_export.viewport_name.empty() ? default_it
                                                                : render_target_contexts.find(color_export.viewport_name);
            if (target_it == render_target_contexts.end() || !target_it->second.output_color.texture) {
                return false;
            }

            candidate.storage_resource = resolve_color_storage(color_export.resource);
            if (candidate.storage_resource.empty()) {
                return false;
            }
            const ResourceSpec* spec = find_resource_spec(candidate.storage_resource);
            const std::string resource_type = spec && !spec->resource_type.empty() ? spec->resource_type : "fbo";
            if (resource_type != "fbo" && resource_type != "multiview_fbo" && resource_type != "color_texture" &&
                resource_type != "multiview_color_texture") {
                return false;
            }

            tgfx::TextureDesc source_desc;
            source_desc.width = static_cast<uint32_t>(spec && spec->size ? spec->size->first : default_width);
            source_desc.height = static_cast<uint32_t>(spec && spec->size ? spec->size->second : default_height);
            source_desc.format =
                resolve_fbo_color_format(spec && spec->format ? *spec->format : std::string{}, default_rt_ctx, *device);
            source_desc.array_layers = static_cast<uint32_t>(spec && spec->array_layers > 0 ? spec->array_layers : 1);
            source_desc.sample_count = static_cast<uint32_t>(spec && spec->samples > 0 ? spec->samples : 1);

            candidate.target = target_it->second.output_color.texture;
            candidate.plan =
                plan_color_output_binding(source_desc, color_export.content, device->texture_desc(candidate.target));
            return true;
        };

        for (const PipelineColorExport& color_export : pipeline.color_exports()) {
            DirectColorCandidate candidate;
            if (plan_direct_color_candidate(color_export, candidate) && candidate.plan.valid) {
                ++valid_color_consumer_counts[candidate.storage_resource];
            }
        }

        std::unordered_map<std::string, tgfx::TextureHandle> direct_color_allocations;
        for (const PipelineColorExport& color_export : pipeline.color_exports()) {
            DirectColorCandidate candidate;
            if (!plan_direct_color_candidate(color_export, candidate)) {
                continue;
            }
            if (candidate.plan.valid && candidate.plan.operation == ColorOutputBindingOp::Direct &&
                valid_color_consumer_counts[candidate.storage_resource] == 1) {
                direct_color_allocations[candidate.storage_resource] = candidate.target;
            }
        }
        tc_profiler_end_section();
        specs_ms = timing_ms(specs_begin, RenderTimingClock::now());

        tc_profiler_begin_section("Allocate Resources");
        const auto allocate_begin = RenderTimingClock::now();
        FBOMap resources;
        pipeline_cache.texture_alias_to_canonical.clear();
        // OUTPUT/DISPLAY no longer travel through the FBOMap — they're
        // native tgfx2 textures owned by the caller (ViewportRenderState)
        // and plumbed straight into tex2_writes below.

        std::vector<const char*> canonical_names = collect_canonical_resources(fg);
        size_t canon_count = canonical_names.size();

        for (size_t i = 0; i < canon_count; i++) {
            const char* canon = canonical_names[i];

            if (pipeline_cache.fbo_compositions.find(canon) != pipeline_cache.fbo_compositions.end()) {
                continue;
            }

            if (find_external_alias(fg, canon, is_external_output_resource) ||
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
                std::vector<const char*> aliases = collect_alias_group(fg, canon);
                size_t alias_count = aliases.size();
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

            if (resource_type == "color_texture" || resource_type == "multiview_color_texture") {
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

                tgfx::TextureUsage usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::ColorAttachment |
                                           tgfx::TextureUsage::CopySrc | tgfx::TextureUsage::CopyDst;
                tgfx::PixelFormat color_format = resolve_fbo_color_format(format, default_rt_ctx, *device);
                tgfx::TextureDesc texture_desc;
                texture_desc.width = static_cast<uint32_t>(tex_width);
                texture_desc.height = static_cast<uint32_t>(tex_height);
                texture_desc.format = color_format;
                texture_desc.array_layers =
                    static_cast<uint32_t>(spec && spec->array_layers > 0 ? spec->array_layers : 1);
                texture_desc.sample_count = static_cast<uint32_t>(spec && spec->samples > 0 ? spec->samples : 1);
                texture_desc.usage = usage;
                auto direct_color = direct_color_allocations.find(canon);
                if (direct_color != direct_color_allocations.end()) {
                    pipeline_cache.texture_pool.erase(canon);
                } else {
                    if (!pipeline_cache.texture_pool.ensure(*device, canon, texture_desc)) {
                        tc::Log::error("RenderEngine::execute_pipeline: failed to allocate color_texture '%s'", canon);
                        tc_profiler_end_section();
                        return;
                    }
                }

                std::vector<const char*> aliases = collect_alias_group(fg, canon);
                size_t alias_count = aliases.size();
                for (size_t j = 0; j < alias_count; j++) {
                    resources[aliases[j]] = nullptr;
                    if (std::string(aliases[j]) != canon) {
                        pipeline_cache.texture_alias_to_canonical[aliases[j]] = canon;
                    }
                }
                continue;
            }

            if (resource_type == "depth_texture" || resource_type == "multiview_depth_texture") {
                int tex_width = default_width;
                int tex_height = default_height;
                if (spec && spec->size) {
                    tex_width = spec->size->first;
                    tex_height = spec->size->second;
                }
                tgfx::TextureUsage usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::DepthStencilAttachment |
                                           tgfx::TextureUsage::CopySrc | tgfx::TextureUsage::CopyDst;
                tgfx::TextureDesc texture_desc;
                texture_desc.width = static_cast<uint32_t>(tex_width);
                texture_desc.height = static_cast<uint32_t>(tex_height);
                texture_desc.format = tgfx::PixelFormat::D32F;
                texture_desc.array_layers =
                    static_cast<uint32_t>(spec && spec->array_layers > 0 ? spec->array_layers : 1);
                texture_desc.sample_count = static_cast<uint32_t>(spec && spec->samples > 0 ? spec->samples : 1);
                texture_desc.usage = usage;
                if (!pipeline_cache.texture_pool.ensure(*device, canon, texture_desc)) {
                    tc::Log::error("RenderEngine::execute_pipeline: failed to allocate depth_texture '%s'", canon);
                    tc_profiler_end_section();
                    return;
                }

                std::vector<const char*> aliases = collect_alias_group(fg, canon);
                size_t alias_count = aliases.size();
                for (size_t j = 0; j < alias_count; j++) {
                    resources[aliases[j]] = nullptr;
                    if (std::string(aliases[j]) != canon) {
                        pipeline_cache.texture_alias_to_canonical[aliases[j]] = canon;
                    }
                }
                continue;
            }

            if (resource_type != "fbo" && resource_type != "multiview_fbo") {
                if (!spec) {
                    tc::Log::error("RenderEngine::execute_pipeline: non-texture resource '%s' has no ResourceSpec",
                                   canon);
                    tc_profiler_end_section();
                    return;
                }
                auto& resource = pipeline_cache.frame_graph_resources[canon];
                if (resource) {
                    const char* cached_type = resource->resource_type();
                    if (!cached_type || resource_type != cached_type) {
                        resource.reset();
                    }
                }
                if (!resource) {
                    resource.reset(create_frame_graph_resource(*spec));
                }
                if (!resource) {
                    tc::Log::error("RenderEngine::execute_pipeline: failed to allocate resource '%s' of type '%s'",
                                   canon,
                                   resource_type.c_str());
                    tc_profiler_end_section();
                    return;
                }
                std::vector<const char*> aliases = collect_alias_group(fg, canon);
                size_t alias_count = aliases.size();
                for (size_t j = 0; j < alias_count; j++) {
                    resources[aliases[j]] = resource.get();
                }
                continue;
            }

            int fbo_width = default_width;
            int fbo_height = default_height;
            int samples = 1;
            int array_layers = 1;
            std::string format;
            TextureFilter filter = TextureFilter::LINEAR;

            if (spec) {
                if (spec->size) {
                    fbo_width = spec->size->first;
                    fbo_height = spec->size->second;
                }
                samples = spec->samples > 0 ? spec->samples : 1;
                array_layers = spec->array_layers > 0 ? spec->array_layers : 1;
                if (spec->format)
                    format = *spec->format;
                filter = spec->filter;
            }

            FBOPool& fbo_pool = pipeline.fbo_pool();

            tgfx::PixelFormat color_fmt = resolve_fbo_color_format(format, default_rt_ctx, *device);
            tgfx::RenderTargetPoolDesc target_desc;
            target_desc.width = fbo_width;
            target_desc.height = fbo_height;
            target_desc.samples = samples;
            target_desc.array_layers = array_layers;
            target_desc.color_format = color_fmt;
            target_desc.has_color = spec ? spec->has_color.value_or(true) : true;
            target_desc.has_depth = spec ? spec->has_depth.value_or(true) : true;
            target_desc.depth_format = tgfx::PixelFormat::D32F;

            tgfx::TextureHandle direct_color;
            auto direct_color_it = direct_color_allocations.find(canon);
            if (direct_color_it != direct_color_allocations.end()) {
                direct_color = direct_color_it->second;
            }
            if (!fbo_pool.ensure_native(*device, canon, target_desc, direct_color)) {
                tc::Log::error("RenderEngine::execute_pipeline: failed to allocate fbo '%s'", canon);
                tc_profiler_end_section();
                return;
            }
            (void)filter;

            std::vector<const char*> aliases = collect_alias_group(fg, canon);
            size_t alias_count = aliases.size();
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

        // Assemble per-resource tgfx2 texture maps from the pool. Native
        // path: handles are owned by IRenderDevice, persistent across
        // frames without any wrap/destroy churn.
        const auto assemble_resources_begin = RenderTimingClock::now();
        std::unordered_map<std::string, tgfx::TextureHandle> tex2_resources;
        std::unordered_map<std::string, tgfx::TextureHandle> tex2_depth_resources;
        if (ctx2 && device) {
            for (const auto& [storage_resource, target] : direct_color_allocations) {
                for (const char* alias : collect_alias_group(fg, storage_resource.c_str())) {
                    tex2_resources[alias] = target;
                }
                tex2_resources[storage_resource] = target;
            }
            FBOPool& fbo_pool = pipeline.fbo_pool();
            for (const auto& [name, res] : resources) {
                tgfx::TextureHandle color_handle = fbo_pool.get_color_tgfx2(name);
                if (color_handle)
                    tex2_resources[name] = color_handle;
                tgfx::TextureHandle depth_handle = fbo_pool.get_depth_tgfx2(name);
                if (depth_handle)
                    tex2_depth_resources[name] = depth_handle;
            }

            for (size_t i = 0; i < canon_count; i++) {
                const char* canon = canonical_names[i];
                tgfx::TextureHandle handle = pipeline_cache.texture_pool.get(canon);
                if (!handle) {
                    continue;
                }
                const tgfx::TextureDesc desc = device->texture_desc(handle);
                const bool depth_texture = tgfx::is_depth_format(desc.format);

                std::vector<const char*> aliases = collect_alias_group(fg, canon);
                size_t alias_count = aliases.size();
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
                        tc::Log::error("RenderEngine: color view '%s' parent '%s' is missing",
                                       view_name.c_str(),
                                       view.parent.c_str());
                    }
                } else {
                    auto it = tex2_depth_resources.find(view.parent);
                    if (it != tex2_depth_resources.end() && it->second) {
                        tex2_resources[view_name] = it->second;
                        tex2_depth_resources[view_name] = it->second;
                    } else {
                        tc::Log::error("RenderEngine: depth view '%s' parent '%s' is missing",
                                       view_name.c_str(),
                                       view.parent.c_str());
                    }
                }
            }

            for (const auto& [fbo_name, composition] : pipeline_cache.fbo_compositions) {
                auto composition_input_is_external = [&](const std::string& name) {
                    if (is_external_output_resource(name.c_str())) {
                        return true;
                    }
                    if (is_external_graph_input_resource(name.c_str(), render_target_contexts)) {
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
                    tc::Log::error("RenderEngine: composed FBO '%s' color input '%s' is missing",
                                   fbo_name.c_str(),
                                   composition.color.c_str());
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
                    tc::Log::error("RenderEngine: composed FBO '%s' depth input '%s' is missing",
                                   fbo_name.c_str(),
                                   composition.depth.c_str());
                }
            }
        }
        assemble_resources_ms = timing_ms(assemble_resources_begin, RenderTimingClock::now());

        struct PreparedColorOutputBinding {
            PipelineColorExport export_desc;
            std::string canonical_resource;
            std::string storage_resource;
            tgfx::TextureHandle source;
            tgfx::TextureHandle target;
            ColorOutputBindingPlan plan;
        };
        std::vector<PreparedColorOutputBinding> color_output_bindings;
        color_output_bindings.reserve(pipeline.color_exports().size());
        for (const PipelineColorExport& color_export : pipeline.color_exports()) {
            auto target_it = color_export.viewport_name.empty() ? default_it
                                                                : render_target_contexts.find(color_export.viewport_name);
            if (target_it == render_target_contexts.end()) {
                tc::Log::error("RenderEngine: color export '%s' refers to unknown target '%s'",
                               color_export.resource.c_str(),
                               color_export.viewport_name.c_str());
                continue;
            }
            const tgfx::TextureHandle target = target_it->second.output_color.texture;
            if (!target) {
                tc::Log::error("RenderEngine: color export '%s' has no physical target texture",
                               color_export.resource.c_str());
                continue;
            }

            tgfx::TextureHandle source;
            auto source_it = tex2_resources.find(color_export.resource);
            if (source_it != tex2_resources.end())
                source = source_it->second;
            if (!source) {
                const char* canonical = tc_frame_graph_canonical_resource(fg, color_export.resource.c_str());
                if (canonical) {
                    source_it = tex2_resources.find(canonical);
                    if (source_it != tex2_resources.end())
                        source = source_it->second;
                }
            }
            if (!source) {
                tc::Log::error("RenderEngine: color export resource '%s' is unavailable",
                               color_export.resource.c_str());
                continue;
            }

            const ColorOutputBindingPlan plan = plan_color_output_binding(
                device->texture_desc(source), color_export.content, device->texture_desc(target));
            const char* canonical = tc_frame_graph_canonical_resource(fg, color_export.resource.c_str());
            const std::string canonical_resource = canonical ? canonical : color_export.resource;
            const std::string storage_resource = resolve_color_storage(canonical_resource);
            if (!plan.valid) {
                tc::Log::error("RenderEngine: refusing to bind scene-linear export '%s' to an SDR color target",
                               color_export.resource.c_str());
                color_output_bindings.push_back(
                    {color_export, canonical_resource, storage_resource, source, target, plan});
                continue;
            }
            color_output_bindings.push_back({color_export, canonical_resource, storage_resource, source, target, plan});
        }

        std::unordered_map<std::string, size_t> color_export_consumer_counts;
        for (const PreparedColorOutputBinding& binding : color_output_bindings) {
            if (binding.plan.valid)
                ++color_export_consumer_counts[binding.storage_resource];
        }
        for (PreparedColorOutputBinding& binding : color_output_bindings) {
            if (!binding.plan.valid || binding.plan.operation != ColorOutputBindingOp::Direct)
                continue;

            // One internal result may fan out to several physical targets. It
            // cannot be rebound directly in that case because the global
            // resource map can name only one texture; preserve the internal
            // producer and copy once to each consumer instead.
            if (color_export_consumer_counts[binding.storage_resource] != 1) {
                binding.plan.operation = ColorOutputBindingOp::CopyOrResolve;
                continue;
            }

            for (const char* alias : collect_alias_group(fg, binding.canonical_resource.c_str()))
                tex2_resources[alias] = binding.target;
            tex2_resources[binding.export_desc.resource] = binding.target;
        }

        std::vector<size_t> debug_capture_boundaries(debug_capture_requests.size(), static_cast<size_t>(-1));
        for (size_t request_index = 0; request_index < debug_capture_requests.size(); ++request_index) {
            FrameGraphCaptureRequest* request = debug_capture_requests[request_index];
            if (!request)
                continue;
            request->status = FrameGraphCaptureRequestStatus::Pending;
            if (request->kind != FrameGraphCaptureRequestKind::Resource || request->paused ||
                request->resource.empty() || !request->capture) {
                continue;
            }
            const char* selected_canonical = tc_frame_graph_canonical_resource(fg, request->resource.c_str());
            const std::string selected = selected_canonical ? selected_canonical : request->resource;
            for (size_t pass_index = 0; pass_index < schedule_count; ++pass_index) {
                tc_pass* scheduled = tc_frame_graph_schedule_at(fg, pass_index);
                if (!scheduled || !scheduled->enabled || scheduled->passthrough)
                    continue;
                const std::vector<const char*> writes = collect_pass_dependencies(scheduled, tc_pass_get_writes);
                for (const char* write : writes) {
                    if (!write)
                        continue;
                    const char* write_canonical = tc_frame_graph_canonical_resource(fg, write);
                    if (selected == (write_canonical ? write_canonical : write)) {
                        debug_capture_boundaries[request_index] = pass_index;
                    }
                }
            }
        }

        auto capture_debug_resource = [&](FrameGraphCaptureRequest& request, const RenderTargetContext& rt_ctx) {
            std::function<tgfx::TextureHandle(const std::string&)> resolve_depth;
            std::function<tgfx::TextureHandle(const std::string&)> resolve_color;
            resolve_color = [&](const std::string& name) -> tgfx::TextureHandle {
                if (find_external_alias(fg, name.c_str(), is_external_color_output)) {
                    return rt_ctx.output_color.texture;
                }
                const char* canonical_c = tc_frame_graph_canonical_resource(fg, name.c_str());
                const std::string canonical = canonical_c ? canonical_c : name;
                if (canonical != name) {
                    tgfx::TextureHandle canonical_handle = resolve_color(canonical);
                    if (canonical_handle)
                        return canonical_handle;
                }
                if (is_external_color_output(name.c_str()))
                    return rt_ctx.output_color.texture;
                auto external = rt_ctx.external_textures.find(name);
                if (external != rt_ctx.external_textures.end())
                    return external->second;
                auto view = pipeline_cache.resource_views.find(name);
                if (view != pipeline_cache.resource_views.end()) {
                    return view->second.attachment == AttachmentKind::Color ? resolve_color(view->second.parent)
                                                                            : tgfx::TextureHandle{};
                }
                auto composition = pipeline_cache.fbo_compositions.find(name);
                if (composition != pipeline_cache.fbo_compositions.end()) {
                    return resolve_color(composition->second.color);
                }
                auto texture = tex2_resources.find(name);
                if (texture != tex2_resources.end()) {
                    return texture->second;
                }
                auto resource = resources.find(name);
                if (resource == resources.end() || !resource->second) {
                    return {};
                }
                const FrameGraphResourceSampledTexture sampled =
                    frame_graph_resource_sampled_texture(*resource->second);
                return sampled.kind == FrameGraphResourceSampledTextureKind::Color ? sampled.texture
                                                                                   : tgfx::TextureHandle{};
            };
            resolve_depth = [&](const std::string& name) -> tgfx::TextureHandle {
                if (find_external_alias(fg, name.c_str(), is_external_output_resource)) {
                    return rt_ctx.output_depth_tex;
                }
                const char* canonical_c = tc_frame_graph_canonical_resource(fg, name.c_str());
                const std::string canonical = canonical_c ? canonical_c : name;
                if (canonical != name) {
                    tgfx::TextureHandle canonical_handle = resolve_depth(canonical);
                    if (canonical_handle)
                        return canonical_handle;
                }
                if (is_external_color_output(name.c_str()) || is_external_depth_output(name.c_str())) {
                    return rt_ctx.output_depth_tex;
                }
                auto view = pipeline_cache.resource_views.find(name);
                if (view != pipeline_cache.resource_views.end()) {
                    return view->second.attachment == AttachmentKind::Depth ? resolve_depth(view->second.parent)
                                                                            : tgfx::TextureHandle{};
                }
                auto composition = pipeline_cache.fbo_compositions.find(name);
                if (composition != pipeline_cache.fbo_compositions.end()) {
                    tgfx::TextureHandle depth = resolve_depth(composition->second.depth);
                    return depth ? depth : resolve_color(composition->second.depth);
                }
                auto texture = tex2_depth_resources.find(name);
                if (texture != tex2_depth_resources.end()) {
                    return texture->second;
                }
                auto resource = resources.find(name);
                if (resource == resources.end() || !resource->second) {
                    return {};
                }
                const FrameGraphResourceSampledTexture sampled =
                    frame_graph_resource_sampled_texture(*resource->second);
                return sampled.kind == FrameGraphResourceSampledTextureKind::Depth ? sampled.texture
                                                                                   : tgfx::TextureHandle{};
            };

            const tgfx::TextureHandle color = resolve_color(request.resource);
            const tgfx::TextureHandle depth = resolve_depth(request.resource);
            const tgfx::TextureHandle primary = color ? color : depth;
            if (!primary) {
                request.status = FrameGraphCaptureRequestStatus::ResourceUnavailable;
                tc::Log::warn("[FrameGraphDebugger] resource '%s' is unavailable in the live execution",
                              request.resource.c_str());
                return;
            }
            request.capture->reset_capture();
            request.capture->capture_direct_via_ctx2(
                ctx2, primary, 0, 0, tgfx::PixelFormat::RGBA8_UNorm, request.max_long_edge);
            if (request.depth_capture) {
                if (color && depth && depth != color) {
                    request.depth_capture->capture_direct_via_ctx2(
                        ctx2, depth, 0, 0, tgfx::PixelFormat::RGBA8_UNorm, request.max_long_edge);
                } else {
                    request.depth_capture->reset_capture();
                }
            }
            request.status = request.capture->has_capture() ? FrameGraphCaptureRequestStatus::Captured
                                                            : FrameGraphCaptureRequestStatus::ResourceUnavailable;
            if (request.status == FrameGraphCaptureRequestStatus::ResourceUnavailable) {
                tc::Log::error("[FrameGraphDebugger] failed to capture resource '%s'", request.resource.c_str());
            }
        };

        // Resources without a writer in this schedule are external/read-only and
        // already exist after execution-resource assembly.
        for (size_t request_index = 0; request_index < debug_capture_requests.size(); ++request_index) {
            FrameGraphCaptureRequest* request = debug_capture_requests[request_index];
            if (!request || request->kind != FrameGraphCaptureRequestKind::Resource || request->paused ||
                request->resource.empty() || !request->capture) {
                continue;
            }
            if (debug_capture_boundaries[request_index] == static_cast<size_t>(-1)) {
                capture_debug_resource(*request, default_rt_ctx);
            }
        }

        struct PreparedPass {
            size_t schedule_index = 0;
            tc_pass* pass = nullptr;
            const RenderTargetContext* render_target = nullptr;
            ExecuteContext context;
            tc_raster_pass_contract raster_contract{};
            tc_raster_resolve_contract resolve_contract{};
            bool has_raster_contract = false;
            bool has_resolve_contract = false;
            std::string canonical_raster_target;
            std::string canonical_resolve_source;
            std::string canonical_resolve_target;
            double preparation_ms = 0.0;
        };

        const auto prepare_pass = [&](size_t i, tc_pass* pass) -> PreparedPass {
            const auto preparation_begin = RenderTimingClock::now();
            PreparedPass prepared;
            prepared.schedule_index = i;
            prepared.pass = pass;

            std::string pass_render_target_name = default_target;
            if (pass->viewport_name && pass->viewport_name[0] != '\0') {
                pass_render_target_name = pass->viewport_name;
            }

            auto rt_it = render_target_contexts.find(pass_render_target_name);
            if (rt_it == render_target_contexts.end()) {
                rt_it = default_it;
            }
            const RenderTargetContext& rt_ctx = rt_it->second;
            prepared.render_target = &rt_ctx;
            const RenderExecutionTarget& execution_target = execution.targets.at(rt_it->first);

            std::vector<const char*> reads = collect_pass_dependencies(pass, tc_pass_get_reads);
            std::vector<const char*> writes = collect_pass_dependencies(pass, tc_pass_get_writes);

            Tex2Map pass_tex2_reads;
            Tex2Map pass_tex2_writes;
            Tex2Map pass_tex2_depth_reads;
            Tex2Map pass_tex2_depth_writes;
            ResourceMap pass_frame_graph_resources;

            std::function<tgfx::TextureHandle(const std::string&)> resolve_depth_resource;
            std::function<tgfx::TextureHandle(const std::string&)> resolve_color_resource;

            resolve_color_resource = [&](const std::string& name) -> tgfx::TextureHandle {
                if (find_external_alias(fg, name.c_str(), is_external_color_output)) {
                    return rt_ctx.output_color.texture;
                }
                const char* canonical_c = tc_frame_graph_canonical_resource(fg, name.c_str());
                std::string canonical = canonical_c ? canonical_c : name;
                if (canonical != name) {
                    tgfx::TextureHandle canonical_handle = resolve_color_resource(canonical);
                    if (canonical_handle) {
                        return canonical_handle;
                    }
                }
                if (is_external_color_output(name.c_str())) {
                    return rt_ctx.output_color.texture;
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
                if (find_external_alias(fg, name.c_str(), is_external_output_resource)) {
                    return rt_ctx.output_depth_tex;
                }
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

            auto collect_frame_graph_resource = [&](const char* name) {
                auto it = resources.find(name);
                if (it != resources.end() && it->second) {
                    pass_frame_graph_resources[name] = it->second;
                    const FrameGraphResourceSampledTexture sampled = frame_graph_resource_sampled_texture(*it->second);
                    if (sampled.texture) {
                        if (sampled.kind == FrameGraphResourceSampledTextureKind::Depth) {
                            pass_tex2_depth_reads[name] = sampled.texture;
                        } else {
                            pass_tex2_reads[name] = sampled.texture;
                        }
                    }
                }
            };

            for (const char* read_name : reads) {
                if (is_external_color_output(read_name)) {
                    if (rt_ctx.output_color.texture)
                        pass_tex2_reads[read_name] = rt_ctx.output_color.texture;
                    if (rt_ctx.output_depth_tex)
                        pass_tex2_depth_reads[read_name] = rt_ctx.output_depth_tex;
                    continue;
                }
                if (is_external_depth_output(read_name)) {
                    if (rt_ctx.output_depth_tex)
                        pass_tex2_depth_reads[read_name] = rt_ctx.output_depth_tex;
                    continue;
                }
                collect_frame_graph_resource(read_name);
                auto ext_it = rt_ctx.external_textures.find(read_name);
                if (ext_it != rt_ctx.external_textures.end() && ext_it->second) {
                    pass_tex2_reads[read_name] = ext_it->second;
                    continue;
                }
                if (tgfx::TextureHandle color_handle = resolve_color_resource(read_name))
                    pass_tex2_reads[read_name] = color_handle;
                if (tgfx::TextureHandle depth_handle = resolve_depth_resource(read_name))
                    pass_tex2_depth_reads[read_name] = depth_handle;
            }

            for (const char* write_name : writes) {
                if (is_external_color_output(write_name)) {
                    if (rt_ctx.output_color.texture)
                        pass_tex2_writes[write_name] = rt_ctx.output_color.texture;
                    if (rt_ctx.output_depth_tex)
                        pass_tex2_depth_writes[write_name] = rt_ctx.output_depth_tex;
                } else if (is_external_depth_output(write_name)) {
                    if (rt_ctx.output_depth_tex)
                        pass_tex2_depth_writes[write_name] = rt_ctx.output_depth_tex;
                } else {
                    collect_frame_graph_resource(write_name);
                    if (tgfx::TextureHandle color_handle = resolve_color_resource(write_name))
                        pass_tex2_writes[write_name] = color_handle;
                    if (tgfx::TextureHandle depth_handle = resolve_depth_resource(write_name))
                        pass_tex2_depth_writes[write_name] = depth_handle;
                }
            }

            ExecuteContext& ctx = prepared.context;
            ctx.ctx2 = ctx2;
            ctx.tex2_reads = std::move(pass_tex2_reads);
            ctx.tex2_writes = std::move(pass_tex2_writes);
            ctx.tex2_depth_reads = std::move(pass_tex2_depth_reads);
            ctx.tex2_depth_writes = std::move(pass_tex2_depth_writes);
            ctx.frame_graph_resources = std::move(pass_frame_graph_resources);
            ctx.render_rect = rt_ctx.render_rect;
            ctx.view = rt_ctx.view;
            ctx.render_target_name = rt_ctx.name;
            ctx.render_item_snapshot = execution_target.render_items;
            ctx.material_texture_sources = &rt_ctx.material_texture_sources;
            ctx.capabilities = execution_target.capabilities;
            for (FrameGraphCaptureRequest* request : debug_capture_requests) {
                if (!request || request->kind != FrameGraphCaptureRequestKind::InternalSymbol || request->paused ||
                    !request->capture || request->pass_index >= pipeline.pass_count()) {
                    continue;
                }
                if (pipeline.get_pass_at(request->pass_index) == pass)
                    ctx.debug_internal_capture_requests.push_back(request);
            }

            prepared.has_raster_contract =
                tc_pass_get_raster_contract(pass, &ctx, &prepared.raster_contract) &&
                prepared.raster_contract.struct_size >= sizeof(tc_raster_pass_contract) &&
                prepared.raster_contract.target_resource && prepared.raster_contract.target_resource[0];
            if (prepared.has_raster_contract) {
                const char* canonical =
                    tc_frame_graph_canonical_resource(fg, prepared.raster_contract.target_resource);
                prepared.canonical_raster_target = canonical ? canonical : prepared.raster_contract.target_resource;
            }
            prepared.has_resolve_contract =
                tc_pass_get_raster_resolve_contract(pass, &ctx, &prepared.resolve_contract) &&
                prepared.resolve_contract.struct_size >= sizeof(tc_raster_resolve_contract) &&
                prepared.resolve_contract.source_resource && prepared.resolve_contract.source_resource[0] &&
                prepared.resolve_contract.target_resource && prepared.resolve_contract.target_resource[0];
            if (prepared.has_resolve_contract) {
                const char* canonical_source =
                    tc_frame_graph_canonical_resource(fg, prepared.resolve_contract.source_resource);
                const char* canonical_target =
                    tc_frame_graph_canonical_resource(fg, prepared.resolve_contract.target_resource);
                prepared.canonical_resolve_source =
                    canonical_source ? canonical_source : prepared.resolve_contract.source_resource;
                prepared.canonical_resolve_target =
                    canonical_target ? canonical_target : prepared.resolve_contract.target_resource;
            }
            prepared.preparation_ms = timing_ms(preparation_begin, RenderTimingClock::now());
            return prepared;
        };

        std::vector<PreparedPass> prepared_passes;
        prepared_passes.reserve(schedule_count);
        for (size_t i = 0; i < schedule_count; ++i) {
            tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
            if (!pass || !pass->enabled || pass->passthrough)
                continue;
            prepared_passes.push_back(prepare_pass(i, pass));
        }

        const auto has_resource_capture_boundary = [&](size_t schedule_index) {
            for (size_t boundary : debug_capture_boundaries) {
                if (boundary == schedule_index)
                    return true;
            }
            return false;
        };

        const tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        const auto raster_targets_match = [](const PreparedPass& a, const PreparedPass& b) {
            if (!a.has_raster_contract || !b.has_raster_contract || !a.raster_contract.fusion_eligible ||
                !b.raster_contract.fusion_eligible || a.canonical_raster_target != b.canonical_raster_target ||
                a.context.render_target_name != b.context.render_target_name ||
                a.context.render_rect.width != b.context.render_rect.width ||
                a.context.render_rect.height != b.context.render_rect.height ||
                a.raster_contract.view_count != b.raster_contract.view_count ||
                a.raster_contract.has_color != b.raster_contract.has_color ||
                a.raster_contract.has_depth != b.raster_contract.has_depth ||
                b.raster_contract.color_load != TC_RASTER_LOAD ||
                (b.raster_contract.has_depth && b.raster_contract.depth_load != TC_RASTER_LOAD)) {
                return false;
            }
            const char* a_target = a.raster_contract.target_resource;
            const char* b_target = b.raster_contract.target_resource;
            const auto a_color = a.context.tex2_writes.find(a_target);
            const auto b_color = b.context.tex2_writes.find(b_target);
            const auto a_depth = a.context.tex2_depth_writes.find(a_target);
            const auto b_depth = b.context.tex2_depth_writes.find(b_target);
            return (a_color == a.context.tex2_writes.end() ? tgfx::TextureHandle{} : a_color->second) ==
                       (b_color == b.context.tex2_writes.end() ? tgfx::TextureHandle{} : b_color->second) &&
                   (a_depth == a.context.tex2_depth_writes.end() ? tgfx::TextureHandle{} : a_depth->second) ==
                       (b_depth == b.context.tex2_depth_writes.end() ? tgfx::TextureHandle{} : b_depth->second);
        };

        const auto lookup_color_read = [](const PreparedPass& prepared, const char* resource) {
            const auto it = prepared.context.tex2_reads.find(resource);
            return it == prepared.context.tex2_reads.end() ? tgfx::TextureHandle{} : it->second;
        };
        const auto lookup_color_write = [](const PreparedPass& prepared, const char* resource) {
            const auto it = prepared.context.tex2_writes.find(resource);
            return it == prepared.context.tex2_writes.end() ? tgfx::TextureHandle{} : it->second;
        };

        const auto canonical_resource_name = [&](const std::string& resource) {
            const char* canonical = tc_frame_graph_canonical_resource(fg, resource.c_str());
            return std::string(canonical ? canonical : resource);
        };
        std::unordered_map<std::string, ResourceSpec> canonical_clear_specs;
        for (const ResourceSpec& spec : specs) {
            if (!spec.clear_color && !spec.clear_depth)
                continue;
            const std::string canonical = canonical_resource_name(spec.resource);
            auto [it, inserted] = canonical_clear_specs.try_emplace(canonical, spec);
            ResourceSpec& merged = it->second;
            if (inserted) {
                merged.resource = canonical;
                continue;
            }
            if (spec.clear_color) {
                const bool same_clear = merged.clear_color && spec.clear_color &&
                                        merged.clear_color->r == spec.clear_color->r &&
                                        merged.clear_color->g == spec.clear_color->g &&
                                        merged.clear_color->b == spec.clear_color->b &&
                                        merged.clear_color->a == spec.clear_color->a;
                if (merged.clear_color && !same_clear) {
                    tc::Log::error("RenderEngine::execute_pipeline: conflicting clear colors for aliased resource '%s'",
                                   canonical.c_str());
                } else {
                    merged.clear_color = spec.clear_color;
                }
            }
            if (spec.clear_depth) {
                if (merged.clear_depth && merged.clear_depth != spec.clear_depth) {
                    tc::Log::error("RenderEngine::execute_pipeline: conflicting clear depths for aliased resource '%s'",
                                   canonical.c_str());
                } else {
                    merged.clear_depth = spec.clear_depth;
                }
            }
        }

        // A resolve contract promises a complete image write, but its target
        // clear may only be discarded when the resolve is the target texture's
        // first graph access. An earlier read or partial write still depends on
        // the render-target clear requested by the caller.
        std::unordered_set<uint32_t> fully_overwritten_external_colors;
        std::unordered_set<uint32_t> textures_accessed;
        for (const PreparedPass& prepared : prepared_passes) {
            std::unordered_set<uint32_t> pass_reads;
            std::unordered_set<uint32_t> pass_writes;
            for (const auto& [_, texture] : prepared.context.tex2_reads) {
                if (texture)
                    pass_reads.insert(texture.id);
            }
            for (const auto& [_, texture] : prepared.context.tex2_depth_reads) {
                if (texture)
                    pass_reads.insert(texture.id);
            }
            for (const auto& [_, texture] : prepared.context.tex2_writes) {
                if (texture)
                    pass_writes.insert(texture.id);
            }
            for (const auto& [_, texture] : prepared.context.tex2_depth_writes) {
                if (texture)
                    pass_writes.insert(texture.id);
            }

            tgfx::TextureHandle resolve_target{};
            if (prepared.has_resolve_contract) {
                resolve_target = lookup_color_write(prepared, prepared.resolve_contract.target_resource);
            }
            for (uint32_t texture_id : pass_reads)
                textures_accessed.insert(texture_id);
            for (uint32_t texture_id : pass_writes) {
                const bool first_access = textures_accessed.insert(texture_id).second;
                if (first_access && resolve_target.id == texture_id && !pass_reads.contains(texture_id))
                    fully_overwritten_external_colors.insert(texture_id);
            }
        }

        tc_profiler_begin_section("Clear Render Target Contexts");
        const auto clear_targets_begin = RenderTimingClock::now();
        if (ctx2) {
            for (const auto& [render_target_name, rt_ctx] : render_target_contexts) {
                if (!rt_ctx.clear_color_enabled && !rt_ctx.clear_depth_enabled)
                    continue;
                if (rt_ctx.clear_color_enabled && !rt_ctx.output_color.texture) {
                    tc::Log::error("RenderEngine::execute_pipeline: render target context '%s' requested a color "
                                   "clear but its output color texture is missing",
                                   render_target_name.c_str());
                }
                if (rt_ctx.clear_depth_enabled && !rt_ctx.output_depth_tex) {
                    tc::Log::error("RenderEngine::execute_pipeline: render target context '%s' requested a depth "
                                   "clear but its output depth texture is missing",
                                   render_target_name.c_str());
                }
                const bool clear_color = rt_ctx.clear_color_enabled && rt_ctx.output_color.texture &&
                                         !fully_overwritten_external_colors.contains(rt_ctx.output_color.texture.id);
                const bool clear_depth = rt_ctx.clear_depth_enabled && rt_ctx.output_depth_tex;
                if (!clear_color && !clear_depth)
                    continue;

                if (!begin_clear_texture_pass(*ctx2,
                                              *device,
                                              clear_color ? rt_ctx.output_color.texture : tgfx::TextureHandle{},
                                              clear_depth ? rt_ctx.output_depth_tex : tgfx::TextureHandle{},
                                              clear_color ? &rt_ctx.clear_linear_color : nullptr,
                                              rt_ctx.clear_depth,
                                              clear_depth)) {
                    continue;
                }
                ctx2->set_viewport(0, 0, std::max(1, rt_ctx.render_rect.width), std::max(1, rt_ctx.render_rect.height));
                ctx2->end_pass();
            }
        }
        tc_profiler_end_section();
        clear_targets_ms = timing_ms(clear_targets_begin, RenderTimingClock::now());

        // Resource initialization belongs to the first physical raster scope
        // whenever that pass can be recorded by the executor. This preserves
        // tile-local Clear loadOps for MSAA color/depth instead of performing a
        // separate clear followed by an off-chip Load.
        std::unordered_map<std::string, const ResourceSpec*> deferred_raster_clears;
        std::unordered_set<std::string> resources_accessed;
        for (const PreparedPass& prepared : prepared_passes) {
            std::unordered_set<std::string> pass_reads;
            std::unordered_set<std::string> pass_writes;
            const std::vector<const char*> reads = collect_pass_dependencies(prepared.pass, tc_pass_get_reads);
            for (const char* read : reads) {
                if (read)
                    pass_reads.insert(canonical_resource_name(read));
            }
            const std::vector<const char*> writes = collect_pass_dependencies(prepared.pass, tc_pass_get_writes);
            for (const char* write : writes) {
                if (write)
                    pass_writes.insert(canonical_resource_name(write));
            }

            std::unordered_set<std::string> pass_accesses = pass_reads;
            pass_accesses.insert(pass_writes.begin(), pass_writes.end());
            for (const std::string& canonical : pass_accesses) {
                if (!resources_accessed.insert(canonical).second)
                    continue;
                const auto spec = canonical_clear_specs.find(canonical);
                if (spec == canonical_clear_specs.end())
                    continue;
                const bool attachment_write = pass_writes.contains(canonical) && prepared.has_raster_contract &&
                                              prepared.canonical_raster_target == canonical;
                if (ctx2 && sync_mode == TC_RENDER_SYNC_NONE && attachment_write &&
                    prepared.raster_contract.fusion_eligible) {
                    deferred_raster_clears.emplace(canonical, &spec->second);
                }
            }
        }

        tc_profiler_begin_section("Clear Resources");
        const auto clear_resources_begin = RenderTimingClock::now();
        if (ctx2) {
            for (const auto& [canonical, spec] : canonical_clear_specs) {
                if (spec.resource_type != "fbo" && spec.resource_type != "multiview_fbo" &&
                    !spec.resource_type.empty()) {
                    continue;
                }
                if (deferred_raster_clears.contains(canonical))
                    continue;

                auto ct = tex2_resources.find(canonical);
                auto dt = tex2_depth_resources.find(canonical);
                const tgfx::TextureHandle color =
                    ct == tex2_resources.end() ? tgfx::TextureHandle{} : ct->second;
                const tgfx::TextureHandle depth =
                    dt == tex2_depth_resources.end() ? tgfx::TextureHandle{} : dt->second;
                if (!color && !depth)
                    continue;

                termin::LinearColor clear_rgba{0.0f, 0.0f, 0.0f, 1.0f};
                const termin::LinearColor* clear_color = nullptr;
                if (spec.clear_color) {
                    clear_rgba = *spec.clear_color;
                    clear_color = &clear_rgba;
                }
                const float clear_depth = spec.clear_depth.value_or(1.0f);
                if (!begin_clear_texture_pass(
                        *ctx2, *device, color, depth, clear_color, clear_depth, spec.clear_depth.has_value())) {
                    continue;
                }
                const tgfx::TextureDesc desc = device->texture_desc(color ? color : depth);
                ctx2->set_viewport(0, 0, static_cast<int>(desc.width), static_cast<int>(desc.height));
                ctx2->end_pass();
            }
        }
        tc_profiler_end_section();
        clear_resources_ms = timing_ms(clear_resources_begin, RenderTimingClock::now());

        const auto texture_is_read_after = [&](tgfx::TextureHandle texture, size_t first_later_pass) {
            if (!texture)
                return false;
            for (size_t i = first_later_pass; i < prepared_passes.size(); ++i) {
                for (const auto& [_, read] : prepared_passes[i].context.tex2_reads) {
                    if (read == texture)
                        return true;
                }
                for (const auto& [_, read] : prepared_passes[i].context.tex2_depth_reads) {
                    if (read == texture)
                        return true;
                }
            }
            return false;
        };

        const auto record_pass_timing = [&](PreparedPass& prepared, double execute_ms) {
            const char* pass_name =
                prepared.pass->pass_name ? prepared.pass->pass_name : "UnnamedPass";
            const double pass_ms = prepared.preparation_ms + execute_ms;
            pass_total_ms += pass_ms;
            RenderPassTimingStats& pass_stats = local_pass_stats[pass_name];
            pass_stats.count += 1;
            pass_stats.total_ms += pass_ms;
        };

        const auto capture_after_pass = [&](const PreparedPass& prepared) {
            for (size_t request_index = 0; request_index < debug_capture_requests.size(); ++request_index) {
                FrameGraphCaptureRequest* request = debug_capture_requests[request_index];
                if (!request || request->kind != FrameGraphCaptureRequestKind::Resource || request->paused ||
                    request->status != FrameGraphCaptureRequestStatus::Pending) {
                    continue;
                }
                if (debug_capture_boundaries[request_index] == prepared.schedule_index)
                    capture_debug_resource(*request, *prepared.render_target);
            }
        };

        const auto execute_standalone = [&](PreparedPass& prepared) {
            const char* pass_name = prepared.pass->pass_name ? prepared.pass->pass_name : "UnnamedPass";
            tc_profiler_begin_section(pass_name);
            const auto execute_begin = RenderTimingClock::now();
            device->reset_state();
            tc_pass_execute(prepared.pass, &prepared.context);
            capture_after_pass(prepared);
            if (sync_mode == TC_RENDER_SYNC_FLUSH)
                device->flush();
            else if (sync_mode == TC_RENDER_SYNC_FINISH)
                device->finish();
            const double execute_ms = timing_ms(execute_begin, RenderTimingClock::now());
            tc_profiler_end_section();
            record_pass_timing(prepared, execute_ms);
        };

        tc_profiler_begin_section("Execute Passes");
        size_t prepared_index = 0;
        while (prepared_index < prepared_passes.size()) {
            size_t group_end = prepared_index + 1;
            if (ctx2 && sync_mode == TC_RENDER_SYNC_NONE && prepared_passes[prepared_index].has_raster_contract &&
                prepared_passes[prepared_index].raster_contract.fusion_eligible) {
                while (group_end < prepared_passes.size() &&
                       !has_resource_capture_boundary(prepared_passes[group_end - 1].schedule_index) &&
                       raster_targets_match(prepared_passes[group_end - 1], prepared_passes[group_end])) {
                    ++group_end;
                }
            }

            PreparedPass* absorbed_resolve = nullptr;
            if (ctx2 && sync_mode == TC_RENDER_SYNC_NONE && group_end < prepared_passes.size() &&
                !has_resource_capture_boundary(prepared_passes[group_end - 1].schedule_index)) {
                PreparedPass& candidate = prepared_passes[group_end];
                PreparedPass& last_raster = prepared_passes[group_end - 1];
                if (candidate.has_resolve_contract && candidate.resolve_contract.fusion_eligible &&
                    last_raster.has_raster_contract && last_raster.raster_contract.fusion_eligible &&
                    candidate.canonical_resolve_source == last_raster.canonical_raster_target &&
                    candidate.resolve_contract.view_count == last_raster.raster_contract.view_count) {
                    const tgfx::TextureHandle source =
                        lookup_color_read(candidate, candidate.resolve_contract.source_resource);
                    const tgfx::TextureHandle target =
                        lookup_color_write(candidate, candidate.resolve_contract.target_resource);
                    const tgfx::TextureHandle raster_color =
                        lookup_color_write(last_raster, last_raster.raster_contract.target_resource);
                    if (source && target && source == raster_color)
                        absorbed_resolve = &candidate;
                }
            }

            const bool can_use_physical_scope =
                ctx2 && sync_mode == TC_RENDER_SYNC_NONE &&
                prepared_passes[prepared_index].has_raster_contract &&
                prepared_passes[prepared_index].raster_contract.fusion_eligible;
            if (!can_use_physical_scope) {
                PreparedPass& prepared = prepared_passes[prepared_index];
                execute_standalone(prepared);
                ++prepared_index;
                continue;
            }

            PreparedPass& first = prepared_passes[prepared_index];
            const char* target = first.raster_contract.target_resource;
            const auto color_it = first.context.tex2_writes.find(target);
            const auto depth_it = first.context.tex2_depth_writes.find(target);
            const tgfx::TextureHandle color =
                color_it == first.context.tex2_writes.end() ? tgfx::TextureHandle{} : color_it->second;
            const tgfx::TextureHandle depth =
                depth_it == first.context.tex2_depth_writes.end() ? tgfx::TextureHandle{} : depth_it->second;
            const auto deferred_clear_it = deferred_raster_clears.find(first.canonical_raster_target);
            const ResourceSpec* deferred_clear =
                deferred_clear_it == deferred_raster_clears.end() ? nullptr : deferred_clear_it->second;

            tgfx::RenderPassDesc base_scope;
            if (first.raster_contract.has_color && color) {
                tgfx::ColorAttachmentDesc attachment;
                attachment.texture = color;
                attachment.load = (deferred_clear && deferred_clear->clear_color) ||
                                          first.raster_contract.color_load == TC_RASTER_CLEAR
                                      ? tgfx::LoadOp::Clear
                                      : tgfx::LoadOp::Load;
                if (deferred_clear && deferred_clear->clear_color) {
                    attachment.clear_color = *deferred_clear->clear_color;
                }
                if (absorbed_resolve) {
                    attachment.resolve_texture =
                        lookup_color_write(*absorbed_resolve, absorbed_resolve->resolve_contract.target_resource);
                    if (!texture_is_read_after(color, group_end + 1))
                        attachment.store = tgfx::StoreOp::DontCare;
                }
                base_scope.colors.push_back(attachment);
            }
            if (first.raster_contract.has_depth && depth) {
                base_scope.has_depth = true;
                base_scope.depth.texture = depth;
                base_scope.depth.load = (deferred_clear && deferred_clear->clear_depth) ||
                                                first.raster_contract.depth_load == TC_RASTER_CLEAR
                                            ? tgfx::LoadOp::Clear
                                            : tgfx::LoadOp::Load;
                if (deferred_clear && deferred_clear->clear_depth)
                    base_scope.depth.clear_depth = *deferred_clear->clear_depth;
                if (absorbed_resolve && !texture_is_read_after(depth, group_end + 1))
                    base_scope.depth.store = tgfx::StoreOp::DontCare;
            }

            device->reset_state();
            bool scope_open = false;
            if (first.raster_contract.view_count > 1) {
                tgfx::MultiviewRenderPassDesc scope;
                scope.colors = base_scope.colors;
                scope.depth = base_scope.depth;
                scope.has_depth = base_scope.has_depth;
                scope.view_count = first.raster_contract.view_count;
                if (absorbed_resolve && !texture_is_read_after(color, group_end + 1))
                    scope.color_final_state = tgfx::MultiviewColorFinalState::ColorAttachment;
                scope_open = ctx2->begin_multiview_pass(scope);
            } else {
                scope_open = ctx2->begin_pass(base_scope);
            }
            if (!scope_open) {
                tc::Log::error("RenderEngine: failed to open fused raster scope for resource '%s'",
                               first.canonical_raster_target.c_str());
                if (deferred_clear) {
                    termin::LinearColor clear_rgba{0.0f, 0.0f, 0.0f, 1.0f};
                    const termin::LinearColor* clear_color = nullptr;
                    if (deferred_clear->clear_color) {
                        clear_rgba = *deferred_clear->clear_color;
                        clear_color = &clear_rgba;
                    }
                    if (begin_clear_texture_pass(*ctx2,
                                                 *device,
                                                 color,
                                                 depth,
                                                 clear_color,
                                                 deferred_clear->clear_depth.value_or(1.0f),
                                                 deferred_clear->clear_depth.has_value())) {
                        ctx2->set_viewport(0,
                                           0,
                                           std::max(1, first.context.render_rect.width),
                                           std::max(1, first.context.render_rect.height));
                        ctx2->end_pass();
                    }
                    deferred_raster_clears.erase(first.canonical_raster_target);
                }
                execute_standalone(first);
                ++prepared_index;
                continue;
            }
            if (deferred_clear)
                deferred_raster_clears.erase(first.canonical_raster_target);

            for (size_t member_index = prepared_index; member_index < group_end; ++member_index) {
                PreparedPass& prepared = prepared_passes[member_index];
                const char* pass_name = prepared.pass->pass_name ? prepared.pass->pass_name : "UnnamedPass";
                tc_profiler_begin_section(pass_name);
                const auto execute_begin = RenderTimingClock::now();
                ctx2->begin_logical_pass();
                if (!tc_pass_record_raster(prepared.pass, &prepared.context)) {
                    tc::Log::error("RenderEngine: fused raster pass '%s' failed to record", pass_name);
                }
                if (prepared.raster_contract.attachment_barrier_after && member_index + 1 < group_end)
                    ctx2->framebuffer_local_barrier();
                const double execute_ms = timing_ms(execute_begin, RenderTimingClock::now());
                tc_profiler_end_section();
                record_pass_timing(prepared, execute_ms);
            }
            ctx2->end_pass();
            for (size_t member_index = prepared_index; member_index < group_end; ++member_index)
                capture_after_pass(prepared_passes[member_index]);
            if (absorbed_resolve) {
                const char* pass_name = absorbed_resolve->pass->pass_name ? absorbed_resolve->pass->pass_name
                                                                          : "UnnamedPass";
                tc_profiler_begin_section(pass_name);
                tc_profiler_end_section();
                record_pass_timing(*absorbed_resolve, 0.0);
                capture_after_pass(*absorbed_resolve);
                prepared_index = group_end + 1;
            } else {
                prepared_index = group_end;
            }
        }
        tc_profiler_end_section();

        for (const PreparedColorOutputBinding& binding : color_output_bindings) {
            if (!binding.plan.valid || binding.plan.operation == ColorOutputBindingOp::Direct ||
                binding.source == binding.target) {
                continue;
            }
            try {
                if (binding.plan.operation == ColorOutputBindingOp::Transform) {
                    const tgfx::TextureDesc source_desc = device->texture_desc(binding.source);
                    const tgfx::TextureDesc target_desc = device->texture_desc(binding.target);
                    if (!output_transform_.record(*ctx2,
                                                  binding.source,
                                                  binding.target,
                                                  make_output_transform_params(source_desc,
                                                                               binding.export_desc.content,
                                                                               target_desc))) {
                        tc::Log::error("RenderEngine: color export '%s' output transform failed",
                                       binding.export_desc.resource.c_str());
                    }
                } else {
                    // Pure transport/resolve remains a backend copy operation.
                    ctx2->blit(binding.source, binding.target);
                }
            } catch (const std::exception& error) {
                tc::Log::error("RenderEngine: color export '%s' binding failed: %s",
                               binding.export_desc.resource.c_str(),
                               error.what());
            }
        }

        for (FrameGraphCaptureRequest* request : debug_capture_requests) {
            if (request && !request->paused && request->status == FrameGraphCaptureRequestStatus::Pending) {
                request->status = FrameGraphCaptureRequestStatus::ResourceUnavailable;
                if (request->kind == FrameGraphCaptureRequestKind::Resource) {
                    tc::Log::warn("[FrameGraphDebugger] resource '%s' capture boundary was not executed",
                                  request->resource.c_str());
                } else {
                    tc::Log::warn("[FrameGraphDebugger] internal symbol '%s' in pass %zu was not executed",
                                  request->internal_symbol.c_str(),
                                  request->pass_index);
                }
            }
        }

        const auto end_frame_begin = RenderTimingClock::now();
        if (owns_tgfx2_frame) {
            ctx2->end_frame();
        }
        end_frame_ms = timing_ms(end_frame_begin, RenderTimingClock::now());

        if (collect_render_timing) {
            RenderEngineTimingStats& stats = render_engine_timing_stats();
            for (const auto& [name, target] : execution.targets) {
                (void)name;
                const RenderItemSnapshotCounters& counters = target.render_items->counters();
                stats.render_item_scene_traversals += counters.source_traversals;
                stats.render_item_producers += counters.producers;
                stats.render_items += counters.emitted_items;
            }
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
