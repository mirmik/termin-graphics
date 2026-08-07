#include <termin/render/scene_render_execution.hpp>

#include <tcbase/tc_log.hpp>
#include <termin/render/render_scene_item_collector.hpp>
#include <termin/render/scene_render_services.hpp>

namespace termin {

void render_scene_pipeline_offscreen(
    RenderEngine& engine,
    RenderPipeline& pipeline,
    tc_scene_handle scene,
    const std::unordered_map<std::string, RenderTargetContext>& render_target_contexts,
    const SceneInternalEntityMap& internal_entities,
    const std::vector<Light>& lights,
    const std::string& default_render_target,
    const std::vector<FrameGraphCaptureRequest*>& debug_capture_requests)
{
    if (!tc_scene_handle_valid(scene)) {
        tc::Log::error(
            "render_scene_pipeline_offscreen: invalid scene handle");
        return;
    }
    if (render_target_contexts.empty()) {
        tc::Log::error(
            "render_scene_pipeline_offscreen: no render target contexts");
        return;
    }

    const size_t target_count = render_target_contexts.size();
    std::vector<RenderItemSnapshot> snapshots(target_count);
    std::vector<SceneRenderServices> scene_services;
    std::vector<RenderExecutionCapabilities> capabilities;
    scene_services.reserve(target_count);
    capabilities.reserve(target_count);

    RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.targets.reserve(target_count);
    execution.default_render_target = default_render_target;
    execution.debug_capture_requests = debug_capture_requests;

    size_t target_index = 0;
    for (const auto& [name, target] : render_target_contexts) {
        tc_entity_handle internal = TC_ENTITY_HANDLE_INVALID;
        const auto internal_it = internal_entities.find(name);
        if (internal_it != internal_entities.end()) {
            internal = internal_it->second;
        }

        scene_services.emplace_back(
            TcSceneRef(scene),
            internal,
            lights,
            target.layer_mask,
            target.render_category_mask);

        RenderSceneItemCollectRequest collect_request{};
        collect_request.scene = scene;
        collect_request.layer_mask = target.layer_mask;
        collect_request.render_category_mask = target.render_category_mask;
        collect_request.debug_pass_name = "render_scene_pipeline_offscreen";
        collect_request.camera = target.view.primary_view();
        collect_request.scene_context = &scene_services.back().scene;
        if (!collect_scene_render_item_snapshot(
                snapshots[target_index], collect_request)) {
            tc::Log::error(
                "render_scene_pipeline_offscreen: failed to publish RenderItem snapshot for target '%s'",
                name.c_str());
            return;
        }

        capabilities.emplace_back();
        capabilities.back().add(scene_services.back());
        execution.targets.emplace(name, RenderExecutionTarget{
            .context = &target,
            .render_items = &snapshots[target_index],
            .capabilities = &capabilities.back(),
        });
        ++target_index;
    }

    engine.execute_pipeline(execution);
}

} // namespace termin
