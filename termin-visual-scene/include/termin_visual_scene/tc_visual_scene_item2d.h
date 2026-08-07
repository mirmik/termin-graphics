#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <geom/tc_affine2.h>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_visual_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tc_visual_fill_rule2d {
    TC_VISUAL_FILL_RULE_NON_ZERO = 0,
    TC_VISUAL_FILL_RULE_EVEN_ODD = 1,
} tc_visual_fill_rule2d;

typedef enum tc_visual_path_verb2d {
    TC_VISUAL_PATH_MOVE_TO = 0,
    TC_VISUAL_PATH_LINE_TO = 1,
    TC_VISUAL_PATH_QUADRATIC_TO = 2,
    TC_VISUAL_PATH_CUBIC_TO = 3,
    TC_VISUAL_PATH_CLOSE = 4,
} tc_visual_path_verb2d;

typedef struct tc_visual_path2d_view {
    const tc_visual_path_verb2d* verbs;
    size_t verb_count;
    const tc_vec2f* points;
    size_t point_count;
} tc_visual_path2d_view;

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_is_valid(tc_visual_scene_handle scene, tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API const char* tc_visual_scene_item_type_name(tc_visual_scene_handle scene,
                                                                   tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_is_type(tc_visual_scene_handle scene, tc_graphic_item_handle item, const char* type_name);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_get_transform(tc_visual_scene_handle scene,
                                                                tc_graphic_item_handle item,
                                                                tc_affine2f* out_transform);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_transform(tc_visual_scene_handle scene, tc_graphic_item_handle item, tc_affine2f transform);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_get_visible(tc_visual_scene_handle scene, tc_graphic_item_handle item, bool* out_visible);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_visible(tc_visual_scene_handle scene, tc_graphic_item_handle item, bool visible);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_get_enabled(tc_visual_scene_handle scene, tc_graphic_item_handle item, bool* out_enabled);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_enabled(tc_visual_scene_handle scene, tc_graphic_item_handle item, bool enabled);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_get_opacity(tc_visual_scene_handle scene, tc_graphic_item_handle item, float* out_opacity);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_opacity(tc_visual_scene_handle scene, tc_graphic_item_handle item, float opacity);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_get_z_order(tc_visual_scene_handle scene, tc_graphic_item_handle item, int64_t* out_z_order);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_z_order(tc_visual_scene_handle scene, tc_graphic_item_handle item, int64_t z_order);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_scene_item_parent(tc_visual_scene_handle scene,
                                                                           tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API size_t tc_visual_scene_item_child_count(tc_visual_scene_handle scene,
                                                                tc_graphic_item_handle item);
TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_scene_item_child_at(tc_visual_scene_handle scene,
                                                                             tc_graphic_item_handle item,
                                                                             size_t index);
// An invalid parent detaches item to the scene root.
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_set_parent(tc_visual_scene_handle scene,
                                                             tc_graphic_item_handle item,
                                                             tc_graphic_item_handle parent,
                                                             size_t index);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_get_local_bounds(tc_visual_scene_handle scene,
                                                                   tc_graphic_item_handle item,
                                                                   tc_bounds2f* out_bounds);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_get_world_bounds(tc_visual_scene_handle scene,
                                                                   tc_graphic_item_handle item,
                                                                   tc_bounds2f* out_bounds);

TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_set_clip_path(tc_visual_scene_handle scene,
                                                                tc_graphic_item_handle item,
                                                                tc_visual_path2d_view path,
                                                                tc_visual_fill_rule2d rule);
TERMIN_VISUAL_SCENE_API bool
tc_visual_scene_item_set_clip_rect(tc_visual_scene_handle scene, tc_graphic_item_handle item, tc_rect2f rect);
TERMIN_VISUAL_SCENE_API bool tc_visual_scene_item_clear_clip(tc_visual_scene_handle scene, tc_graphic_item_handle item);

#ifdef __cplusplus
}
#endif
