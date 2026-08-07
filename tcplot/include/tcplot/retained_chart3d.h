#ifndef TCPLOT_RETAINED_CHART3D_H
#define TCPLOT_RETAINED_CHART3D_H

#include <stddef.h>
#include <stdint.h>

#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_retained_chart3d tc_retained_chart3d;

typedef struct tc_plot_item3d_handle {
    uint64_t scene_id;
    uint32_t index;
    uint32_t generation;
} tc_plot_item3d_handle;

typedef enum tc_plot_item3d_kind {
    TC_PLOT_ITEM3D_INVALID = 0,
    TC_PLOT_ITEM3D_SURFACE = 1,
    TC_PLOT_ITEM3D_SCATTER = 2,
    TC_PLOT_ITEM3D_GRID = 3
} tc_plot_item3d_kind;

typedef enum tc_plot_colormap3d {
    TC_PLOT_COLORMAP3D_JET = 0,
    TC_PLOT_COLORMAP3D_VIRIDIS = 1,
    TC_PLOT_COLORMAP3D_PLASMA = 2,
    TC_PLOT_COLORMAP3D_GRAYSCALE = 3,
    TC_PLOT_COLORMAP3D_COOL_WARM = 4,
    TC_PLOT_COLORMAP3D_SOLID = 5
} tc_plot_colormap3d;

typedef struct tc_surface_item3d_style {
    float color_r;
    float color_g;
    float color_b;
    float color_a;
    uint32_t colormap;
    uint32_t colormap_reversed;
    uint32_t wireframe;
    uint32_t surface_grid_visible;
    uint32_t surface_grid_row_step;
    uint32_t surface_grid_col_step;
    float surface_grid_width_px;
    float surface_grid_r;
    float surface_grid_g;
    float surface_grid_b;
    float surface_grid_a;
} tc_surface_item3d_style;

typedef struct tc_scatter_item3d_style {
    float color_r;
    float color_g;
    float color_b;
    float color_a;
    float size;
} tc_scatter_item3d_style;

typedef struct tc_grid_item3d_style {
    float grid_r;
    float grid_g;
    float grid_b;
    float grid_a;
    float x_axis_r;
    float x_axis_g;
    float x_axis_b;
    float y_axis_r;
    float y_axis_g;
    float y_axis_b;
    float z_axis_r;
    float z_axis_g;
    float z_axis_b;
    uint32_t labels_visible;
} tc_grid_item3d_style;

typedef struct tc_plot_item3d_snapshot {
    uint32_t kind;
    uint64_t geometry_revision;
    uint64_t style_revision;
    uint64_t gpu_revision;
} tc_plot_item3d_snapshot;

typedef struct tc_orbit_camera3d_state {
    float target_x;
    float target_y;
    float target_z;
    float distance;
    float azimuth;
    float elevation;
    float fov_y;
    float near_clip;
    float far_clip;
} tc_orbit_camera3d_state;

TCPLOT_API tc_retained_chart3d* tc_retained_chart3d_create(void* gpu_host);
TCPLOT_API void tc_retained_chart3d_destroy(tc_retained_chart3d* chart);

TCPLOT_API uint64_t tc_retained_chart3d_scene_id(
    const tc_retained_chart3d* chart);
TCPLOT_API size_t tc_retained_chart3d_item_count(
    const tc_retained_chart3d* chart);
TCPLOT_API int tc_retained_chart3d_item_is_valid(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle item);
TCPLOT_API int tc_retained_chart3d_item_snapshot(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle item,
    tc_plot_item3d_snapshot* snapshot);
TCPLOT_API int tc_retained_chart3d_destroy_item(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle item);

TCPLOT_API tc_plot_item3d_handle tc_retained_chart3d_add_surface(
    tc_retained_chart3d* chart,
    const double* x,
    const double* y,
    const double* z,
    uint32_t rows,
    uint32_t columns,
    const tc_surface_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_surface_set_data(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    const double* x,
    const double* y,
    const double* z,
    uint32_t rows,
    uint32_t columns);
TCPLOT_API int tc_retained_chart3d_surface_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    const tc_surface_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_surface_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    tc_surface_item3d_style* style);

TCPLOT_API tc_plot_item3d_handle tc_retained_chart3d_add_scatter(
    tc_retained_chart3d* chart,
    const double* x,
    const double* y,
    const double* z,
    size_t count,
    const tc_scatter_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_scatter_set_data(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    const double* x,
    const double* y,
    const double* z,
    size_t count);
TCPLOT_API int tc_retained_chart3d_scatter_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    const tc_scatter_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_scatter_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    tc_scatter_item3d_style* style);

TCPLOT_API tc_plot_item3d_handle tc_retained_chart3d_add_grid(
    tc_retained_chart3d* chart,
    const tc_grid_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_grid_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid,
    const tc_grid_item3d_style* style);
TCPLOT_API int tc_retained_chart3d_grid_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid,
    tc_grid_item3d_style* style);
TCPLOT_API tc_plot_item3d_handle tc_retained_chart3d_grid_part(
    const tc_retained_chart3d* chart);
TCPLOT_API int tc_retained_chart3d_set_grid_part(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid);

TCPLOT_API void tc_retained_chart3d_set_axis_labels(
    tc_retained_chart3d* chart,
    const char* x_label,
    const char* y_label,
    const char* z_label);
TCPLOT_API int tc_retained_chart3d_set_surface_shading(
    tc_retained_chart3d* chart,
    int enabled,
    float strength);
TCPLOT_API int tc_retained_chart3d_set_light_direction(
    tc_retained_chart3d* chart,
    float x,
    float y,
    float z);
TCPLOT_API int tc_retained_chart3d_set_axis_scale(
    tc_retained_chart3d* chart,
    float x,
    float y,
    float z);
TCPLOT_API int tc_retained_chart3d_set_msaa_samples(
    tc_retained_chart3d* chart,
    int samples);
TCPLOT_API int tc_retained_chart3d_msaa_samples(
    const tc_retained_chart3d* chart);

TCPLOT_API int tc_retained_chart3d_get_camera(
    const tc_retained_chart3d* chart,
    tc_orbit_camera3d_state* state);
TCPLOT_API int tc_retained_chart3d_set_camera(
    tc_retained_chart3d* chart,
    const tc_orbit_camera3d_state* state);
TCPLOT_API void tc_retained_chart3d_fit_camera(
    tc_retained_chart3d* chart);
TCPLOT_API void tc_retained_chart3d_reset_camera(
    tc_retained_chart3d* chart);

TCPLOT_API int tc_retained_chart3d_pointer_down(
    tc_retained_chart3d* chart,
    float x,
    float y,
    int button);
TCPLOT_API void tc_retained_chart3d_pointer_move(
    tc_retained_chart3d* chart,
    float x,
    float y);
TCPLOT_API void tc_retained_chart3d_pointer_up(
    tc_retained_chart3d* chart,
    float x,
    float y,
    int button);
TCPLOT_API int tc_retained_chart3d_wheel(
    tc_retained_chart3d* chart,
    float x,
    float y,
    float delta);

TCPLOT_API uint32_t tc_retained_chart3d_render(
    tc_retained_chart3d* chart,
    int width,
    int height);
TCPLOT_API void tc_retained_chart3d_release_gpu(
    tc_retained_chart3d* chart);

#ifdef __cplusplus
}
#endif

#endif
