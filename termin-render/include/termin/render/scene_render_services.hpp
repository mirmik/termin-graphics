#pragma once

#include <cstdint>
#include <span>

#include <termin/lighting/light.hpp>
#include <termin/render/render_execution_capabilities.hpp>
#include <termin/render/render_export.hpp>
#include <termin/tc_scene.hpp>

extern "C" {
#include <core/tc_entity_pool.h>
}

namespace termin {

    struct ExecuteContext;

    // Capabilities supplied only by the tc_scene adapter. The service borrows
    // frame-local light storage and is valid for one complete pipeline execution.
    struct SceneRenderServices final : RenderExecutionCapability {
        TcSceneRef scene;
        tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
        std::span<const Light> lights;
        uint64_t layer_mask = UINT64_MAX;
        uint64_t render_category_mask = UINT64_MAX;

        SceneRenderServices() = default;
        explicit SceneRenderServices(TcSceneRef scene_value)
            : scene(scene_value) {}
        SceneRenderServices(TcSceneRef scene_value,
                            tc_entity_handle internal_entities_value,
                            std::span<const Light> lights_value,
                            uint64_t layer_mask_value,
                            uint64_t render_category_mask_value)
            : scene(scene_value),
              internal_entities(internal_entities_value),
              lights(lights_value),
              layer_mask(layer_mask_value),
              render_category_mask(render_category_mask_value) {}
    };

    // Scene-only passes use this gate instead of silently assuming that every
    // ExecuteContext originated from a tc_scene render.
    RENDER_API const SceneRenderServices* require_scene_render_services(const ExecuteContext& context,
                                                                        const char* consumer);

} // namespace termin
