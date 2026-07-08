#include <termin/render/render_scene_item_collector.hpp>

#include <tcbase/tc_log.hpp>

namespace termin {
namespace {

const char* safe_pass_name(const RenderSceneItemCollectRequest& request)
{
    return request.debug_pass_name ? request.debug_pass_name : "RenderSceneItemCollector";
}

struct CollectCallbackData {
    RenderSceneItemCollector* collector = nullptr;
    const RenderSceneItemCollectRequest* request = nullptr;
    bool ok = true;
};

bool collect_drawable_items_callback(tc_component* component, void* user_data)
{
    auto* data = static_cast<CollectCallbackData*>(user_data);
    if (!data || !data->collector || !data->request) {
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

    tc_render_item_collect_context context{};
    context.phase_mark = data->request->phase_mark;
    context.flags = data->request->flags;
    context.layer_mask = data->request->layer_mask;
    context.render_category_mask = data->request->render_category_mask;
    context.debug_pass_name = data->request->debug_pass_name;
    context.pass_contract = data->request->pass_contract;
    context.scene = data->request->scene_context;
    context.camera = data->request->camera;
    context.user_context = data->request->user_context;

    if (!collect_drawable_render_items(component, context, data->collector->storage())) {
        data->ok = false;
    }
    return true;
}

} // namespace

void RenderSceneItemCollector::clear_keep_capacity()
{
    storage_.clear();
}

bool RenderSceneItemCollector::collect(const RenderSceneItemCollectRequest& request)
{
    clear_keep_capacity();

    if (!tc_scene_handle_valid(request.scene)) {
        tc::Log::error(
            "[%s] cannot collect scene RenderItems: scene is invalid",
            safe_pass_name(request));
        return false;
    }
    if (!request.phase_mark || request.phase_mark[0] == '\0') {
        tc::Log::error(
            "[%s] cannot collect scene RenderItems: phase_mark is empty",
            safe_pass_name(request));
        return false;
    }

    CollectCallbackData data;
    data.collector = this;
    data.request = &request;

    tc_scene_foreach_drawable(
        request.scene,
        collect_drawable_items_callback,
        &data,
        request.scene_filter_flags,
        request.layer_mask);
    return data.ok;
}

} // namespace termin
