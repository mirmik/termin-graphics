#include <termin/render/render_scene_item_collector.hpp>
#include <termin/render/execute_context.hpp>

#include <tcbase/tc_log.hpp>
#include <cstring>

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
    uint64_t producer_count = 0;
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

    if (!collect_drawable_render_items(component, context, data->collector->storage())) {
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
    clear_keep_capacity();
    last_scene_traversals_ = 0;
    last_drawable_producers_ = 0;

    if (!tc_scene_handle_valid(request.scene)) {
        tc::Log::error(
            "[%s] cannot collect scene RenderItems: scene is invalid",
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
    last_scene_traversals_ = 1;
    last_drawable_producers_ = data.producer_count;
    return data.ok;
}

bool RenderSceneItemSnapshot::collect(const RenderSceneItemCollectRequest& request)
{
    RenderSceneItemCollectRequest snapshot_request = request;
    snapshot_request.phase = TC_PHASE_NONE;
    snapshot_request.flags |= TC_RENDER_ITEM_COLLECT_FLAG_ALLOW_MISSING_MATERIAL_PHASE;
    valid_ = collector_.collect(snapshot_request);
    counters_.scene_traversals = collector_.last_scene_traversals();
    counters_.emitted_items = collector_.item_count();
    counters_.drawable_producers = collector_.last_drawable_producers();
    for (RenderSceneItemPhaseBucket& bucket : phase_buckets_) {
        bucket.item_indices.clear();
    }
    for (size_t item_index = 0; item_index < collector_.item_count(); ++item_index) {
        const tc_render_item* item = collector_.item(item_index);
        if (!item) {
            continue;
        }
        tc_material_phase* phase = nullptr;
        if (!tc_material_handle_is_invalid(item->material) &&
            item->material_phase_index != SIZE_MAX) {
            tc_material* material = tc_material_get(item->material);
            if (material && item->material_phase_index < material->phase_count) {
                phase = &material->phases[item->material_phase_index];
            }
        }
        if (!phase) {
            phase = item->material_phase;
        }
        if (!phase || !tc_phase_is_single(phase->phase)) {
            continue;
        }

        RenderSceneItemPhaseBucket* selected = nullptr;
        for (RenderSceneItemPhaseBucket& bucket : phase_buckets_) {
            if (bucket.phase == phase->phase) {
                selected = &bucket;
                break;
            }
        }
        if (!selected) {
            phase_buckets_.emplace_back();
            selected = &phase_buckets_.back();
            selected->phase = phase->phase;
        }
        selected->item_indices.push_back(item_index);
    }
    return valid_;
}

void RenderSceneItemSnapshot::invalidate_keep_capacity()
{
    collector_.clear_keep_capacity();
    counters_ = RenderSceneItemSnapshotCounters{};
    valid_ = false;
}

std::span<const size_t> RenderSceneItemSnapshot::phase_item_indices(
    tc_phase_mask phase) const
{
    if (!tc_phase_is_single(phase)) {
        return {};
    }
    for (const RenderSceneItemPhaseBucket& bucket : phase_buckets_) {
        if (bucket.phase == phase) {
            return bucket.item_indices;
        }
    }
    return {};
}

RenderSceneItemSnapshot* ensure_render_item_snapshot(
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

    RenderSceneItemCollectRequest request{};
    request.scene = context.scene.handle();
    request.layer_mask = context.layer_mask;
    request.render_category_mask = context.render_category_mask;
    request.debug_pass_name = debug_pass_name;
    request.camera = context.camera;
    request.scene_context = &context.scene;
    if (!context.render_item_snapshot->collect(request)) {
        tc::Log::error("[%s] failed to build scene RenderItem snapshot",
                       debug_pass_name ? debug_pass_name : "RenderItemSnapshot");
        return nullptr;
    }
    return context.render_item_snapshot;
}

bool render_item_matches_phase(const tc_render_item& item, tc_phase_mask requested_phase)
{
    if (requested_phase == TC_PHASE_NONE) {
        return true;
    }
    tc_material_phase* phase = nullptr;
    if (!tc_material_handle_is_invalid(item.material) &&
        item.material_phase_index != SIZE_MAX) {
        tc_material* material = tc_material_get(item.material);
        if (material && item.material_phase_index < material->phase_count) {
            phase = &material->phases[item.material_phase_index];
        }
    }
    if (!phase) {
        phase = item.material_phase;
    }
    return phase && phase->phase == requested_phase;
}

} // namespace termin
