// tc_scene_skybox.h - Scene skybox properties
#ifndef TC_SCENE_SKYBOX_H
#define TC_SCENE_SKYBOX_H

#include "tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_mesh;
struct tc_material;

// Skybox type
typedef enum tc_skybox_type {
    TC_SKYBOX_NONE = 0,
    TC_SKYBOX_GRADIENT = 1,
    TC_SKYBOX_SOLID = 2
} tc_skybox_type;

// Scene skybox properties
typedef struct tc_scene_skybox {
    int type;                    // tc_skybox_type
    float color[3];              // Solid color RGB
    float top_color[3];          // Gradient top RGB
    float bottom_color[3];       // Gradient bottom RGB
    struct tc_mesh* mesh;        // Skybox cube mesh (refcounted)
    struct tc_material* material; // Current skybox material (refcounted, alias to one of below)
    struct tc_material* gradient_material;  // Gradient skybox material (refcounted)
    struct tc_material* solid_material;     // Solid skybox material (refcounted)
} tc_scene_skybox;

// Initialize with defaults
TC_API void tc_scene_skybox_init(tc_scene_skybox* skybox);

// Free resources (release mesh/material refs)
TC_API void tc_scene_skybox_free(tc_scene_skybox* skybox);

// Ensure skybox mesh exists (creates lazily if needed)
TC_API struct tc_mesh* tc_scene_skybox_ensure_mesh(tc_scene_skybox* skybox);

// Ensure skybox material exists for given type (creates lazily if needed)
// type: 0=none, 1=gradient, 2=solid
TC_API struct tc_material* tc_scene_skybox_ensure_material(tc_scene_skybox* skybox, int type);

// Note: Scene skybox API functions (tc_scene_get_skybox, tc_scene_set_skybox_*, etc.)
// are declared in tc_scene_render_state.h with tc_scene_handle parameter

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_SKYBOX_H
