#include <stdio.h>
#include <string.h>

#include "core/tc_component.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene.h"
#include "core/tc_scene_drawable.h"
#include "core/tc_drawable_capability.h"
#include "core/tc_drawable_protocol.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            return 1; \
        } \
    } while (0)

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

static void* test_drawable_get_geometry_draws(tc_component* self, const char* phase_mark) {
    (void)self;
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
};

static bool count_components(tc_component* c, void* user_data) {
    (void)c;
    int* count = (int*)user_data;
    (*count)++;
    return true;
}

static int test_live_reindex_for_drawable_capability(void) {
    tc_component_cap_id drawable_cap = tc_drawable_capability_id();
    TEST_ASSERT(drawable_cap != TC_COMPONENT_CAPABILITY_INVALID_ID, "drawable capability id ok");

    tc_scene_handle scene = tc_scene_new_named("drawable-reindex-scene");
    TEST_ASSERT(tc_scene_alive(scene), "scene created");

    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    tc_entity_id entity = tc_entity_pool_alloc(pool, "entity");
    TEST_ASSERT(tc_entity_id_valid(entity), "entity created");

    tc_component component;
    tc_component_init(&component, NULL);

    tc_entity_pool_add_component(pool, entity, &component);
    TEST_ASSERT(tc_scene_capability_count(scene, drawable_cap) == 0, "no drawable capability initially");

    TEST_ASSERT(tc_drawable_capability_attach(&component, &g_test_drawable_vtable, (void*)0x1234), "attach live drawable capability");
    TEST_ASSERT(tc_scene_capability_count(scene, drawable_cap) == 1, "live reindex adds drawable capability");
    TEST_ASSERT(tc_component_is_drawable(&component), "component is drawable");
    TEST_ASSERT(tc_component_get_drawable_userdata(&component) == (void*)0x1234, "drawable userdata stored");
    TEST_ASSERT(tc_component_has_phase(&component, "opaque"), "phase dispatch works");
    TEST_ASSERT(tc_component_get_geometry_draws(&component, "opaque") == (void*)0x1, "geometry draw retrieval works");

    g_draw_called = false;
    tc_component_draw_geometry(&component, NULL, 0);
    TEST_ASSERT(g_draw_called, "draw dispatch works");

    int count = 0;
    tc_scene_foreach_drawable(scene, count_components, &count, TC_SCENE_FILTER_NONE, 0);
    TEST_ASSERT(count == 1, "scene drawable iteration sees one component");

    tc_component_detach_capability(&component, drawable_cap);
    TEST_ASSERT(tc_scene_capability_count(scene, drawable_cap) == 0, "live reindex removes drawable capability");

    tc_entity_pool_remove_component(pool, entity, &component);
    tc_scene_free(scene);
    return 0;
}

int main(void) {
    int result = test_live_reindex_for_drawable_capability();
    if (result == 0) {
        printf("All drawable capability tests PASSED\n");
    }
    return result;
}
