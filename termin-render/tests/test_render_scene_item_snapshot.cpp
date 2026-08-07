#include <cassert>
#include <cstring>

#include <termin/render/render_scene_item_collector.hpp>

extern "C" {
#include <core/tc_component.h>
#include <core/tc_drawable_capability.h>
#include <core/tc_entity_pool.h>
#include <core/tc_scene.h>
}

namespace {

int g_collect_calls = 0;
uint64_t g_last_layer_mask = 0;
uint64_t g_last_render_category_mask = 0;
const void* g_last_camera = nullptr;
tc_render_item_vec3 g_points[] = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
};

tc_phase_mask phase_mask(tc_component*) { return TC_PHASE_EDITOR_DEBUG; }

bool collect_items(
    tc_component* component,
    const tc_render_item_collect_context* context,
    tc_render_item_sink* sink)
{
    assert(context);
    assert(context->phase == TC_PHASE_NONE || context->phase == TC_PHASE_EDITOR_DEBUG);
    ++g_collect_calls;
    g_last_layer_mask = context->layer_mask;
    g_last_render_category_mask = context->render_category_mask;
    g_last_camera = context->camera;
    for (int phase_variant = 0; phase_variant < 2; ++phase_variant) {
        tc_render_item item{};
        item.kind = TC_RENDER_ITEM_KIND_LINE_BATCH;
        item.geometry_id = 17;
        item.payload.line_batch.points = g_points;
        item.payload.line_batch.point_count = 2;
        if (!sink->emit(&item, sink->user_data)) {
            return false;
        }
    }
    return true;
}

const tc_drawable_vtable kDrawableVtable = {
    &phase_mask,
    &collect_items,
};

} // namespace

int main()
{
    tc_scene_handle scene = tc_scene_new_named("render-item-snapshot-test");
    assert(tc_scene_alive(scene));
    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    tc_entity_id entity = tc_entity_pool_alloc(pool, "drawable");
    assert(tc_entity_id_valid(entity));

    tc_component component;
    tc_component_init(&component, nullptr);
    tc_entity_pool_add_component(pool, entity, &component);
    assert(tc_drawable_capability_attach(&component, &kDrawableVtable, &component));

    termin::TcSceneRenderItemSource source(
        scene,
        nullptr,
        TC_SCENE_FILTER_NONE);
    termin::RenderViewState view;
    view.primary.emplace();
    termin::RenderItemSourceRequest source_request{};
    source_request.view = &view;
    source_request.layer_mask = 1;
    source_request.render_category_mask = 0x34;
    source_request.debug_name = "render-item-snapshot-test";

    termin::RenderItemSnapshot first_view;
    assert(source.publish(first_view, source_request));
    assert(g_collect_calls == 1);
    assert(g_last_layer_mask == 1);
    assert(g_last_render_category_mask == 0x34);
    assert(g_last_camera == view.primary_view());
    assert(first_view.counters().source_traversals == 1);
    assert(first_view.counters().producers == 1);
    assert(first_view.counters().emitted_items == 2);
    assert(first_view.item_count() == 2);
    assert(first_view.item(0)->source.domain_id ==
           TC_RENDER_ITEM_SOURCE_DOMAIN_SCENE);
    assert(first_view.item(0)->source.object_id != 0);
    assert(first_view.item(0)->source.generation == entity.generation);
    assert(termin::render_scene_item_component(*first_view.item(0)) ==
           &component);
    assert(first_view.item(0)->payload.line_batch.points ==
           first_view.item(1)->payload.line_batch.points);
    assert(first_view.storage().line_batch_points.size() == 1);

    first_view.invalidate_keep_capacity();
    assert(source.publish(first_view, source_request));
    assert(g_collect_calls == 2);
    assert(first_view.storage().line_batch_points.size() == 1);

    termin::RenderItemSnapshot second_view;
    assert(source.publish(second_view, source_request));
    assert(g_collect_calls == 3);
    assert(second_view.counters().source_traversals == 1);

    // Editor-only geometry must be rejected before its producer is called for
    // service phases that it does not advertise (notably the normal pass).
    termin::RenderSceneItemCollector phase_filtered;
    termin::RenderSceneItemCollectRequest request{};
    request.scene = scene;
    request.scene_filter_flags = TC_SCENE_FILTER_NONE;
    request.phase = TC_PHASE_NORMAL;
    assert(phase_filtered.collect(request));
    assert(g_collect_calls == 3);
    assert(phase_filtered.item_count() == 0);
    assert(phase_filtered.last_drawable_producers() == 0);

    request.phase = TC_PHASE_EDITOR_DEBUG;
    assert(phase_filtered.collect(request));
    assert(g_collect_calls == 4);
    assert(phase_filtered.item_count() == 2);
    assert(phase_filtered.last_drawable_producers() == 1);

    tc_component_detach_capability(&component, tc_drawable_capability_id());
    tc_entity_pool_remove_component(pool, entity, &component);
    tc_scene_free(scene);
    return 0;
}
