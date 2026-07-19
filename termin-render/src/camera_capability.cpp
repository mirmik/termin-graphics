#include <termin/render/camera_capability.hpp>

#include <cstring>
#include <tcbase/tc_log.hpp>

extern "C" {
#include <core/tc_camera_capability.h>
#include <core/tc_entity_pool.h>
}

namespace termin {

bool resolve_render_camera(
    tc_component* component,
    double aspect_override,
    RenderCameraSnapshot& out)
{
    const tc_camera_capability* capability =
        tc_camera_capability_get(component);
    if (!capability || !capability->vtable ||
        !capability->vtable->get_camera_data) {
        return false;
    }

    tc_camera_data data{};
    if (!capability->vtable->get_camera_data(
            component,
            aspect_override,
            &data)) {
        return false;
    }

    std::memcpy(out.camera.view.data, data.view, sizeof(data.view));
    std::memcpy(
        out.camera.projection.data,
        data.projection,
        sizeof(data.projection));
    out.camera.position =
        Vec3(data.position[0], data.position[1], data.position[2]);
    out.camera.near_clip = data.near_clip;
    out.camera.far_clip = data.far_clip;
    out.layer_mask = data.layer_mask;
    out.render_category_mask = data.render_category_mask;
    return true;
}

NamedCameraResolveError resolve_named_render_camera(
    tc_scene_handle scene,
    const char* entity_name,
    double aspect_override,
    RenderCameraSnapshot& out)
{
    if (!tc_scene_handle_valid(scene)) {
        return NamedCameraResolveError::InvalidScene;
    }
    if (!entity_name || entity_name[0] == '\0') {
        return NamedCameraResolveError::EmptyEntityName;
    }

    const tc_entity_id entity_id =
        tc_scene_find_entity_by_name(scene, entity_name);
    if (!tc_entity_id_valid(entity_id)) {
        return NamedCameraResolveError::EntityNotFound;
    }

    tc_entity_pool* pool = tc_scene_entity_pool(scene);
    const size_t component_count =
        tc_entity_pool_component_count(pool, entity_id);
    for (size_t i = 0; i < component_count; ++i) {
        tc_component* component =
            tc_entity_pool_component_at(pool, entity_id, i);
        if (!tc_camera_capability_get(component)) {
            continue;
        }
        return resolve_render_camera(component, aspect_override, out)
            ? NamedCameraResolveError::None
            : NamedCameraResolveError::CameraDataUnavailable;
    }
    return NamedCameraResolveError::CapabilityNotFound;
}

bool resolve_named_render_camera_for_pass(
    tc_scene_handle scene,
    const char* entity_name,
    double aspect_override,
    const char* pass_name,
    RenderCameraSnapshot& out)
{
    const NamedCameraResolveError error = resolve_named_render_camera(
        scene,
        entity_name,
        aspect_override,
        out);
    if (error == NamedCameraResolveError::None) {
        return true;
    }
    tc::Log::error(
        "[%s] cannot use named camera entity '%s': %s",
        pass_name ? pass_name : "RenderPass",
        entity_name ? entity_name : "",
        named_camera_resolve_error_name(error));
    return false;
}

const char* named_camera_resolve_error_name(NamedCameraResolveError error)
{
    switch (error) {
    case NamedCameraResolveError::None:
        return "none";
    case NamedCameraResolveError::InvalidScene:
        return "invalid scene";
    case NamedCameraResolveError::EmptyEntityName:
        return "empty entity name";
    case NamedCameraResolveError::EntityNotFound:
        return "entity not found";
    case NamedCameraResolveError::CapabilityNotFound:
        return "entity has no camera capability";
    case NamedCameraResolveError::CameraDataUnavailable:
        return "camera provider failed to produce camera data";
    }
    return "unknown camera resolution error";
}

} // namespace termin
