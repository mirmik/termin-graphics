// tc_render_target.h - Render target: scene + camera + pipeline → FBO
#ifndef TC_RENDER_TARGET_H
#define TC_RENDER_TARGET_H

#include <tc_types.h>
#include "render/tc_render_target_pool.h"
#include "render/tc_pipeline_pool.h"
#include "core/tc_entity_pool.h"
#include "core/tc_scene_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

TC_API tc_render_target_handle tc_render_target_new(const char* name);
TC_API void tc_render_target_free(tc_render_target_handle h);
TC_API bool tc_render_target_alive(tc_render_target_handle h);

TC_API void tc_render_target_set_name(tc_render_target_handle h, const char* name);
TC_API const char* tc_render_target_get_name(tc_render_target_handle h);

TC_API void tc_render_target_set_width(tc_render_target_handle h, int width);
TC_API int tc_render_target_get_width(tc_render_target_handle h);

TC_API void tc_render_target_set_height(tc_render_target_handle h, int height);
TC_API int tc_render_target_get_height(tc_render_target_handle h);

TC_API void tc_render_target_set_scene(tc_render_target_handle h, tc_scene_handle scene);
TC_API tc_scene_handle tc_render_target_get_scene(tc_render_target_handle h);

TC_API void tc_render_target_set_camera(tc_render_target_handle h, tc_component* camera);
TC_API tc_component* tc_render_target_get_camera(tc_render_target_handle h);
TC_API tc_entity_handle tc_render_target_get_camera_entity(tc_render_target_handle h);

TC_API void tc_render_target_set_pipeline(tc_render_target_handle h, tc_pipeline_handle pipeline);
TC_API tc_pipeline_handle tc_render_target_get_pipeline(tc_render_target_handle h);

TC_API void tc_render_target_set_layer_mask(tc_render_target_handle h, uint64_t mask);
TC_API uint64_t tc_render_target_get_layer_mask(tc_render_target_handle h);

TC_API void tc_render_target_set_enabled(tc_render_target_handle h, bool enabled);
TC_API bool tc_render_target_get_enabled(tc_render_target_handle h);

TC_API void tc_render_target_set_locked(tc_render_target_handle h, bool locked);
TC_API bool tc_render_target_get_locked(tc_render_target_handle h);

#ifdef __cplusplus
}
#endif

#endif
