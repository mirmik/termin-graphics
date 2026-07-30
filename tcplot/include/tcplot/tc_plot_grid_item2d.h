#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <termin_visual_scene/tc_visual_scene.h>

#include "tcplot/tc_plot_projection2d.h"
#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_plot_grid_style2d {
  float r;
  float g;
  float b;
  float a;
  float width_px;
} tc_plot_grid_style2d;

typedef struct tc_plot_grid_item_snapshot2d {
  tc_plot_projection_handle2d projection;
  tc_plot_grid_style2d style;
  size_t x_tick_count;
  size_t y_tick_count;
  uint64_t revision;
} tc_plot_grid_item_snapshot2d;

// Creates and adopts one tcplot-owned GraphicItem into owner_scene. The scene
// owns the returned item and destroys it through the ordinary GraphicItem
// deleter. Tick arrays are copied and may be released after this call.
TCPLOT_API tc_graphic_item_handle tc_plot_grid_item2d_create(
    tc_visual_scene_handle owner_scene, tc_plot_projection_handle2d projection,
    const double *x_ticks, size_t x_tick_count, const double *y_ticks,
    size_t y_tick_count, tc_plot_grid_style2d style);

TCPLOT_API bool
tc_plot_grid_item2d_set_projection(tc_visual_scene_handle owner_scene,
                                   tc_graphic_item_handle item,
                                   tc_plot_projection_handle2d projection);
TCPLOT_API bool
tc_plot_grid_item2d_set_ticks(tc_visual_scene_handle owner_scene,
                              tc_graphic_item_handle item,
                              const double *x_ticks, size_t x_tick_count,
                              const double *y_ticks, size_t y_tick_count);
TCPLOT_API bool
tc_plot_grid_item2d_set_style(tc_visual_scene_handle owner_scene,
                              tc_graphic_item_handle item,
                              tc_plot_grid_style2d style);
TCPLOT_API bool
tc_plot_grid_item2d_snapshot(tc_visual_scene_handle owner_scene,
                             tc_graphic_item_handle item,
                             tc_plot_grid_item_snapshot2d *out_snapshot);

// Returns the total tick count. When both output arrays are null this is a
// size query. Otherwise both capacities must cover their corresponding axes.
TCPLOT_API size_t tc_plot_grid_item2d_copy_ticks(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    double *out_x_ticks, size_t x_capacity, double *out_y_ticks,
    size_t y_capacity);

#ifdef __cplusplus
}
#endif
