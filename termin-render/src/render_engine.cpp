#include <termin/render/render_engine.hpp>

#include <cstdlib>

#include <tcbase/tc_log.hpp>
#include "tc_profiler.h"
#include "tc_project_settings.h"

#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"
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

RenderEngine::RenderEngine(GraphicsBackend* graphics)
    : graphics(graphics)
{
    ensure_scene_extensions_for_render();
}

// Out-of-line destructor so unique_ptr<tgfx2::*> members can use forward
// declarations in the header; the full tgfx2 types are visible here.
RenderEngine::~RenderEngine() = default;

void RenderEngine::ensure_tgfx2() {
    // Diagnostic escape hatch: setting TERMIN_DISABLE_TGFX2=1 keeps the tgfx2
    // stack un-initialised, so ctx.ctx2 stays nullptr and every migrated pass
    // takes its legacy fallback path. Used to isolate whether a rendering
    // regression is caused by the tgfx2 path or not.
    static const bool disable_tgfx2 = []() {
        const char* env = std::getenv("TERMIN_DISABLE_TGFX2");
        return env && env[0] && env[0] != '0';
    }();
    if (disable_tgfx2) {
        return;
    }

    if (tgfx2_ctx_) {
        return;
    }
    if (!graphics) {
        return;
    }
    // Assumes the GL context is current (caller is inside a render frame).
    tgfx2_device_ = std::make_unique<tgfx2::OpenGLRenderDevice>();
    tgfx2_cache_ = std::make_unique<tgfx2::PipelineCache>(*tgfx2_device_);
    tgfx2_ctx_ = std::make_unique<tgfx2::RenderContext2>(*tgfx2_device_, *tgfx2_cache_);

    // Stage 6: swap the tgfx_gpu_ops vtable from the raw-GL implementation
    // installed by OpenGLGraphicsBackend::ensure_ready() to the tgfx2-backed
    // one. From now on tc_shader_compile_gpu, tc_mesh_upload_gpu and
    // tc_texture_upload_gpu route through IRenderDevice::create_shader,
    // create_buffer and create_texture. Returned GL ids are still
    // extracted from the tgfx2 handles and stored in tc_gpu_slot for
    // backward-compat with the legacy TcShader::use / set_uniform_*
    // surface (Stage 7 deletes those callers; Stage 8 removes
    // gpu_ops_impl entirely).
    //
    // Known regression: chronosquad hits a driver-side SIGSEGV inside
    // ShadowPass's draw after the swap. Deferred — Stage 7 first, then
    // root-cause on the settled code.
    tgfx2_interop_set_device(tgfx2_device_.get());
    tgfx2_gpu_ops_register();
}

void RenderEngine::render_to_screen(
    RenderPipeline& pipeline,
    int width,
    int height,
    tc_scene_handle scene,
    const RenderCamera& camera
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("[render_to_screen] pipeline is NULL");
        return;
    }
    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error("[render_to_screen] scene is invalid");
        return;
    }
    std::vector<Light> empty_lights;
    render_view_to_fbo(
        pipeline,
        nullptr,
        width,
        height,
        scene,
        camera,
        "",
        TC_ENTITY_HANDLE_INVALID,
        empty_lights,
        0xFFFFFFFFFFFFFFFFULL
    );
}

void RenderEngine::present_to_screen(
    RenderPipeline& pipeline,
    int width,
    int height,
    const std::string& resource_name
) {
    if (!pipeline.is_valid() || !graphics) {
        tc::Log::warn("[present_to_screen] pipeline=%p graphics=%p", pipeline, graphics);
        return;
    }

    FramebufferHandle* src_fbo = pipeline.fbo_pool().get(resource_name);
    if (!src_fbo) {
        tc::Log::warn("[present_to_screen] FBO '%s' not found in pipeline. Available FBOs:", resource_name.c_str());
        auto& pool = pipeline.fbo_pool();
        for (const auto& key : pool.keys()) {
            auto* fbo = pool.get(key);
            tc::Log::warn("  - '%s': %p", key.c_str(), fbo);
        }
        return;
    }

    graphics->blit_framebuffer(
        src_fbo,
        nullptr,
        0, 0, src_fbo->get_width(), src_fbo->get_height(),
        0, 0, width, height,
        true,
        false
    );
}

void RenderEngine::render_view_to_fbo(
    RenderPipeline& pipeline,
    FramebufferHandle* target_fbo,
    int width,
    int height,
    tc_scene_handle scene,
    const RenderCamera& camera,
    const std::string& viewport_name,
    tc_entity_handle internal_entities,
    const std::vector<Light>& lights,
    uint64_t layer_mask
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_view_to_fbo: pipeline is null");
        return;
    }
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_view_to_fbo: pipeline is invalid");
        return;
    }
    if (!graphics) {
        tc::Log::error("RenderEngine::render_view_to_fbo: graphics is null");
        return;
    }

    tc_frame_graph* fg = tc_pipeline_get_frame_graph(pipeline.handle());
    if (!fg) {
        tc::Log::error("RenderEngine::render_view_to_fbo: failed to get frame graph");
        return;
    }

    if (tc_frame_graph_get_error(fg) != TC_FG_OK) {
        tc::Log::error("RenderEngine::render_view_to_fbo: frame graph error: %s",
                       tc_frame_graph_get_error_message(fg));
        return;
    }

    // Bring up tgfx2 stack BEFORE FBO allocation so FBOPool::ensure can
    // attach persistent tgfx2 wrappers on the very first frame.
    ensure_tgfx2();

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

    FBOMap resources;
    resources["OUTPUT"] = target_fbo;
    resources["DISPLAY"] = target_fbo;

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (strcmp(canon, "OUTPUT") == 0 || strcmp(canon, "DISPLAY") == 0) {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = target_fbo;
            }
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

        if (resource_type != "fbo") {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
            }
            continue;
        }

        int fbo_width = width;
        int fbo_height = height;
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
        auto* tgfx2_gl_dev =
            dynamic_cast<tgfx2::OpenGLRenderDevice*>(tgfx2_device_.get());
        FramebufferHandle* fbo = fbo_pool.ensure(
            graphics, canon, fbo_width, fbo_height, samples, format, filter,
            tgfx2_gl_dev);

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = fbo;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    if (tgfx2_ctx_) {
        tgfx2_ctx_->begin_frame();
    }

    // Pull the persistent tgfx2 wrappers that FBOPool::ensure cached
    // at allocation time. No per-frame wrap/destroy churn — the
    // wrappers live as long as the FBO they reference, so ctx2's
    // fbo_cache entries remain valid across frames.
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_resources;
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_depth_resources;
    if (tgfx2_ctx_ && tgfx2_device_) {
        FBOPool& fbo_pool = pipeline.fbo_pool();
        for (const auto& [name, res] : resources) {
            if (!res) continue;
            auto* fbo = dynamic_cast<FramebufferHandle*>(res);
            if (!fbo) continue;
            tgfx2::TextureHandle color_handle = fbo_pool.get_color_tgfx2(name);
            if (color_handle) tex2_resources[name] = color_handle;
            tgfx2::TextureHandle depth_handle = fbo_pool.get_depth_tgfx2(name);
            if (depth_handle) tex2_depth_resources[name] = depth_handle;
        }
    }

    // Pre-frame clear phase: open a transient ctx2 render pass on each
    // resource that wants clearing. This replaces the legacy
    // graphics->bind_framebuffer + clear_color_depth loop that used to
    // run BEFORE begin_frame and mutated GL state behind ctx2's back.
    if (tgfx2_ctx_) {
        for (const auto& spec : specs) {
            if (spec.resource_type != "fbo" && !spec.resource_type.empty()) {
                continue;
            }
            if (!spec.clear_color && !spec.clear_depth) {
                continue;
            }

            auto it = resources.find(spec.resource);
            if (it == resources.end() || it->second == nullptr) continue;
            auto* fbo = dynamic_cast<FramebufferHandle*>(it->second);
            if (!fbo) continue;

            auto ct = tex2_resources.find(spec.resource);
            auto dt = tex2_depth_resources.find(spec.resource);
            tgfx2::TextureHandle color_tex =
                (ct != tex2_resources.end()) ? ct->second : tgfx2::TextureHandle{};
            tgfx2::TextureHandle depth_tex =
                (dt != tex2_depth_resources.end()) ? dt->second : tgfx2::TextureHandle{};
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

            int fb_w = spec.size ? spec.size->first : width;
            int fb_h = spec.size ? spec.size->second : height;

            tgfx2_ctx_->begin_pass(
                color_tex, depth_tex,
                clear_color_ptr, clear_depth_val, clear_depth_enabled
            );
            tgfx2_ctx_->set_viewport(0, 0, fb_w, fb_h);
            tgfx2_ctx_->end_pass();
        }
    }

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);

        graphics->reset_state();

        const char* reads[16];
        const char* writes[8];
        size_t read_count = tc_pass_get_reads(pass, reads, 16);
        size_t write_count = tc_pass_get_writes(pass, writes, 8);

        FBOMap pass_reads;
        FBOMap pass_writes;
        Tex2Map pass_tex2_reads;
        Tex2Map pass_tex2_writes;
        Tex2Map pass_tex2_depth_reads;
        Tex2Map pass_tex2_depth_writes;

        for (size_t j = 0; j < read_count; j++) {
            auto it = resources.find(reads[j]);
            pass_reads[reads[j]] = (it != resources.end()) ? it->second : nullptr;
            auto t_it = tex2_resources.find(reads[j]);
            if (t_it != tex2_resources.end()) {
                pass_tex2_reads[reads[j]] = t_it->second;
            }
            auto d_it = tex2_depth_resources.find(reads[j]);
            if (d_it != tex2_depth_resources.end()) {
                pass_tex2_depth_reads[reads[j]] = d_it->second;
            }
        }
        for (size_t j = 0; j < write_count; j++) {
            auto it = resources.find(writes[j]);
            pass_writes[writes[j]] = (it != resources.end()) ? it->second : nullptr;
            auto t_it = tex2_resources.find(writes[j]);
            if (t_it != tex2_resources.end()) {
                pass_tex2_writes[writes[j]] = t_it->second;
            }
            auto d_it = tex2_depth_resources.find(writes[j]);
            if (d_it != tex2_depth_resources.end()) {
                pass_tex2_depth_writes[writes[j]] = d_it->second;
            }
        }

        ExecuteContext ctx;
        ctx.graphics = graphics;
        ctx.ctx2 = tgfx2_ctx_.get();
        ctx.reads_fbos = std::move(pass_reads);
        ctx.writes_fbos = std::move(pass_writes);
        ctx.tex2_reads = std::move(pass_tex2_reads);
        ctx.tex2_writes = std::move(pass_tex2_writes);
        ctx.tex2_depth_reads = std::move(pass_tex2_depth_reads);
        ctx.tex2_depth_writes = std::move(pass_tex2_depth_writes);
        ctx.rect = Rect4i{0, 0, width, height};
        ctx.scene = TcSceneRef(scene);
        ctx.viewport_name = viewport_name;
        ctx.internal_entities = internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&camera);
        ctx.lights = lights;
        ctx.layer_mask = layer_mask;

        tc_pass_execute(pass, &ctx);

        tc_profiler_begin_section("Sync Operations");
        tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        if (sync_mode == TC_RENDER_SYNC_FLUSH) {
            graphics->flush();
        } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
            graphics->finish();
        }
        tc_profiler_end_section();

        tc_profiler_end_section();
    }
    tc_profiler_end_section();

    if (tgfx2_ctx_) {
        tgfx2_ctx_->end_frame();
    }

    // Phase 3: wrappers live on FBOPoolEntry now. No per-frame destroy.
    // They persist until the FBO is resized, reformatted, or cleared.
}

void RenderEngine::render_scene_pipeline_offscreen(
    RenderPipeline& pipeline,
    tc_scene_handle scene,
    const std::unordered_map<std::string, ViewportContext>& viewport_contexts,
    const std::vector<Light>& lights,
    const std::string& default_viewport
) {
    if (!pipeline.is_valid()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: pipeline is null");
        return;
    }
    if (!graphics) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: graphics is null");
        return;
    }
    if (viewport_contexts.empty()) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: no viewport contexts");
        return;
    }

    std::string default_vp = default_viewport;
    if (default_vp.empty()) {
        default_vp = viewport_contexts.begin()->first;
    }

    auto default_it = viewport_contexts.find(default_vp);
    if (default_it == viewport_contexts.end()) {
        default_it = viewport_contexts.begin();
    }
    const ViewportContext& default_ctx = default_it->second;

    int default_width = default_ctx.rect.width;
    int default_height = default_ctx.rect.height;

    tc_profiler_begin_section("Get Frame Graph");
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

    // Bring up tgfx2 stack BEFORE FBO allocation so FBOPool::ensure can
    // attach persistent tgfx2 wrappers on the very first frame.
    ensure_tgfx2();

    tc_profiler_begin_section("Collect Specs");
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

    tc_profiler_begin_section("Allocate Resources");
    FBOMap resources;
    resources["OUTPUT"] = default_ctx.output_fbo;
    resources["DISPLAY"] = default_ctx.output_fbo;

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (strcmp(canon, "OUTPUT") == 0 || strcmp(canon, "DISPLAY") == 0) {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = default_ctx.output_fbo;
            }
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

        if (resource_type != "fbo") {
            const char* aliases[64];
            size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
            for (size_t j = 0; j < alias_count; j++) {
                resources[aliases[j]] = nullptr;
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
        auto* tgfx2_gl_dev =
            dynamic_cast<tgfx2::OpenGLRenderDevice*>(tgfx2_device_.get());
        FramebufferHandle* fbo = fbo_pool.ensure(
            graphics, canon, fbo_width, fbo_height, samples, format, filter,
            tgfx2_gl_dev);

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = fbo;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }
    tc_profiler_end_section();

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    if (tgfx2_ctx_) {
        tgfx2_ctx_->begin_frame();
    }

    // Pull persistent tgfx2 wrappers from FBOPool for every resource
    // the pipeline exposes. Mirrors render_view_to_fbo so ctx.tex2_*
    // maps match for Skybox/Bloom/Tonemap/etc. in this offscreen path.
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_resources;
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_depth_resources;
    if (tgfx2_ctx_ && tgfx2_device_) {
        FBOPool& fbo_pool = pipeline.fbo_pool();
        for (const auto& [name, res] : resources) {
            if (!res) continue;
            auto* fbo = dynamic_cast<FramebufferHandle*>(res);
            if (!fbo) continue;
            tgfx2::TextureHandle color_handle = fbo_pool.get_color_tgfx2(name);
            if (color_handle) tex2_resources[name] = color_handle;
            tgfx2::TextureHandle depth_handle = fbo_pool.get_depth_tgfx2(name);
            if (depth_handle) tex2_depth_resources[name] = depth_handle;
        }
    }

    // Pre-frame clear phase via transient ctx2 passes. Replaces legacy
    // graphics->bind_framebuffer + clear_color_depth loop — see
    // render_view_to_fbo for the rationale.
    tc_profiler_begin_section("Clear Resources");
    if (tgfx2_ctx_) {
        for (const auto& spec : specs) {
            if (spec.resource_type != "fbo" && !spec.resource_type.empty()) {
                continue;
            }
            if (!spec.clear_color && !spec.clear_depth) {
                continue;
            }

            auto it = resources.find(spec.resource);
            if (it == resources.end() || it->second == nullptr) continue;
            auto* fbo = dynamic_cast<FramebufferHandle*>(it->second);
            if (!fbo) continue;

            auto ct = tex2_resources.find(spec.resource);
            auto dt = tex2_depth_resources.find(spec.resource);
            tgfx2::TextureHandle color_tex =
                (ct != tex2_resources.end()) ? ct->second : tgfx2::TextureHandle{};
            tgfx2::TextureHandle depth_tex =
                (dt != tex2_depth_resources.end()) ? dt->second : tgfx2::TextureHandle{};
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

            tgfx2_ctx_->begin_pass(
                color_tex, depth_tex,
                clear_color_ptr, clear_depth_val, clear_depth_enabled
            );
            tgfx2_ctx_->set_viewport(0, 0, fb_w, fb_h);
            tgfx2_ctx_->end_pass();
        }
    }
    tc_profiler_end_section();

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);

        graphics->reset_state();

        std::string pass_viewport_name = default_vp;
        if (pass->viewport_name && pass->viewport_name[0] != '\0') {
            pass_viewport_name = pass->viewport_name;
        }

        auto vp_it = viewport_contexts.find(pass_viewport_name);
        if (vp_it == viewport_contexts.end()) {
            vp_it = default_it;
        }
        const ViewportContext& vp_ctx = vp_it->second;

        const char* reads[16];
        const char* writes[8];
        size_t read_count = tc_pass_get_reads(pass, reads, 16);
        size_t write_count = tc_pass_get_writes(pass, writes, 8);

        FBOMap pass_reads;
        FBOMap pass_writes;
        Tex2Map pass_tex2_reads;
        Tex2Map pass_tex2_writes;
        Tex2Map pass_tex2_depth_reads;
        Tex2Map pass_tex2_depth_writes;

        for (size_t j = 0; j < read_count; j++) {
            auto it = resources.find(reads[j]);
            FrameGraphResource* res = (it != resources.end()) ? it->second : nullptr;
            pass_reads[reads[j]] = res;
            auto t_it = tex2_resources.find(reads[j]);
            if (t_it != tex2_resources.end()) {
                pass_tex2_reads[reads[j]] = t_it->second;
            }
            auto d_it = tex2_depth_resources.find(reads[j]);
            if (d_it != tex2_depth_resources.end()) {
                pass_tex2_depth_reads[reads[j]] = d_it->second;
            }
        }

        for (size_t j = 0; j < write_count; j++) {
            const char* write_name = writes[j];
            if (strcmp(write_name, "OUTPUT") == 0 || strcmp(write_name, "DISPLAY") == 0) {
                pass_writes[write_name] = vp_ctx.output_fbo;
            } else {
                auto it = resources.find(write_name);
                FrameGraphResource* res = (it != resources.end()) ? it->second : nullptr;
                pass_writes[write_name] = res;
                auto t_it = tex2_resources.find(write_name);
                if (t_it != tex2_resources.end()) {
                    pass_tex2_writes[write_name] = t_it->second;
                }
                auto d_it = tex2_depth_resources.find(write_name);
                if (d_it != tex2_depth_resources.end()) {
                    pass_tex2_depth_writes[write_name] = d_it->second;
                }
            }
        }

        ExecuteContext ctx;
        ctx.graphics = graphics;
        ctx.ctx2 = tgfx2_ctx_.get();
        ctx.reads_fbos = std::move(pass_reads);
        ctx.writes_fbos = std::move(pass_writes);
        ctx.tex2_reads = std::move(pass_tex2_reads);
        ctx.tex2_writes = std::move(pass_tex2_writes);
        ctx.tex2_depth_reads = std::move(pass_tex2_depth_reads);
        ctx.tex2_depth_writes = std::move(pass_tex2_depth_writes);
        ctx.rect = vp_ctx.rect;
        ctx.scene = TcSceneRef(scene);
        ctx.viewport_name = vp_ctx.name;
        ctx.internal_entities = vp_ctx.internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&vp_ctx.camera);
        ctx.lights = lights;
        ctx.layer_mask = vp_ctx.layer_mask;

        tc_pass_execute(pass, &ctx);

        tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
        if (sync_mode == TC_RENDER_SYNC_FLUSH) {
            graphics->flush();
        } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
            graphics->finish();
        }

        tc_profiler_end_section();
    }
    tc_profiler_end_section();

    if (tgfx2_ctx_) {
        tgfx2_ctx_->end_frame();
    }
}

} // namespace termin
