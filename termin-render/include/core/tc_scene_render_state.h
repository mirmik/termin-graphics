// tc_scene_render_state.h - Render scene state extension (lighting, skybox, background)
#ifndef TC_SCENE_RENDER_STATE_H
#define TC_SCENE_RENDER_STATE_H

#include "core/tc_scene_pool.h"
#include "core/tc_scene_extension.h"
#include "core/tc_scene_lighting.h"
#include "core/tc_scene_skybox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_scene_render_state {
    tc_scene_lighting lighting;
    tc_scene_skybox skybox;
    float background_color[4];
} tc_scene_render_state;

// Register builtin render-state extension type in scene-extension registry.
// Safe to call multiple times.
TC_API void tc_scene_render_state_extension_init(void);

// Access render-state extension instance from scene.
TC_API tc_scene_render_state* tc_scene_render_state_get(tc_scene_handle scene);

// Ensure render-state extension is attached to scene.
TC_API bool tc_scene_render_state_ensure(tc_scene_handle scene);

// Render-state scene API (moved from tc_scene.h)
TC_API void tc_scene_set_background_color(tc_scene_handle h, float r, float g, float b, float a);
TC_API void tc_scene_get_background_color(tc_scene_handle h, float* r, float* g, float* b, float* a);

TC_API tc_scene_skybox* tc_scene_get_skybox(tc_scene_handle h);
TC_API void tc_scene_set_skybox_type(tc_scene_handle h, int type);
TC_API int tc_scene_get_skybox_type(tc_scene_handle h);
TC_API void tc_scene_set_skybox_color(tc_scene_handle h, float r, float g, float b);
TC_API void tc_scene_get_skybox_color(tc_scene_handle h, float* r, float* g, float* b);
TC_API void tc_scene_set_skybox_top_color(tc_scene_handle h, float r, float g, float b);
TC_API void tc_scene_get_skybox_top_color(tc_scene_handle h, float* r, float* g, float* b);
TC_API void tc_scene_set_skybox_bottom_color(tc_scene_handle h, float r, float g, float b);
TC_API void tc_scene_get_skybox_bottom_color(tc_scene_handle h, float* r, float* g, float* b);
TC_API void tc_scene_set_skybox_mesh(tc_scene_handle h, struct tc_mesh* mesh);
TC_API struct tc_mesh* tc_scene_get_skybox_mesh(tc_scene_handle h);
TC_API void tc_scene_set_skybox_material(tc_scene_handle h, struct tc_material* material);
TC_API struct tc_material* tc_scene_get_skybox_material(tc_scene_handle h);

TC_API tc_scene_lighting* tc_scene_get_lighting(tc_scene_handle h);
TC_API void tc_scene_set_ambient(tc_scene_handle h, float r, float g, float b, float intensity);
TC_API void tc_scene_set_shadow_settings(tc_scene_handle h, int method, float softness, float bias);

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_RENDER_STATE_H
