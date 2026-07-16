#include "guard_c.h"

#include "render/tc_render_target.h"
#include "core/tc_component.h"
#include "core/tc_scene.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"

#include <limits.h>

GUARD_C_TEST(test_render_target_rejects_invalid_dimensions_without_mutation) {
    tc_render_target_handle target = tc_render_target_new("dimension-validation");
    GUARD_C_REQUIRE(tc_render_target_alive(target));

    tc_render_target_set_width(target, 64);
    tc_render_target_set_height(target, 48);
    tc_render_target_ensure_textures(target);

    tc_texture* color = tc_texture_get(tc_render_target_get_color_texture(target));
    tc_texture* depth = tc_texture_get(tc_render_target_get_depth_texture(target));
    GUARD_C_REQUIRE(color != NULL);
    GUARD_C_REQUIRE(depth != NULL);
    GUARD_C_CHECK_EQ_INT(64, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(48, tc_render_target_get_height(target));
    GUARD_C_CHECK_EQ_UINT(64, color->width);
    GUARD_C_CHECK_EQ_UINT(48, color->height);

    tc_render_target_set_width(target, -1);
    tc_render_target_set_height(target, 0);
    tc_render_target_set_width(target, INT_MAX);
    tc_render_target_set_height(target, TC_RENDER_TARGET_MAX_DIMENSION + 1);
    GUARD_C_CHECK_EQ_INT(64, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(48, tc_render_target_get_height(target));

    tc_render_target_set_width(target, 128);
    tc_render_target_set_height(target, 96);
    tc_render_target_ensure_textures(target);
    GUARD_C_CHECK_EQ_INT(128, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(96, tc_render_target_get_height(target));
    GUARD_C_CHECK_EQ_UINT(128, color->width);
    GUARD_C_CHECK_EQ_UINT(96, color->height);

    tc_render_target_free(target);
    return 0;
}

static void init_test_component(tc_component* component, const char* type_name) {
    if (!tc_component_registry_has(type_name)) {
        tc_component_registry_register_abstract(type_name, TC_CXX_COMPONENT, NULL);
    }
    tc_component_init(component, NULL);
    tc_component_set_declared_type_name(component, type_name);
}

GUARD_C_TEST(test_render_target_resolves_camera_replacement_from_entity_handle) {
    tc_scene_handle scene = tc_scene_new_named("rt-camera-resolution");
    GUARD_C_REQUIRE(tc_scene_alive(scene));
    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    GUARD_C_REQUIRE(pool != NULL);
    tc_entity_pool_handle pool_handle = tc_entity_pool_registry_find(pool);
    GUARD_C_REQUIRE(tc_entity_pool_handle_valid(pool_handle));
    tc_entity_id entity_id = tc_entity_pool_alloc(pool, "Camera");
    GUARD_C_REQUIRE(tc_entity_pool_alive(pool, entity_id));

    tc_component first_camera;
    init_test_component(&first_camera, "CameraComponent");
    tc_entity_pool_add_component(pool, entity_id, &first_camera);

    tc_render_target_handle target = tc_render_target_new("stable-camera");
    tc_render_target_set_scene(target, scene);
    tc_render_target_set_camera(target, &first_camera);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == &first_camera);

    tc_entity_pool_remove_component(pool, entity_id, &first_camera);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == NULL);

    tc_component replacement_camera;
    init_test_component(&replacement_camera, "CameraComponent");
    tc_entity_pool_add_component(pool, entity_id, &replacement_camera);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == &replacement_camera);

    tc_entity_pool_free(pool, entity_id);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == NULL);

    tc_render_target_free(target);
    tc_scene_free(scene);
    return 0;
}

GUARD_C_TEST(test_render_target_resolves_camera_from_scene_less_pool) {
    tc_scene_handle scene = tc_scene_new_named("rt-editor-camera-resolution");
    GUARD_C_REQUIRE(tc_scene_alive(scene));

    tc_entity_pool_handle pool_handle = tc_entity_pool_registry_create(4);
    GUARD_C_REQUIRE(tc_entity_pool_handle_valid(pool_handle));
    tc_entity_pool* pool = tc_entity_pool_registry_get(pool_handle);
    GUARD_C_REQUIRE(pool != NULL);
    GUARD_C_CHECK(!tc_scene_handle_valid(tc_entity_pool_get_scene(pool)));

    tc_entity_id entity_id = tc_entity_pool_alloc(pool, "Editor Camera");
    GUARD_C_REQUIRE(tc_entity_pool_alive(pool, entity_id));
    tc_component camera;
    init_test_component(&camera, "CameraComponent");
    tc_entity_pool_add_component(pool, entity_id, &camera);

    tc_render_target_handle target = tc_render_target_new("editor-target");
    tc_render_target_set_scene(target, scene);
    tc_render_target_set_camera(target, &camera);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == &camera);

    tc_entity_pool_free(pool, entity_id);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == NULL);

    tc_render_target_free(target);
    tc_entity_pool_registry_destroy(pool_handle);
    tc_scene_free(scene);
    return 0;
}

GUARD_C_TEST(test_render_target_rejects_camera_from_another_scene) {
    tc_scene_handle target_scene = tc_scene_new_named("rt-target-scene");
    tc_scene_handle camera_scene = tc_scene_new_named("rt-camera-scene");
    GUARD_C_REQUIRE(tc_scene_alive(target_scene));
    GUARD_C_REQUIRE(tc_scene_alive(camera_scene));

    tc_entity_pool* camera_pool = tc_scene_entity_pool(camera_scene);
    GUARD_C_REQUIRE(camera_pool != NULL);
    tc_entity_id camera_entity = tc_entity_pool_alloc(camera_pool, "Camera");
    GUARD_C_REQUIRE(tc_entity_pool_alive(camera_pool, camera_entity));
    tc_component camera;
    init_test_component(&camera, "CameraComponent");
    tc_entity_pool_add_component(camera_pool, camera_entity, &camera);

    tc_render_target_handle target = tc_render_target_new("cross-scene-target");
    tc_render_target_set_scene(target, target_scene);
    tc_render_target_set_camera(target, &camera);
    GUARD_C_CHECK(tc_render_target_get_camera(target) == NULL);

    tc_render_target_free(target);
    tc_scene_free(camera_scene);
    tc_scene_free(target_scene);
    return 0;
}

GUARD_C_TEST(test_render_target_resolves_xr_origin_and_rejects_stale_scene) {
    tc_scene_handle scene = tc_scene_new_named("rt-xr-resolution");
    GUARD_C_REQUIRE(tc_scene_alive(scene));
    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    GUARD_C_REQUIRE(pool != NULL);
    tc_entity_pool_handle pool_handle = tc_entity_pool_registry_find(pool);
    GUARD_C_REQUIRE(tc_entity_pool_handle_valid(pool_handle));
    tc_entity_id entity_id = tc_entity_pool_alloc(pool, "XR Origin");
    GUARD_C_REQUIRE(tc_entity_pool_alive(pool, entity_id));

    tc_component xr_origin;
    init_test_component(&xr_origin, "XrOriginComponent");
    tc_entity_pool_add_component(pool, entity_id, &xr_origin);

    tc_render_target_handle target = tc_render_target_new("stable-xr-origin");
    tc_render_target_set_scene(target, scene);
    tc_render_target_set_xr_origin(target, &xr_origin);
    GUARD_C_CHECK(tc_render_target_get_xr_origin(target) == &xr_origin);

    tc_scene_free(scene);
    GUARD_C_CHECK(tc_render_target_get_xr_origin(target) == NULL);

    tc_render_target_free(target);
    return 0;
}

int main(int argc, char** argv) {
    GUARD_C_BEGIN_ARGS(argc, argv);
    GUARD_C_RUN(test_render_target_rejects_invalid_dimensions_without_mutation);
    GUARD_C_RUN(test_render_target_resolves_camera_replacement_from_entity_handle);
    GUARD_C_RUN(test_render_target_resolves_camera_from_scene_less_pool);
    GUARD_C_RUN(test_render_target_rejects_camera_from_another_scene);
    GUARD_C_RUN(test_render_target_resolves_xr_origin_and_rejects_stale_scene);
    tc_component_registry_cleanup();
    return GUARD_C_END();
}
