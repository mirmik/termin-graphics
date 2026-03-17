// tc_camera_capability.h - Camera capability for components that provide a view
#ifndef TC_CAMERA_CAPABILITY_H
#define TC_CAMERA_CAPABILITY_H

#include "core/tc_component_capability.h"
#include "core/tc_component.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Plain C camera data for rendering (matches C++ RenderCamera layout)
typedef struct tc_camera_data {
    double view[16];        // 4x4 column-major view matrix
    double projection[16];  // 4x4 column-major projection matrix
    double position[3];     // world-space camera position
    double near_clip;
    double far_clip;
} tc_camera_data;

// Vtable for camera capability
typedef struct tc_camera_vtable {
    // Fill tc_camera_data. aspect_override > 0 means recompute projection with this aspect.
    bool (*get_camera_data)(tc_component* self, double aspect_override, tc_camera_data* out);
} tc_camera_vtable;

// Capability data stored on component
typedef struct tc_camera_capability {
    const tc_camera_vtable* vtable;
    void* userdata;
} tc_camera_capability;

// Get the global camera capability ID (registers on first call)
TC_API tc_component_cap_id tc_camera_capability_id(void);

// Attach camera capability to a component
TC_API bool tc_camera_capability_attach(
    tc_component* c,
    const tc_camera_vtable* vtable,
    void* userdata
);

// Get camera capability from component (NULL if not a camera)
TC_API const tc_camera_capability* tc_camera_capability_get(const tc_component* c);

// Convenience: check if component is a camera
static inline bool tc_component_is_camera(const tc_component* c) {
    return c && tc_camera_capability_get(c) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif // TC_CAMERA_CAPABILITY_H
