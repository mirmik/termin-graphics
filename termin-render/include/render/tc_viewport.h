// tc_viewport.h - Viewport (what to render and where)
#ifndef TC_VIEWPORT_H
#define TC_VIEWPORT_H

#include "tc_types.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene_pool.h"
#include "render/tc_viewport_pool.h"
#include "render/tc_pipeline_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_input_manager tc_input_manager;

// Viewport Lifecycle
TC_API tc_viewport_handle tc_viewport_new(const char* name, tc_scene_handle scene, tc_component* camera);
TC_API void tc_viewport_free(tc_viewport_handle h);
TC_API bool tc_viewport_alive(tc_viewport_handle h);

// Viewport Properties
TC_API void tc_viewport_set_name(tc_viewport_handle h, const char* name);
TC_API const char* tc_viewport_get_name(tc_viewport_handle h);

TC_API void tc_viewport_set_rect(tc_viewport_handle h, float x, float y, float w, float height);
TC_API void tc_viewport_get_rect(tc_viewport_handle h, float* x, float* y, float* w, float* height);

TC_API void tc_viewport_set_pixel_rect(tc_viewport_handle h, int px, int py, int pw, int ph);
TC_API void tc_viewport_get_pixel_rect(tc_viewport_handle h, int* px, int* py, int* pw, int* ph);

TC_API void tc_viewport_set_depth(tc_viewport_handle h, int depth);
TC_API int tc_viewport_get_depth(tc_viewport_handle h);

TC_API void tc_viewport_set_pipeline(tc_viewport_handle h, tc_pipeline_handle pipeline);
TC_API tc_pipeline_handle tc_viewport_get_pipeline(tc_viewport_handle h);

TC_API void tc_viewport_set_layer_mask(tc_viewport_handle h, uint64_t mask);
TC_API uint64_t tc_viewport_get_layer_mask(tc_viewport_handle h);

TC_API void tc_viewport_set_enabled(tc_viewport_handle h, bool enabled);
TC_API bool tc_viewport_get_enabled(tc_viewport_handle h);

TC_API void tc_viewport_set_scene(tc_viewport_handle h, tc_scene_handle scene);
TC_API tc_scene_handle tc_viewport_get_scene(tc_viewport_handle h);

TC_API void tc_viewport_set_camera(tc_viewport_handle h, tc_component* camera);
TC_API tc_component* tc_viewport_get_camera(tc_viewport_handle h);
TC_API tc_entity_handle tc_viewport_get_camera_entity(tc_viewport_handle h);

TC_API void tc_viewport_set_input_mode(tc_viewport_handle h, const char* mode);
TC_API const char* tc_viewport_get_input_mode(tc_viewport_handle h);

TC_API void tc_viewport_set_managed_by(tc_viewport_handle h, const char* pipeline_name);
TC_API const char* tc_viewport_get_managed_by(tc_viewport_handle h);

TC_API void tc_viewport_set_block_input_in_editor(tc_viewport_handle h, bool block);
TC_API bool tc_viewport_get_block_input_in_editor(tc_viewport_handle h);

// Input Manager
TC_API void tc_viewport_set_input_manager(tc_viewport_handle h, tc_input_manager* manager);
TC_API tc_input_manager* tc_viewport_get_input_manager(tc_viewport_handle h);

// Pixel Rect Calculation
TC_API void tc_viewport_update_pixel_rect(tc_viewport_handle h, int display_width, int display_height);

// Internal Entities
TC_API void tc_viewport_set_internal_entities(tc_viewport_handle h, tc_entity_handle ent);
TC_API tc_entity_handle tc_viewport_get_internal_entities(tc_viewport_handle h);
TC_API bool tc_viewport_has_internal_entities(tc_viewport_handle h);

// Display Linked List
TC_API tc_viewport_handle tc_viewport_get_display_next(tc_viewport_handle h);
TC_API tc_viewport_handle tc_viewport_get_display_prev(tc_viewport_handle h);
TC_API void tc_viewport_set_display_next(tc_viewport_handle h, tc_viewport_handle next);
TC_API void tc_viewport_set_display_prev(tc_viewport_handle h, tc_viewport_handle prev);

#ifdef __cplusplus
}
#endif

#endif // TC_VIEWPORT_H
