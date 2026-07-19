#pragma once

#include <cstdint>
#include <termin/render/render_camera.hpp>
#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_component.h>
#include <core/tc_scene.h>
}

namespace termin {

struct RenderCameraSnapshot {
    RenderCamera camera;
    uint64_t layer_mask = UINT64_MAX;
    uint64_t render_category_mask = UINT64_MAX;
};

enum class NamedCameraResolveError : uint8_t {
    None,
    InvalidScene,
    EmptyEntityName,
    EntityNotFound,
    CapabilityNotFound,
    CameraDataUnavailable,
};

// Resolve the engine-facing camera capability without depending on a concrete
// component implementation. A positive aspect_override asks the provider to
// recompute its projection for that aspect ratio.
RENDER_API bool resolve_render_camera(
    tc_component* component,
    double aspect_override,
    RenderCameraSnapshot& out);

// Resolve the first camera-capable component attached to the named entity.
// The result distinguishes lookup failures so callers can emit actionable,
// pass-specific diagnostics.
RENDER_API NamedCameraResolveError resolve_named_render_camera(
    tc_scene_handle scene,
    const char* entity_name,
    double aspect_override,
    RenderCameraSnapshot& out);

RENDER_API bool resolve_named_render_camera_for_pass(
    tc_scene_handle scene,
    const char* entity_name,
    double aspect_override,
    const char* pass_name,
    RenderCameraSnapshot& out);

RENDER_API const char* named_camera_resolve_error_name(
    NamedCameraResolveError error);

} // namespace termin
