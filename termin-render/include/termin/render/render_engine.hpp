// render_engine.hpp - Core render engine for executing pipeline passes
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <tgfx2/handles.hpp>
#include "termin/render/frame_pass.hpp"
#include "termin/render/execute_context.hpp"
#include "termin/render/render_camera.hpp"
#include "termin/render/render_pipeline.hpp"
#include "termin/render/render_export.hpp"
#include <termin/render/light.hpp>
#include "termin/lighting/shadow.hpp"
#include <termin/tc_scene.hpp>

extern "C" {
#include "render/tc_frame_graph.h"
#include "core/tc_scene.h"
}

// tgfx2 forward declarations — RenderEngine lazily owns an OpenGL device,
// pipeline cache, and mid-level RenderContext2 that migrated passes use via
// ExecuteContext::ctx2 (see Phase 2 of tgfx2 migration).
namespace tgfx {
class IRenderDevice;
class PipelineCache;
class RenderContext2;
}

namespace termin {

struct ViewportContext {
public:
    std::string name;
    RenderCamera camera;
    Rect4i rect{0, 0, 0, 0};
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;

    // Final render target — native tgfx2 color + depth textures owned
    // by the caller (typically ViewportRenderState). Passes in the
    // pipeline receive these through ExecuteContext::tex2_writes under
    // the OUTPUT/DISPLAY alias.
    tgfx::TextureHandle output_color_tex;
    tgfx::TextureHandle output_depth_tex;
};

class RENDER_API RenderEngine {
private:
    // tgfx2 stack — lazily constructed on first render call. All
    // rendering now goes through this stack; the legacy graphics
    // backend is no longer involved.
    //
    // `tgfx2_device_` may be either engine-owned (we called
    // create_device) or borrowed from the application host
    // (Tgfx2Context.from_window already installed an interop device
    // before we ever ran). `tgfx2_device_owned_` tells the destructor
    // which one it is — must not delete a host-owned device. This is
    // the process-wide-single-device invariant: host owns the one
    // IRenderDevice and every renderer points at it.
    std::unique_ptr<tgfx::IRenderDevice> tgfx2_device_;
    bool tgfx2_device_owned_ = true;
    std::unique_ptr<tgfx::PipelineCache> tgfx2_cache_;
    std::unique_ptr<tgfx::RenderContext2> tgfx2_ctx_;

    // Reusable offscreen color+depth textures backing
    // render_view_to_fbo_id(). Resized on demand, destroyed with the
    // engine.
    tgfx::TextureHandle external_target_color_;
    tgfx::TextureHandle external_target_depth_;
    int external_target_w_ = 0;
    int external_target_h_ = 0;

public:
    void ensure_tgfx2();

    // Access the engine's tgfx2 render context. May return nullptr if
    // ensure_tgfx2() has not yet been called or TERMIN_DISABLE_TGFX2 is
    // set. The returned pointer remains owned by the RenderEngine.
    tgfx::RenderContext2* tgfx2_ctx() { return tgfx2_ctx_.get(); }

    // Access the tgfx2 render device that owns all texture/buffer
    // handles used by the engine. Lifetime-tied to the RenderEngine.
    tgfx::IRenderDevice* tgfx2_device() { return tgfx2_device_.get(); }

public:
    RenderEngine();
    ~RenderEngine();

    // Native tgfx2 renderer — renders `pipeline`
    // for `scene` with `camera` into an internal offscreen color+depth
    // texture pair (created lazily / resized on demand), then blits the
    // color texture into the given external GL framebuffer id. Clients
    // that only own a raw GL fbo id (SDL window backbuffer = 0, Qt
    // QOpenGLWidget fbo id, streaming server's offscreen) use this to
    // drive a full render without touching the legacy GraphicsBackend /
    // FramebufferHandle plumbing.
    void render_view_to_fbo_id(
        RenderPipeline& pipeline,
        uint32_t target_fbo_id,
        int width,
        int height,
        tc_scene_handle scene,
        const RenderCamera& camera,
        const std::string& viewport_name,
        tc_entity_handle internal_entities,
        const std::vector<Light>& lights,
        uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL
    );

    void render_to_screen(
        RenderPipeline& pipeline,
        int width,
        int height,
        tc_scene_handle scene,
        const RenderCamera& camera
    );

    void present_to_screen(
        RenderPipeline& pipeline,
        int width,
        int height,
        const std::string& resource_name = "color"
    );

    void render_scene_pipeline_offscreen(
        RenderPipeline& pipeline,
        tc_scene_handle scene,
        const std::unordered_map<std::string, ViewportContext>& viewport_contexts,
        const std::vector<Light>& lights,
        const std::string& default_viewport = ""
    );
};

} // namespace termin
