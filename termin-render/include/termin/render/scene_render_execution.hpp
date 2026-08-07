#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <termin/lighting/light.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_entity_pool.h>
#include <core/tc_scene_pool.h>
}

namespace termin {

using SceneInternalEntityMap =
    std::unordered_map<std::string, tc_entity_handle>;

// tc_scene adapter for the scene-neutral RenderEngine::execute_pipeline().
// It publishes one immutable item snapshot and one typed capability bundle per
// target, then keeps those borrowed values alive for the complete execution.
RENDER_API void render_scene_pipeline_offscreen(
    RenderEngine& engine,
    RenderPipeline& pipeline,
    tc_scene_handle scene,
    const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts,
    const SceneInternalEntityMap& internal_entities,
    const std::vector<Light>& lights,
    const std::string& default_render_target = "",
    const std::vector<FrameGraphCaptureRequest*>& debug_capture_requests = {});

} // namespace termin
