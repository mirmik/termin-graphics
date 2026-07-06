#include <string.h>

#include "guard_c.h"

#include "core/tc_component.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene.h"
#include "core/tc_scene_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"

static bool g_draw_called = false;

static bool test_drawable_has_phase(tc_component* self, const char* phase_mark) {
    (void)self;
    return phase_mark && strcmp(phase_mark, "opaque") == 0;
}

static void test_drawable_draw_geometry(tc_component* self, void* render_context, int geometry_id) {
    (void)self;
    (void)render_context;
    (void)geometry_id;
    g_draw_called = true;
}

static void* test_drawable_get_geometry_draws(tc_component* self, void* render_context, const char* phase_mark) {
    (void)self;
    (void)render_context;
    (void)phase_mark;
    return (void*)0x1;
}

static tc_shader_handle test_drawable_override_shader(tc_component* self, const char* phase_mark, int geometry_id, tc_shader_handle original_shader) {
    (void)self;
    (void)phase_mark;
    (void)geometry_id;
    return original_shader;
}

static const tc_drawable_vtable g_test_drawable_vtable = {
    .has_phase = test_drawable_has_phase,
    .draw_geometry = test_drawable_draw_geometry,
    .get_geometry_draws = test_drawable_get_geometry_draws,
    .override_shader = test_drawable_override_shader,
    .collect_shader_usages = NULL,
};

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
    GUARD_C_CHECK_PTR_EQ((void*)0x1, tc_component_get_geometry_draws(&component, &component, "opaque"));

    g_draw_called = false;
    tc_component_draw_geometry(&component, NULL, 0);
    GUARD_C_CHECK(g_draw_called);

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
