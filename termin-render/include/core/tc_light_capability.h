// tc_light_capability.h - Light capability for components that emit light
#ifndef TC_LIGHT_CAPABILITY_H
#define TC_LIGHT_CAPABILITY_H

#include "core/tc_component_capability.h"
#include "core/tc_component.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Light type enum (matches C++ LightType)
typedef enum tc_light_type {
    TC_LIGHT_DIRECTIONAL = 0,
    TC_LIGHT_POINT = 1,
    TC_LIGHT_SPOT = 2
} tc_light_type;

// Shadow parameters (plain C)
typedef struct tc_light_shadow_params {
    bool enabled;
    double bias;
    double normal_bias;
    int map_resolution;
    int cascade_count;
    float max_distance;
    float split_lambda;
    bool cascade_blend;
    float blend_distance;
} tc_light_shadow_params;

// Plain C light data struct (matches C++ Light for rendering)
typedef struct tc_light_data {
    tc_light_type type;
    double color[3];        // RGB
    double intensity;
    double direction[3];    // normalized direction
    double position[3];     // world position
    bool has_range;
    double range;
    double inner_angle;     // radians
    double outer_angle;     // radians
    tc_light_shadow_params shadows;
} tc_light_data;

// Vtable for light capability
typedef struct tc_light_vtable {
    // Fill tc_light_data from component state. Returns true on success.
    bool (*get_light_data)(tc_component* self, tc_light_data* out_data);
} tc_light_vtable;

// Capability data stored on component
typedef struct tc_light_capability {
    const tc_light_vtable* vtable;
    void* userdata;
} tc_light_capability;

// Get the global light capability ID (registers on first call)
TC_API tc_component_cap_id tc_light_capability_id(void);

// Attach light capability to a component
TC_API bool tc_light_capability_attach(
    tc_component* c,
    const tc_light_vtable* vtable,
    void* userdata
);

// Get light capability from component (NULL if not a light)
TC_API const tc_light_capability* tc_light_capability_get(const tc_component* c);

// Convenience: check if component is a light
static inline bool tc_component_is_light(const tc_component* c) {
    return c && tc_light_capability_get(c) != NULL;
}

#ifdef __cplusplus
}
#endif

#endif // TC_LIGHT_CAPABILITY_H
