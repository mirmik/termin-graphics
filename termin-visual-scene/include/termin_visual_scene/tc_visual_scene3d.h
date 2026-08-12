#pragma once
#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_visual_item3d.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tcbase/tc_binding_types.h>
#ifdef __cplusplus
extern "C" {
#endif
TERMIN_VISUAL_SCENE_API tc_visual_scene3d_handle tc_visual_scene3d_create(void);
TERMIN_VISUAL_SCENE_API void tc_visual_scene3d_destroy(tc_visual_scene3d_handle);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene3d_is_valid(tc_visual_scene3d_handle);
TERMIN_VISUAL_SCENE_API uint64_t tc_visual_scene3d_id(tc_visual_scene3d_handle);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene3d_item_count(tc_visual_scene3d_handle);
TERMIN_VISUAL_SCENE_API uint64_t tc_visual_scene3d_order_revision(tc_visual_scene3d_handle);
TERMIN_VISUAL_SCENE_API tc_visual_item3d_handle tc_visual_scene3d_adopt_item(tc_visual_scene3d_handle,
                                                                             tc_visual_item3d*,
                                                                             tc_visual_item3d_deleter);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene3d_replace_item(tc_visual_scene3d_handle,
                                                            tc_visual_item3d_handle,
                                                            tc_visual_item3d*,
                                                            tc_visual_item3d_deleter);
TERMIN_VISUAL_SCENE_API tc_visual_item3d* tc_visual_scene3d_resolve_item(tc_visual_scene3d_handle,
                                                                         tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API const tc_visual_item3d* tc_visual_scene3d_resolve_item_const(tc_visual_scene3d_handle,
                                                                                     tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene3d_copy_items(tc_visual_scene3d_handle, tc_visual_item3d**, size_t);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene3d_copy_item_handles(tc_visual_scene3d_handle,
                                                                   tc_visual_item3d_handle*,
                                                                   size_t);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene3d_destroy_item(tc_visual_scene3d_handle, tc_visual_item3d_handle);
TERMIN_VISUAL_SCENE_API void tc_visual_scene3d_clear(tc_visual_scene3d_handle);
// Linear nearest-hit query. Items own geometry and may omit hit_test entirely;
// local_bounds is never used as a mandatory broad phase.
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene3d_hit_test(tc_visual_scene3d_handle, tc_ray3 world_ray, tc_visual_hit_result3d* out_result);
#ifdef __cplusplus
}
#endif
