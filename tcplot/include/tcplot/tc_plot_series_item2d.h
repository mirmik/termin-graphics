#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <termin_visual_scene/tc_graphic_item.h>
#include <termin_visual_scene/tc_visual_scene.h>

#include "tcplot/tc_plot_projection2d.h"
#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_plot_color2d {
  float r;
  float g;
  float b;
  float a;
} tc_plot_color2d;

typedef enum tc_plot_line_style2d {
  TC_PLOT_LINE_STYLE_SOLID_2D = 0,
  TC_PLOT_LINE_STYLE_DASH_2D = 1,
  TC_PLOT_LINE_STYLE_DOT_2D = 2,
} tc_plot_line_style2d;

typedef enum tc_plot_colormap2d {
  TC_PLOT_COLORMAP_JET_2D = 0,
  TC_PLOT_COLORMAP_VIRIDIS_2D = 1,
  TC_PLOT_COLORMAP_PLASMA_2D = 2,
  TC_PLOT_COLORMAP_GRAYSCALE_2D = 3,
  TC_PLOT_COLORMAP_COOL_WARM_2D = 4,
  TC_PLOT_COLORMAP_SOLID_2D = 5,
} tc_plot_colormap2d;

typedef struct tc_plot_line_style_state2d {
  tc_plot_color2d color;
  float thickness_px;
  tc_plot_line_style2d line_style;
  float dash_px;
  float gap_px;
  tc_plot_colormap2d colormap;
  bool colormap_reversed;
  double scalar_min;
  double scalar_max;
} tc_plot_line_style_state2d;

typedef struct tc_plot_scatter_style_state2d {
  tc_plot_color2d color;
  float diameter_px;
} tc_plot_scatter_style_state2d;

typedef struct tc_plot_series_snapshot2d {
  tc_plot_projection_handle2d projection;
  size_t point_count;
  bool has_scalar;
  uint64_t revision;
} tc_plot_series_snapshot2d;

typedef struct tc_plot_nearest_point2d {
  size_t index;
  double data_x;
  double data_y;
  float pixel_x;
  float pixel_y;
  float distance_px;
} tc_plot_nearest_point2d;

TCPLOT_API tc_graphic_item_handle tc_plot_line_series_item2d_create(
    tc_visual_scene_handle owner_scene, tc_plot_projection_handle2d projection,
    const double *x, const double *y, const double *scalar, size_t point_count,
    tc_plot_line_style_state2d style);
TCPLOT_API tc_graphic_item_handle tc_plot_scatter_series_item2d_create(
    tc_visual_scene_handle owner_scene, tc_plot_projection_handle2d projection,
    const double *x, const double *y, size_t point_count,
    tc_plot_scatter_style_state2d style);

TCPLOT_API bool tc_plot_line_series_item2d_set_projection(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_projection_handle2d projection);
TCPLOT_API bool tc_plot_scatter_series_item2d_set_projection(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_projection_handle2d projection);
TCPLOT_API bool tc_plot_line_series_item2d_set_data(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    const double *x, const double *y, const double *scalar, size_t point_count);
TCPLOT_API bool tc_plot_line_series_item2d_append(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    const double *x, const double *y, const double *scalar, size_t point_count);
TCPLOT_API bool tc_plot_scatter_series_item2d_set_data(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    const double *x, const double *y, size_t point_count);
TCPLOT_API bool
tc_plot_line_series_item2d_set_style(tc_visual_scene_handle owner_scene,
                                     tc_graphic_item_handle item,
                                     tc_plot_line_style_state2d style);
TCPLOT_API bool
tc_plot_scatter_series_item2d_set_style(tc_visual_scene_handle owner_scene,
                                        tc_graphic_item_handle item,
                                        tc_plot_scatter_style_state2d style);

TCPLOT_API bool
tc_plot_line_series_item2d_snapshot(tc_visual_scene_handle owner_scene,
                                    tc_graphic_item_handle item,
                                    tc_plot_series_snapshot2d *out_snapshot,
                                    tc_plot_line_style_state2d *out_style);
TCPLOT_API bool tc_plot_scatter_series_item2d_snapshot(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_series_snapshot2d *out_snapshot,
    tc_plot_scatter_style_state2d *out_style);
TCPLOT_API size_t tc_plot_line_series_item2d_copy_data(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    double *out_x, double *out_y, double *out_scalar, size_t capacity);
TCPLOT_API size_t tc_plot_scatter_series_item2d_copy_data(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    double *out_x, double *out_y, size_t capacity);
TCPLOT_API bool
tc_plot_line_series_item2d_nearest(tc_visual_scene_handle owner_scene,
                                   tc_graphic_item_handle item, float pixel_x,
                                   float pixel_y, float max_distance_px,
                                   tc_plot_nearest_point2d *out_point);
TCPLOT_API bool tc_plot_scatter_series_item2d_nearest(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    float pixel_x, float pixel_y, float max_distance_px,
    tc_plot_nearest_point2d *out_point);

#ifdef __cplusplus
}
#endif
