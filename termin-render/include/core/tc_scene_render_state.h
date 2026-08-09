// tc_scene_render_state.h - Render scene state extension (lighting, skybox, background)
#ifndef TC_SCENE_RENDER_STATE_H
#define TC_SCENE_RENDER_STATE_H

#include "core/tc_scene_extension.h"
#include "core/tc_scene_lighting.h"
#include "core/tc_scene_pool.h"
#include "core/tc_scene_skybox.h"
#include <geom/tc_color.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_scene_render_state {
    tc_scene_lighting lighting;
    tc_scene_skybox skybox;
    tc_srgb_color background_color;
} tc_scene_render_state;

// Register builtin render-state extension type in scene-extension registry.
// Safe to call multiple times.
TC_API void tc_scene_render_state_extension_init(void);

// Access render-state extension instance from scene.
TC_API tc_scene_render_state* tc_scene_render_state_get(tc_scene_handle scene);

// Ensure render-state extension is attached to scene.
TC_API bool tc_scene_render_state_ensure(tc_scene_handle scene);

// Render-state scene API (moved from tc_scene.h)
TC_API void tc_scene_set_background_srgb_color(tc_scene_handle h, tc_srgb_color color);
TC_API void tc_scene_get_background_srgb_color(tc_scene_handle h, tc_srgb_color* out_color);

TC_API tc_scene_skybox* tc_scene_get_skybox(tc_scene_handle h);
TC_API void tc_scene_set_skybox_type(tc_scene_handle h, int type);
TC_API int tc_scene_get_skybox_type(tc_scene_handle h);
TC_API void tc_scene_set_skybox_srgb_color(tc_scene_handle h, tc_srgb_color color);
TC_API void tc_scene_get_skybox_srgb_color(tc_scene_handle h, tc_srgb_color* out_color);
TC_API void tc_scene_set_skybox_top_srgb_color(tc_scene_handle h, tc_srgb_color color);
TC_API void tc_scene_get_skybox_top_srgb_color(tc_scene_handle h, tc_srgb_color* out_color);
TC_API void tc_scene_set_skybox_bottom_srgb_color(tc_scene_handle h, tc_srgb_color color);
TC_API void tc_scene_get_skybox_bottom_srgb_color(tc_scene_handle h, tc_srgb_color* out_color);
TC_API void tc_scene_set_skybox_mesh(tc_scene_handle h, struct tc_mesh* mesh);
TC_API struct tc_mesh* tc_scene_get_skybox_mesh(tc_scene_handle h);
TC_API void tc_scene_set_skybox_material(tc_scene_handle h, struct tc_material* material);
TC_API struct tc_material* tc_scene_get_skybox_material(tc_scene_handle h);

TC_API tc_scene_lighting* tc_scene_get_lighting(tc_scene_handle h);
TC_API void tc_scene_set_ambient_srgb_color(tc_scene_handle h, tc_srgb_color color, float intensity);
TC_API void tc_scene_set_shadow_settings(tc_scene_handle h, int method, float softness, float bias);

#ifdef __cplusplus
}
#endif

#endif // TC_SCENE_RENDER_STATE_H
