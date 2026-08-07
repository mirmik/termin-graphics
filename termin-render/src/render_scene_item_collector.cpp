#include <termin/render/render_scene_item_collector.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/scene_render_services.hpp>

#include <tcbase/tc_log.hpp>
#include <cstring>

namespace termin {
namespace {

const char* safe_pass_name(const RenderSceneItemCollectRequest& request)
{
    return request.debug_pass_name ? request.debug_pass_name : "RenderSceneItemCollector";
}

struct CollectCallbackData {
    RenderItemCollection* output = nullptr;
    const RenderSceneItemCollectRequest* request = nullptr;
    bool ok = true;
    uint64_t producer_count = 0;
};

bool collect_drawable_items_callback(tc_component* component, void* user_data)
{
    auto* data = static_cast<CollectCallbackData*>(user_data);
    if (!data || !data->output || !data->request) {
        tc::Log::error("[RenderSceneItemCollector] invalid scene callback state");
        return true;
    }
    if (!component) {
        tc::Log::error(
            "[%s] scene drawable iteration returned null component",
            safe_pass_name(*data->request));
        data->ok = false;
        return true;
    }

    if (data->request->phase != TC_PHASE_NONE &&
        !tc_phase_mask_contains(
            tc_component_phase_mask(component), data->request->phase)) {
        return true;
    }

    tc_render_item_collect_context context{};
    context.phase = data->request->phase;
    context.flags = data->request->flags;
    context.layer_mask = data->request->layer_mask;
    context.render_category_mask = data->request->render_category_mask;
    context.debug_pass_name = data->request->debug_pass_name;
    context.pass_contract = data->request->pass_contract;
    context.scene = data->request->scene_context;
    context.camera = data->request->camera;
    context.user_context = data->request->user_context;

    if (!collect_drawable_render_items(component, context, *data->output)) {
        data->ok = false;
    }
    data->producer_count += 1;
    return true;
}

} // namespace

void RenderSceneItemCollector::clear_keep_capacity()
{
    storage_.clear();
}

bool RenderSceneItemCollector::collect(const RenderSceneItemCollectRequest& request)
{
    return collect_into(request, storage_);
}

bool RenderSceneItemCollector::collect_into(
    const RenderSceneItemCollectRequest& request,
    RenderItemCollection& output)
{
    output.clear();
    last_scene_traversals_ = 0;
    last_drawable_producers_ = 0;

    if (!tc_scene_handle_valid(request.scene)) {
        tc::Log::error(
            "[%s] cannot collect scene RenderItems: scene is invalid",
            safe_pass_name(request));
        return false;
    }
    CollectCallbackData data;
    data.output = &output;
    data.request = &request;

    tc_scene_foreach_drawable(
        request.scene,
        collect_drawable_items_callback,
        &data,
        request.scene_filter_flags,
        request.layer_mask);
    last_scene_traversals_ = 1;
    last_drawable_producers_ = data.producer_count;
    return data.ok;
}

bool collect_scene_render_item_snapshot(
    RenderItemSnapshot& snapshot,
    const RenderSceneItemCollectRequest& request)
{
    RenderSceneItemCollectRequest snapshot_request = request;
    snapshot_request.phase = TC_PHASE_NONE;
    snapshot_request.flags |= TC_RENDER_ITEM_COLLECT_FLAG_ALLOW_MISSING_MATERIAL_PHASE;
    RenderSceneItemCollector collector;
    RenderItemCollection& output = snapshot.begin_collection();
    if (!collector.collect_into(snapshot_request, output)) {
        snapshot.invalidate_keep_capacity();
        return false;
    }
    RenderItemSnapshotCounters counters{};
    counters.source_traversals = collector.last_scene_traversals();
    counters.producers = collector.last_drawable_producers();
    counters.emitted_items = output.items.size();
    snapshot.finish_collection(counters);
    return true;
}

tc_component* render_scene_item_component(const tc_render_item& item)
{
    if (item.source.domain_id != TC_RENDER_ITEM_SOURCE_DOMAIN_SCENE) {
        return nullptr;
    }
    return reinterpret_cast<tc_component*>(item.source.adapter_data);
}

RenderItemSnapshot* ensure_render_item_snapshot(
    ExecuteContext& context,
    const char* debug_pass_name)
{
    if (!context.render_item_snapshot) {
        tc::Log::error("[%s] render execution has no RenderItem snapshot storage",
                       debug_pass_name ? debug_pass_name : "RenderItemSnapshot");
        return nullptr;
    }
    if (context.render_item_snapshot->valid()) {
        return context.render_item_snapshot;
    }

    const SceneRenderServices* services = require_scene_render_services(
        context, debug_pass_name ? debug_pass_name : "RenderItemSnapshot");
    if (!services) {
        return nullptr;
    }

    RenderSceneItemCollectRequest request{};
    request.scene = services->scene.handle();
    request.layer_mask = services->layer_mask;
    request.render_category_mask = services->render_category_mask;
    request.debug_pass_name = debug_pass_name;
    request.camera = context.view.primary_view();
    request.scene_context = &services->scene;
    if (!collect_scene_render_item_snapshot(*context.render_item_snapshot, request)) {
        tc::Log::error("[%s] failed to build scene RenderItem snapshot",
                       debug_pass_name ? debug_pass_name : "RenderItemSnapshot");
        return nullptr;
    }
    return context.render_item_snapshot;
}

} // namespace termin
