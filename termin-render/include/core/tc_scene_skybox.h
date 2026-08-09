// tc_scene_skybox.h - Scene skybox properties
#ifndef TC_SCENE_SKYBOX_H
#define TC_SCENE_SKYBOX_H

#include "tc_types.h"
#include <geom/tc_color.h>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

// Skybox type
typedef enum tc_skybox_type {
    TC_SKYBOX_NONE = 0,
    TC_SKYBOX_GRADIENT = 1,
    TC_SKYBOX_SOLID = 2
} tc_skybox_type;

// Scene skybox properties
typedef struct tc_scene_skybox {
    int type;                    // tc_skybox_type
    tc_srgb_color color;         // Solid authored sRGB color
    tc_srgb_color top_color;     // Gradient authored sRGB color
    tc_srgb_color horizon_color; // Gradient authored sRGB color at the horizon
    tc_srgb_color bottom_color;  // Gradient authored sRGB color
    float top_exponent;          // Horizon-to-zenith curve, positive
    float bottom_exponent;       // Horizon-to-nadir curve, positive
    tc_mesh_handle mesh;         // Skybox cube mesh (refcounted via handle)
    tc_material_handle material; // Optional external skybox material handle
} tc_scene_skybox;

// Initialize with defaults
TC_API void tc_scene_skybox_init(tc_scene_skybox* skybox);

// Free resources (release mesh/material refs)
TC_API void tc_scene_skybox_free(tc_scene_skybox* skybox);

// Ensure skybox mesh exists (creates lazily if needed)
TC_API struct tc_mesh* tc_scene_skybox_ensure_mesh(tc_scene_skybox* skybox);

// Note: Scene skybox API functions (tc_scene_get_skybox,
// tc_scene_set_skybox_srgb_color, etc.)
// are declared in tc_scene_render_state.h with tc_scene_handle parameter

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_SKYBOX_H
