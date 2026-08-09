#pragma once

#include "termin_visual_scene/tc_visual_scene_item2d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_visual_color4f {
    float r, g, b, a;
} tc_visual_color4f;

typedef enum tc_visual_stroke_join2d {
    TC_VISUAL_STROKE_JOIN_MITER = 0,
    TC_VISUAL_STROKE_JOIN_ROUND = 1,
    TC_VISUAL_STROKE_JOIN_BEVEL = 2,
} tc_visual_stroke_join2d;

typedef enum tc_visual_stroke_cap2d {
    TC_VISUAL_STROKE_CAP_BUTT = 0,
    TC_VISUAL_STROKE_CAP_ROUND = 1,
    TC_VISUAL_STROKE_CAP_SQUARE = 2,
} tc_visual_stroke_cap2d;

typedef struct tc_visual_fill_paint2d {
    tc_visual_color4f color;
    tc_visual_fill_rule2d rule;
} tc_visual_fill_paint2d;

typedef struct tc_visual_stroke_paint2d {
    tc_visual_color4f color;
    float width;
    tc_visual_stroke_join2d join;
    tc_visual_stroke_cap2d cap;
    float miter_limit;
    const float* dash_pattern;
    size_t dash_count;
    float dash_offset;
} tc_visual_stroke_paint2d;

typedef enum tc_visual_text_anchor2d {
    TC_VISUAL_TEXT_ANCHOR_LEFT = 0,
    TC_VISUAL_TEXT_ANCHOR_CENTER = 1,
    TC_VISUAL_TEXT_ANCHOR_RIGHT = 2,
} tc_visual_text_anchor2d;

typedef enum tc_visual_texture_sampling2d {
    TC_VISUAL_TEXTURE_SAMPLING_LINEAR = 0,
    TC_VISUAL_TEXTURE_SAMPLING_NEAREST = 1,
} tc_visual_texture_sampling2d;

typedef struct tc_visual_text_desc2d {
    const char* text;
    const char* font_uri;
    tc_vec2f origin;
    float size_px;
    tc_visual_color4f color;
    tc_visual_text_anchor2d anchor;
    tc_bounds2f layout_bounds;
    bool has_coverage_gamma;
    float coverage_gamma;
} tc_visual_text_desc2d;

typedef struct tc_visual_image_desc2d {
    const char* image_uri;
    tc_rect2f rect;
    tc_rect2f uv;
    tc_visual_color4f tint;
    tc_visual_texture_sampling2d sampling;
} tc_visual_image_desc2d;

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_group_item2d_create(tc_visual_scene_handle scene,
                                                                             tc_graphic_item_handle parent);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_rect_item2d_create(tc_visual_scene_handle scene,
                                                                            tc_graphic_item_handle parent,
                                                                            tc_rect2f rect,
                                                                            tc_visual_fill_paint2d fill,
                                                                            const tc_visual_stroke_paint2d* stroke);
TERMIN_VISUAL_SCENE_API bool tc_visual_rect_item2d_set(tc_visual_scene_handle scene,
                                                       tc_graphic_item_handle item,
                                                       tc_rect2f rect,
                                                       tc_visual_fill_paint2d fill,
                                                       const tc_visual_stroke_paint2d* stroke);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_path_item2d_create(tc_visual_scene_handle scene,
                                                                            tc_graphic_item_handle parent,
                                                                            tc_visual_path2d_view path,
                                                                            const tc_visual_fill_paint2d* fill,
                                                                            const tc_visual_stroke_paint2d* stroke);
TERMIN_VISUAL_SCENE_API bool tc_visual_path_item2d_set(tc_visual_scene_handle scene,
                                                       tc_graphic_item_handle item,
                                                       tc_visual_path2d_view path,
                                                       const tc_visual_fill_paint2d* fill,
                                                       const tc_visual_stroke_paint2d* stroke);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_text_item2d_create(tc_visual_scene_handle scene,
                                                                            tc_graphic_item_handle parent,
                                                                            const tc_visual_text_desc2d* desc);
TERMIN_VISUAL_SCENE_API bool
tc_visual_text_item2d_set(tc_visual_scene_handle scene, tc_graphic_item_handle item, const tc_visual_text_desc2d* desc);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_image_item2d_create(tc_visual_scene_handle scene,
                                                                             tc_graphic_item_handle parent,
                                                                             const tc_visual_image_desc2d* desc);
TERMIN_VISUAL_SCENE_API bool tc_visual_image_item2d_set(tc_visual_scene_handle scene,
                                                        tc_graphic_item_handle item,
                                                        const tc_visual_image_desc2d* desc);

TERMIN_VISUAL_SCENE_API tc_graphic_item_handle tc_visual_hit_region_item2d_create(tc_visual_scene_handle scene,
                                                                                  tc_graphic_item_handle parent,
                                                                                  tc_visual_path2d_view path,
                                                                                  tc_visual_fill_rule2d rule);
TERMIN_VISUAL_SCENE_API bool tc_visual_hit_region_item2d_set(tc_visual_scene_handle scene,
                                                             tc_graphic_item_handle item,
                                                             tc_visual_path2d_view path,
                                                             tc_visual_fill_rule2d rule);

#ifdef __cplusplus
}
#endif
