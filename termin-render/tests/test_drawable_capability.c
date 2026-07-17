#include <string.h>

#include "guard_c.h"

#include "core/tc_component.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene.h"
#include "core/tc_scene_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"

static int g_render_item_emit_count = 0;

static bool test_drawable_has_phase(tc_component* self, const char* phase_mark) {
    (void)self;
    return phase_mark && strcmp(phase_mark, "opaque") == 0;
}

static bool test_drawable_collect_render_items(tc_component* self, const tc_render_item_collect_context* context, tc_render_item_sink* sink) {
    (void)self;
    if (!context || !sink || !sink->emit) {
        return false;
    }

    tc_render_item item;
    memset(&item, 0, sizeof(item));
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.geometry_id = 7;
    return sink->emit(&item, sink->user_data);
}

static const tc_drawable_vtable g_test_drawable_vtable = {
    .has_phase = test_drawable_has_phase,
    .collect_render_items = test_drawable_collect_render_items,
};

static bool count_render_item_emit(const tc_render_item* item, void* user_data) {
    (void)user_data;
    if (item && item->kind == TC_RENDER_ITEM_KIND_MESH && item->geometry_id == 7) {
        g_render_item_emit_count++;
    }
    return true;
}

static bool count_components(tc_component* c, void* user_data) {
    (void)c;
    int* count = (int*)user_data;
    (*count)++;
    return true;
}

GUARD_C_TEST(test_live_reindex_for_drawable_capability) {
    tc_component_cap_id drawable_cap = tc_drawable_capability_id();
    GUARD_C_REQUIRE(drawable_cap != TC_COMPONENT_CAPABILITY_INVALID_ID);

    tc_scene_handle scene = tc_scene_new_named("drawable-reindex-scene");
    GUARD_C_REQUIRE(tc_scene_alive(scene));

    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    tc_entity_id entity = tc_entity_pool_alloc(pool, "entity");
    GUARD_C_REQUIRE(tc_entity_id_valid(entity));

    tc_component component;
    tc_component_init(&component, NULL);

    tc_entity_pool_add_component(pool, entity, &component);
    GUARD_C_CHECK_EQ_INT(0, tc_scene_capability_count(scene, drawable_cap));

    GUARD_C_REQUIRE(tc_drawable_capability_attach(&component, &g_test_drawable_vtable, (void*)0x1234));
    GUARD_C_CHECK_EQ_INT(1, tc_scene_capability_count(scene, drawable_cap));
    GUARD_C_CHECK(tc_component_is_drawable(&component));
    GUARD_C_CHECK_PTR_EQ((void*)0x1234, tc_component_get_drawable_userdata(&component));
    GUARD_C_CHECK(tc_component_has_phase(&component, "opaque"));

    g_render_item_emit_count = 0;
    tc_render_item_collect_context collect_context;
    memset(&collect_context, 0, sizeof(collect_context));
    collect_context.phase_mark = "opaque";
    tc_render_item_sink sink;
    sink.emit = count_render_item_emit;
    sink.user_data = NULL;
    GUARD_C_CHECK(tc_component_collect_render_items(&component, &collect_context, &sink));
    GUARD_C_CHECK_EQ_INT(1, g_render_item_emit_count);

    int count = 0;
    tc_scene_foreach_drawable(scene, count_components, &count, TC_SCENE_FILTER_NONE, 0);
    GUARD_C_CHECK_EQ_INT(1, count);

    tc_component_detach_capability(&component, drawable_cap);
    GUARD_C_CHECK_EQ_INT(0, tc_scene_capability_count(scene, drawable_cap));

    tc_entity_pool_remove_component(pool, entity, &component);
    tc_scene_free(scene);
    return 0;
}

int main(int argc, char** argv) {
    GUARD_C_BEGIN_ARGS(argc, argv);
    GUARD_C_RUN(test_live_reindex_for_drawable_capability);
    return GUARD_C_END();
}
