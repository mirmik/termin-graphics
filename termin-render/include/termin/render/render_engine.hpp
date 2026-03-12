#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx/graphics_backend.hpp"
#include "termin/render/frame_pass.hpp"
#include "termin/render/execute_context.hpp"
#include "termin/render/render_camera.hpp"
#include "termin/render/render_pipeline.hpp"
#include <termin/render/light.hpp>
#include "termin/lighting/shadow.hpp"
#include <termin/tc_scene.hpp>

extern "C" {
#include "render/tc_frame_graph.h"
#include "core/tc_scene.h"
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

class RenderEngine {
public:
    GraphicsBackend* graphics = nullptr;

public:
    RenderEngine() = default;
    explicit RenderEngine(GraphicsBackend* graphics);

    void render_view_to_fbo(
        RenderPipeline* pipeline,
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
        RenderPipeline* pipeline,
        int width,
        int height,
        tc_scene_handle scene,
        const RenderCamera& camera
    );

    void present_to_screen(
        RenderPipeline* pipeline,
        int width,
        int height,
        const std::string& resource_name = "color"
    );

    void render_scene_pipeline_offscreen(
        RenderPipeline* pipeline,
        tc_scene_handle scene,
        const std::unordered_map<std::string, ViewportContext>& viewport_contexts,
        const std::vector<Light>& lights,
        const std::string& default_viewport = ""
    );
};

} // namespace termin
