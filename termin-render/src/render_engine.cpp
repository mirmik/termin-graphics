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

// Out-of-line destructor so unique_ptr<tgfx2::*> members can use forward
// declarations in the header; the full tgfx2 types are visible here.
RenderEngine::~RenderEngine() {
    if (tgfx2_device_) {
        if (external_target_color_) tgfx2_device_->destroy(external_target_color_);
        if (external_target_depth_) tgfx2_device_->destroy(external_target_depth_);
    }
}

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
    render_view_to_fbo_id(
        pipeline,
        0,
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
    RenderPipeline& /*pipeline*/,
    int /*width*/,
    int /*height*/,
    const std::string& /*resource_name*/
) {
    // Deprecated after Stage 8.3: FBOPool no longer owns legacy
    // FramebufferHandle objects, so there's no `FramebufferHandle*`
    // to pass to `graphics->blit_framebuffer`. No production C++ /
    // Python code calls this; only a SWIG export for C#. Kept as
    // a warning stub so the binding surface stays stable until the
    // C# path is migrated to `PresentToScreenPass`.
    static bool warned = false;
    if (!warned) {
        warned = true;
        tc::Log::warn("[RenderEngine::present_to_screen] deprecated no-op; "
                      "use PresentToScreenPass in the framegraph instead.");
    }
}

void RenderEngine::render_view_to_fbo_id(
    RenderPipeline& pipeline,
    uint32_t target_fbo_id,
    int width,
    int height,
    tc_scene_handle scene,
    const RenderCamera& camera,
    const std::string& viewport_name,
    tc_entity_handle internal_entities,
    const std::vector<Light>& lights,
    uint64_t layer_mask
) {
    if (width <= 0 || height <= 0) return;
    ensure_tgfx2();
    if (!tgfx2_device_) {
        tc::Log::error("RenderEngine::render_view_to_fbo_id: tgfx2 device unavailable");
        return;
    }

    // Resize internal color+depth attachments on demand.
    if (!external_target_color_ ||
        external_target_w_ != width || external_target_h_ != height) {
        if (external_target_color_) {
            tgfx2_device_->destroy(external_target_color_);
            external_target_color_ = {};
        }
        if (external_target_depth_) {
            tgfx2_device_->destroy(external_target_depth_);
            external_target_depth_ = {};
        }
        tgfx2::TextureDesc color_desc;
        color_desc.width = static_cast<uint32_t>(width);
        color_desc.height = static_cast<uint32_t>(height);
        color_desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
        color_desc.usage = tgfx2::TextureUsage::Sampled |
                           tgfx2::TextureUsage::ColorAttachment |
                           tgfx2::TextureUsage::CopyDst;
        external_target_color_ = tgfx2_device_->create_texture(color_desc);

        tgfx2::TextureDesc depth_desc;
        depth_desc.width = static_cast<uint32_t>(width);
        depth_desc.height = static_cast<uint32_t>(height);
        depth_desc.format = tgfx2::PixelFormat::D24_UNorm;
        depth_desc.usage = tgfx2::TextureUsage::DepthStencilAttachment |
                           tgfx2::TextureUsage::Sampled;
        external_target_depth_ = tgfx2_device_->create_texture(depth_desc);

        external_target_w_ = width;
        external_target_h_ = height;
    }

    // Render pipeline into the internal color+depth textures via the
    // single-viewport offscreen path.
    std::unordered_map<std::string, ViewportContext> contexts;
    ViewportContext ctx;
    ctx.name = viewport_name;
    ctx.camera = camera;
    ctx.rect = {0, 0, width, height};
    ctx.internal_entities = internal_entities;
    ctx.layer_mask = layer_mask;
    ctx.output_color_tex = external_target_color_;
    ctx.output_depth_tex = external_target_depth_;
    contexts[viewport_name] = std::move(ctx);
    render_scene_pipeline_offscreen(
        pipeline, scene, contexts, lights, viewport_name
    );

    // Present: blit internal color → external GL framebuffer id.
    auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(tgfx2_device_.get());
    if (gl_dev) {
        gl_dev->blit_to_external_fbo(
            target_fbo_id, external_target_color_,
            0, 0, width, height,
            0, 0, width, height
        );
    }
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
    ensure_tgfx2();
    if (!tgfx2_device_) {
        tc::Log::error("RenderEngine::render_scene_pipeline_offscreen: tgfx2 device unavailable");
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
    // OUTPUT/DISPLAY no longer travel through the FBOMap — they're
    // native tgfx2 textures owned by the caller (ViewportRenderState)
    // and plumbed straight into tex2_writes below.

    const char* canonical_names[256];
    size_t canon_count = tc_frame_graph_get_canonical_resources(fg, canonical_names, 256);

    for (size_t i = 0; i < canon_count; i++) {
        const char* canon = canonical_names[i];

        if (strcmp(canon, "OUTPUT") == 0 || strcmp(canon, "DISPLAY") == 0) {
            // OUTPUT/DISPLAY are viewport-owned native textures
            // (vp_ctx.output_color_tex / output_depth_tex). Skip the
            // FBOMap allocation — passes receive them directly.
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

        tgfx2::PixelFormat color_fmt = tgfx2::PixelFormat::RGBA8_UNorm;
        if (format == "r8") color_fmt = tgfx2::PixelFormat::R8_UNorm;
        else if (format == "r16f") color_fmt = tgfx2::PixelFormat::R16F;
        else if (format == "r32f") color_fmt = tgfx2::PixelFormat::R32F;
        else if (format == "rgba16f") color_fmt = tgfx2::PixelFormat::RGBA16F;
        else if (format == "rgba32f") color_fmt = tgfx2::PixelFormat::RGBA32F;

        fbo_pool.ensure_native(
            *tgfx2_device_, canon, fbo_width, fbo_height,
            color_fmt, /*has_depth=*/true, tgfx2::PixelFormat::D32F, samples);
        (void)filter;

        const char* aliases[64];
        size_t alias_count = tc_frame_graph_get_alias_group(fg, canon, aliases, 64);
        for (size_t j = 0; j < alias_count; j++) {
            resources[aliases[j]] = nullptr;
            fbo_pool.add_alias(aliases[j], canon);
        }
    }
    tc_profiler_end_section();

    size_t schedule_count = tc_frame_graph_schedule_count(fg);

    if (tgfx2_ctx_) {
        tgfx2_ctx_->begin_frame();
    }

    // Assemble per-resource tgfx2 texture maps from the pool. Native
    // path: handles are owned by IRenderDevice, persistent across
    // frames without any wrap/destroy churn.
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_resources;
    std::unordered_map<std::string, tgfx2::TextureHandle> tex2_depth_resources;
    if (tgfx2_ctx_ && tgfx2_device_) {
        FBOPool& fbo_pool = pipeline.fbo_pool();
        for (const auto& [name, res] : resources) {
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

    // tgfx2 OpenGL device: per-pass state reset + GL sync.
    auto* execute_tgfx2_gl_dev =
        dynamic_cast<tgfx2::OpenGLRenderDevice*>(tgfx2_device_.get());

    tc_profiler_begin_section("Execute Passes");
    for (size_t i = 0; i < schedule_count; i++) {
        tc_pass* pass = tc_frame_graph_schedule_at(fg, i);
        if (!pass || !pass->enabled || pass->passthrough) {
            continue;
        }

        const char* pass_name = pass->pass_name ? pass->pass_name : "UnnamedPass";
        tc_profiler_begin_section(pass_name);

        if (execute_tgfx2_gl_dev) execute_tgfx2_gl_dev->reset_state();

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

        Tex2Map pass_tex2_reads;
        Tex2Map pass_tex2_writes;
        Tex2Map pass_tex2_depth_reads;
        Tex2Map pass_tex2_depth_writes;
        ShadowArrayMap pass_shadow_arrays;

        auto collect_shadow_array = [&](const char* name) {
            auto it = resources.find(name);
            if (it != resources.end() && it->second) {
                auto* arr = dynamic_cast<ShadowMapArrayResource*>(it->second);
                if (arr) pass_shadow_arrays[name] = arr;
            }
        };

        for (size_t j = 0; j < read_count; j++) {
            const char* read_name = reads[j];
            if (strcmp(read_name, "OUTPUT") == 0 || strcmp(read_name, "DISPLAY") == 0) {
                if (vp_ctx.output_color_tex) {
                    pass_tex2_reads[read_name] = vp_ctx.output_color_tex;
                }
                if (vp_ctx.output_depth_tex) {
                    pass_tex2_depth_reads[read_name] = vp_ctx.output_depth_tex;
                }
                continue;
            }
            collect_shadow_array(read_name);
            auto t_it = tex2_resources.find(read_name);
            if (t_it != tex2_resources.end()) {
                pass_tex2_reads[read_name] = t_it->second;
            }
            auto d_it = tex2_depth_resources.find(read_name);
            if (d_it != tex2_depth_resources.end()) {
                pass_tex2_depth_reads[read_name] = d_it->second;
            }
        }

        for (size_t j = 0; j < write_count; j++) {
            const char* write_name = writes[j];
            if (strcmp(write_name, "OUTPUT") == 0 || strcmp(write_name, "DISPLAY") == 0) {
                // OUTPUT/DISPLAY come straight from ViewportContext's
                // owned native textures — no FBO wrap needed anymore.
                if (vp_ctx.output_color_tex) {
                    pass_tex2_writes[write_name] = vp_ctx.output_color_tex;
                }
                if (vp_ctx.output_depth_tex) {
                    pass_tex2_depth_writes[write_name] = vp_ctx.output_depth_tex;
                }
            } else {
                collect_shadow_array(write_name);
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
        ctx.ctx2 = tgfx2_ctx_.get();
        ctx.tex2_reads = std::move(pass_tex2_reads);
        ctx.tex2_writes = std::move(pass_tex2_writes);
        ctx.tex2_depth_reads = std::move(pass_tex2_depth_reads);
        ctx.tex2_depth_writes = std::move(pass_tex2_depth_writes);
        ctx.shadow_arrays = std::move(pass_shadow_arrays);
        ctx.rect = vp_ctx.rect;
        ctx.scene = TcSceneRef(scene);
        ctx.viewport_name = vp_ctx.name;
        ctx.internal_entities = vp_ctx.internal_entities;
        ctx.camera = const_cast<RenderCamera*>(&vp_ctx.camera);
        ctx.lights = lights;
        ctx.layer_mask = vp_ctx.layer_mask;

        tc_pass_execute(pass, &ctx);

        if (execute_tgfx2_gl_dev) {
            tc_render_sync_mode sync_mode = tc_project_settings_get_render_sync_mode();
            if (sync_mode == TC_RENDER_SYNC_FLUSH) {
                execute_tgfx2_gl_dev->flush();
            } else if (sync_mode == TC_RENDER_SYNC_FINISH) {
                execute_tgfx2_gl_dev->finish();
            }
        }

        tc_profiler_end_section();
    }
    tc_profiler_end_section();

    if (tgfx2_ctx_) {
        tgfx2_ctx_->end_frame();
    }
}

} // namespace termin
