// tc_scene_lighting.h - Scene lighting properties (ambient, shadows)
#ifndef TC_SCENE_LIGHTING_H
#define TC_SCENE_LIGHTING_H

#include "tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Shadow sampling method
typedef enum tc_shadow_method {
    TC_SHADOW_METHOD_HARD = 0,
    TC_SHADOW_METHOD_PCF = 1,
    TC_SHADOW_METHOD_POISSON = 2
} tc_shadow_method;

// Scene lighting properties
typedef struct tc_scene_lighting {
    // Ambient lighting
    float ambient_color[3];   // RGB, default (1, 1, 1)
    float ambient_intensity;  // Default 0.1

    // Shadow settings
    int shadow_method;        // tc_shadow_method, default PCF
    float shadow_softness;    // Sampling radius multiplier, default 1.0
    float shadow_bias;        // Depth bias, default 0.005
} tc_scene_lighting;

// Initialize with defaults
TC_API void tc_scene_lighting_init(tc_scene_lighting* lighting);

// Note: Scene lighting API functions (tc_scene_get_lighting, tc_scene_set_ambient, etc.)
// are declared in tc_scene_render_state.h with tc_scene_handle parameter

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_LIGHTING_H
