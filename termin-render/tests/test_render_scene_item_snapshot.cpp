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
tc_render_item_vec3 g_points[] = {
    {0.0, 0.0, 0.0},
    {1.0, 0.0, 0.0},
};

bool has_phase(tc_component*, const char*) { return true; }

bool collect_items(
    tc_component* component,
    const tc_render_item_collect_context* context,
    tc_render_item_sink* sink)
{
    assert(context);
    assert(context->phase_mark);
    assert(context->phase_mark[0] == '\0');
    ++g_collect_calls;
    for (int phase_variant = 0; phase_variant < 2; ++phase_variant) {
        tc_render_item item{};
        item.kind = TC_RENDER_ITEM_KIND_LINE_BATCH;
        item.component = component;
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
    &has_phase,
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

    termin::RenderSceneItemCollectRequest request{};
    request.scene = scene;
    request.scene_filter_flags = TC_SCENE_FILTER_NONE;

    termin::RenderSceneItemSnapshot first_view;
    assert(first_view.collect(request));
    assert(g_collect_calls == 1);
    assert(first_view.counters().scene_traversals == 1);
    assert(first_view.counters().drawable_producers == 1);
    assert(first_view.counters().emitted_items == 2);
    assert(first_view.item_count() == 2);
    assert(first_view.item(0)->payload.line_batch.points ==
           first_view.item(1)->payload.line_batch.points);
    assert(first_view.storage().line_batch_points.size() == 1);

    first_view.invalidate_keep_capacity();
    assert(first_view.collect(request));
    assert(g_collect_calls == 2);
    assert(first_view.storage().line_batch_points.size() == 1);

    termin::RenderSceneItemSnapshot second_view;
    assert(second_view.collect(request));
    assert(g_collect_calls == 3);
    assert(second_view.counters().scene_traversals == 1);

    tc_component_detach_capability(&component, tc_drawable_capability_id());
    tc_entity_pool_remove_component(pool, entity, &component);
    tc_scene_free(scene);
    return 0;
}
