// render_engine.hpp - Core render engine for executing pipeline passes
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx/graphics_backend.hpp"
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
namespace tgfx2 {
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
    FramebufferHandle* output_fbo = nullptr;
};

class RENDER_API RenderEngine {
public:
    GraphicsBackend* graphics = nullptr;

private:
    // tgfx2 stack — lazily constructed on first render_view_to_fbo when
    // the active GL backend is available. Used to populate
    // ExecuteContext::ctx2 so Phase 2 passes can draw through the
    // pipeline+command-buffer API while legacy passes keep using `graphics`.
    std::unique_ptr<tgfx2::IRenderDevice> tgfx2_device_;
    std::unique_ptr<tgfx2::PipelineCache> tgfx2_cache_;
    std::unique_ptr<tgfx2::RenderContext2> tgfx2_ctx_;

    void ensure_tgfx2();

public:
    RenderEngine();
    explicit RenderEngine(GraphicsBackend* graphics);
    ~RenderEngine();

    void render_view_to_fbo(
        RenderPipeline& pipeline,
        FramebufferHandle* target_fbo,
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
