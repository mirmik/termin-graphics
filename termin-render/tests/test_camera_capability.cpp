#include "guard_main.h"

GUARD_TEST_MAIN();

#include <cstring>

#include <termin/render/camera_capability.hpp>

extern "C" {
#include <core/tc_camera_capability.h>
#include <core/tc_component.h>
#include <core/tc_entity_pool.h>
#include <core/tc_scene.h>
}

namespace {

struct AlternateCameraProvider {
    tc_component component;
    double last_aspect = 0.0;
};

bool get_camera_data(
    tc_component* component,
    double aspect_override,
    tc_camera_data* out)
{
    auto* provider = reinterpret_cast<AlternateCameraProvider*>(component);
    provider->last_aspect = aspect_override;
    *out = {};
    for (int i = 0; i < 16; ++i) {
        out->view[i] = 10.0 + i;
        out->projection[i] = 30.0 + i;
    }
    out->position[0] = 1.0;
    out->position[1] = 2.0;
    out->position[2] = 3.0;
    out->near_clip = 0.25;
    out->far_clip = 250.0;
    out->layer_mask = 0x1234u;
    out->render_category_mask = 0x5678u;
    return true;
}

const tc_camera_vtable kAlternateCameraVtable = {&get_camera_data};

} // namespace

TEST_CASE("camera capability resolves a provider without CameraComponent")
{
    AlternateCameraProvider provider{};
    tc_component_init(&provider.component, nullptr);
    REQUIRE(tc_camera_capability_attach(
        &provider.component, &kAlternateCameraVtable, &provider));

    termin::RenderCameraSnapshot snapshot;
    REQUIRE(termin::resolve_render_camera(
        &provider.component, 16.0 / 9.0, snapshot));
    CHECK(provider.last_aspect == 16.0 / 9.0);
    CHECK(snapshot.camera.view.data[0] == 10.0);
    CHECK(snapshot.camera.projection.data[15] == 45.0);
    CHECK(snapshot.camera.position.x == 1.0);
    CHECK(snapshot.camera.position.y == 2.0);
    CHECK(snapshot.camera.position.z == 3.0);
    CHECK(snapshot.camera.near_clip == 0.25);
    CHECK(snapshot.camera.far_clip == 250.0);
    CHECK(snapshot.layer_mask == 0x1234u);
    CHECK(snapshot.render_category_mask == 0x5678u);

    tc_component_clear_capabilities(&provider.component);
}

TEST_CASE("named camera lookup uses capability and reports missing capability")
{
    tc_scene_handle scene = tc_scene_new_named("camera-capability-test");
    REQUIRE(tc_scene_alive(scene));
    tc_entity_pool* pool = tc_scene_entity_pool(scene);

    tc_entity_id camera_entity = tc_entity_pool_alloc(pool, "alternate-camera");
    AlternateCameraProvider provider{};
    tc_component_init(&provider.component, nullptr);
    REQUIRE(tc_camera_capability_attach(
        &provider.component, &kAlternateCameraVtable, &provider));
    tc_entity_pool_add_component(pool, camera_entity, &provider.component);

    termin::RenderCameraSnapshot snapshot;
    CHECK(termin::resolve_named_render_camera(
              scene, "alternate-camera", 2.0, snapshot) ==
          termin::NamedCameraResolveError::None);
    CHECK(provider.last_aspect == 2.0);

    tc_entity_id plain_entity = tc_entity_pool_alloc(pool, "plain-entity");
    tc_component plain{};
    tc_component_init(&plain, nullptr);
    tc_entity_pool_add_component(pool, plain_entity, &plain);
    CHECK(termin::resolve_named_render_camera(
              scene, "plain-entity", 0.0, snapshot) ==
          termin::NamedCameraResolveError::CapabilityNotFound);
    CHECK(termin::resolve_named_render_camera(
              scene, "missing", 0.0, snapshot) ==
          termin::NamedCameraResolveError::EntityNotFound);

    tc_component_clear_capabilities(&provider.component);
    tc_entity_pool_remove_component(pool, camera_entity, &provider.component);
    tc_entity_pool_remove_component(pool, plain_entity, &plain);
    tc_scene_free(scene);
}
