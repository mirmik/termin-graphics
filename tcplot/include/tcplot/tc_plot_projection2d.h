#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <termin_visual_scene/tc_visual_scene.h>

#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_plot_projection_handle2d {
  uint64_t scene_id;
  uint32_t index;
  uint32_t generation;
} tc_plot_projection_handle2d;

static inline tc_plot_projection_handle2d
tc_plot_projection_handle2d_invalid(void) {
  const tc_plot_projection_handle2d result = {
      0,
      UINT32_MAX,
      0,
  };
  return result;
}

static inline bool
tc_plot_projection_handle2d_is_invalid(tc_plot_projection_handle2d handle) {
  return handle.scene_id == 0 || handle.index == UINT32_MAX;
}

static inline bool
tc_plot_projection_handle2d_eq(tc_plot_projection_handle2d left,
                               tc_plot_projection_handle2d right) {
  return left.scene_id == right.scene_id && left.index == right.index &&
         left.generation == right.generation;
}

typedef struct tc_plot_rect2d {
  float x;
  float y;
  float width;
  float height;
} tc_plot_rect2d;

typedef struct tc_plot_range2d {
  double x_min;
  double x_max;
  double y_min;
  double y_max;
} tc_plot_range2d;

// Complete compact input for data-to-visual projection. Coordinates use the
// current tcplot convention: visual X grows right and visual Y grows down.
typedef struct tc_plot_projection_desc2d {
  tc_plot_rect2d viewport;
  tc_plot_rect2d plot_area;
  tc_plot_range2d range;
  tc_plot_rect2d clip_rect;
  float pixel_scale;
} tc_plot_projection_desc2d;

typedef struct tc_plot_projection_state2d {
  tc_plot_projection_desc2d projection;
  uint64_t revision;
} tc_plot_projection_state2d;

typedef struct tc_plot_point2d {
  double x;
  double y;
} tc_plot_point2d;

typedef struct tc_plot_visual_point2d {
  float x;
  float y;
} tc_plot_visual_point2d;

// Projection objects are thread-confined, explicitly owned by the caller and
// associated with exactly one live TcVisualScene. Destroy them before or
// immediately after destroying the owner scene.
TCPLOT_API tc_plot_projection_handle2d tc_plot_projection2d_create(
    tc_visual_scene_handle owner_scene, const tc_plot_projection_desc2d *desc);
TCPLOT_API bool
tc_plot_projection2d_destroy(tc_plot_projection_handle2d handle);
TCPLOT_API bool
tc_plot_projection2d_is_valid(tc_plot_projection_handle2d handle);
TCPLOT_API bool
tc_plot_projection2d_matches_scene(tc_plot_projection_handle2d handle,
                                   tc_visual_scene_handle scene);
TCPLOT_API tc_visual_scene_handle
tc_plot_projection2d_owner_scene(tc_plot_projection_handle2d handle);

// Update is transactional: invalid input leaves the previous state and
// revision unchanged. Each successful update increments revision exactly once.
TCPLOT_API bool
tc_plot_projection2d_update(tc_plot_projection_handle2d handle,
                            const tc_plot_projection_desc2d *desc);
TCPLOT_API bool
tc_plot_projection2d_snapshot(tc_plot_projection_handle2d handle,
                              tc_plot_projection_state2d *out_state);

TCPLOT_API bool
tc_plot_projection_desc2d_is_valid(const tc_plot_projection_desc2d *desc);
TCPLOT_API bool tc_plot_projection_state2d_data_to_visual(
    const tc_plot_projection_state2d *state, tc_plot_point2d data,
    tc_plot_visual_point2d *out_visual);
TCPLOT_API bool tc_plot_projection_state2d_visual_to_data(
    const tc_plot_projection_state2d *state, tc_plot_visual_point2d visual,
    tc_plot_point2d *out_data);

#ifdef __cplusplus
}
#endif
