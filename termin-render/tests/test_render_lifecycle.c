#include "guard_c.h"

#include "core/tc_component.h"
#include "core/tc_entity_pool.h"
#include "core/tc_render_lifecycle.h"
#include "core/tc_scene.h"
#include "core/tc_scene_render_mount.h"
#include "termin_scene/internal/tc_scene_extension_registry.h"

typedef struct probe {
    tc_component component;
    int attach_count;
    int prepare_count;
    int detach_count;
    int prepare_order;
    tc_debug_geometry_type_id debug_type;
} probe;

static int g_prepare_order = 0;

static probe* probe_from_component(tc_component* component) {
    const tc_render_lifecycle_capability* capability =
        tc_render_lifecycle_capability_get(component);
    return capability ? (probe*)capability->userdata : NULL;
}

static void on_attach(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    probe* self = probe_from_component(component);
    if (!self || !context) return;
    self->attach_count++;
}

static void on_prepare(
    tc_component* component,
    const tc_render_prepare_context* context
) {
    probe* self = probe_from_component(component);
    if (!self || !context) return;
    self->prepare_count++;
    self->prepare_order = ++g_prepare_order;
    tc_debug_geometry_drawer drawer = {
        component->lifecycle_scene,
        self->debug_type,
    };
    const float start[3] = {0.0f, 0.0f, 0.0f};
    const float end[3] = {1.0f, 0.0f, 0.0f};
    const float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    tc_debug_geometry_drawer_line(&drawer, start, end, color, false);
}

static void on_detach(
    tc_component* component,
    const tc_render_attachment_context* context
) {
    probe* self = probe_from_component(component);
    if (!self || !context) return;
    self->detach_count++;
}

static const tc_render_lifecycle_vtable probe_vtable = {
    on_attach,
    on_prepare,
    on_detach,
};

static bool probe_init(probe* self) {
    *self = (probe){0};
    tc_component_init(&self->component, NULL);
    return tc_render_lifecycle_capability_attach(
        &self->component, &probe_vtable, self);
}

GUARD_C_TEST(test_render_lifecycle_is_balanced_for_dynamic_components) {
    tc_scene_ext_registry_init();
    tc_scene_render_mount_extension_init();

    tc_debug_geometry_type_id debug_type = tc_debug_geometry_type_register(
        "test.render.lifecycle", "Render lifecycle test", "Tests", true);
    GUARD_C_REQUIRE(debug_type != TC_DEBUG_GEOMETRY_TYPE_INVALID);
    GUARD_C_CHECK_EQ_INT(1, (int)tc_debug_geometry_type_count());

    tc_scene_handle scene = tc_scene_new_named("render-lifecycle-test");
    GUARD_C_REQUIRE(tc_scene_alive(scene));
    GUARD_C_REQUIRE(tc_scene_render_mount_ensure(scene));

    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    tc_entity_id entity = tc_entity_pool_alloc(pool, "participants");
    GUARD_C_REQUIRE(tc_entity_id_valid(entity));

    probe early;
    GUARD_C_REQUIRE(probe_init(&early));
    early.debug_type = debug_type;
    tc_entity_pool_add_component(pool, entity, &early.component);

    int attachment_storage = 0;
    const tc_render_attachment_context* attachment =
        (const tc_render_attachment_context*)&attachment_storage;
    tc_scene_render_mount_notify_attach(scene, attachment);
    GUARD_C_CHECK_EQ_INT(1, early.attach_count);

    int prepare_storage = 0;
    const tc_render_prepare_context* prepare =
        (const tc_render_prepare_context*)&prepare_storage;
    tc_scene_render_mount_prepare(scene, prepare);
    GUARD_C_CHECK_EQ_INT(1, early.prepare_count);
    GUARD_C_CHECK_EQ_INT(1, (int)tc_scene_debug_geometry_primitive_count(scene));

    probe late;
    GUARD_C_REQUIRE(probe_init(&late));
    late.debug_type = debug_type;
    tc_entity_pool_add_component(pool, entity, &late.component);
    GUARD_C_CHECK_EQ_INT(1, late.attach_count);

    tc_scene_render_mount_prepare(scene, prepare);
    GUARD_C_CHECK_EQ_INT(2, early.prepare_count);
    GUARD_C_CHECK_EQ_INT(1, late.prepare_count);

    GUARD_C_REQUIRE(tc_render_lifecycle_set_priority(&early.component, 0));
    GUARD_C_REQUIRE(tc_render_lifecycle_set_priority(&late.component, 10));
    g_prepare_order = 0;
    tc_scene_render_mount_prepare(scene, prepare);
    GUARD_C_CHECK_EQ_INT(1, late.prepare_order);
    GUARD_C_CHECK_EQ_INT(2, early.prepare_order);

    tc_component_set_enabled(&late.component, false);
    GUARD_C_REQUIRE(tc_scene_debug_geometry_set_enabled(scene, debug_type, false));
    tc_scene_render_mount_prepare(scene, prepare);
    GUARD_C_CHECK_EQ_INT(4, early.prepare_count);
    GUARD_C_CHECK_EQ_INT(2, late.prepare_count);
    GUARD_C_CHECK_EQ_INT(0, (int)tc_scene_debug_geometry_primitive_count(scene));

    tc_entity_pool_remove_component(pool, entity, &late.component);
    GUARD_C_CHECK_EQ_INT(1, late.detach_count);

    tc_scene_render_mount_notify_detach(scene, attachment);
    GUARD_C_CHECK_EQ_INT(1, early.detach_count);
    GUARD_C_CHECK_EQ_INT(1, late.detach_count);

    tc_entity_pool_remove_component(pool, entity, &early.component);
    tc_component_clear_capabilities(&early.component);
    tc_component_clear_capabilities(&late.component);
    tc_scene_free(scene);
    GUARD_C_REQUIRE(tc_debug_geometry_type_unregister(debug_type));
    GUARD_C_CHECK_EQ_INT(0, (int)tc_debug_geometry_type_count());
    tc_scene_ext_registry_shutdown();
    return 0;
}

int main(int argc, char** argv) {
    GUARD_C_BEGIN_ARGS(argc, argv);
    GUARD_C_RUN(test_render_lifecycle_is_balanced_for_dynamic_components);
    return GUARD_C_END();
}
