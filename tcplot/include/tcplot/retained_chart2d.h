#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <termin_visual_scene/tc_builtin_items2d.h>

#include "tcplot/tc_plot_grid_item2d.h"
#include "tcplot/tc_plot_series_item2d.h"
#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct tc_retained_chart2d tc_retained_chart2d;

    typedef struct tc_chart2d_theme
    {
        tc_visual_color4f background_color;
        tc_visual_color4f plot_background_color;
        tc_visual_color4f foreground_color;
        tc_visual_color4f axis_color;
        tc_plot_grid_style2d grid_style;
        float axis_width_logical_px;
        float font_size_logical_px;
        float title_font_size_logical_px;
        float tick_length_logical_px;
        float gap_logical_px;
        float outer_padding_logical_px;
        float x_tick_spacing_logical_px;
        float y_tick_spacing_logical_px;
    } tc_chart2d_theme;

    typedef struct tc_chart2d_layout
    {
        tc_plot_rect2d viewport;
        tc_plot_rect2d plot_area;
        float pixel_scale;
        uint64_t revision;
    } tc_chart2d_layout;

    typedef enum tc_chart2d_part
    {
        TC_CHART2D_PART_ROOT = 0,
        TC_CHART2D_PART_BACKGROUND = 1,
        TC_CHART2D_PART_PLOT_AREA = 2,
        TC_CHART2D_PART_PLOT_BACKGROUND = 3,
        TC_CHART2D_PART_GRID = 4,
        TC_CHART2D_PART_SERIES_ROOT = 5,
        TC_CHART2D_PART_ANNOTATIONS_ROOT = 6,
        TC_CHART2D_PART_CHROME_ROOT = 7,
        TC_CHART2D_PART_X_AXIS_ROOT = 8,
        TC_CHART2D_PART_Y_AXIS_ROOT = 9,
        TC_CHART2D_PART_X_AXIS = 10,
        TC_CHART2D_PART_Y_AXIS = 11,
        TC_CHART2D_PART_X_TICK_LABELS_ROOT = 12,
        TC_CHART2D_PART_Y_TICK_LABELS_ROOT = 13,
        TC_CHART2D_PART_TITLE = 14,
        TC_CHART2D_PART_X_AXIS_LABEL = 15,
        TC_CHART2D_PART_Y_AXIS_LABEL = 16,
        TC_CHART2D_PART_LEGEND_ROOT = 17,
        TC_CHART2D_PART_OVERLAY_ROOT = 18,
    } tc_chart2d_part;

    TCPLOT_API tc_chart2d_theme tc_retained_chart2d_default_theme(void);

    // The owned form creates and destroys its public TcVisualScene2D. The
    // borrowed form contributes one independently removable chart subtree to a
    // caller-owned scene; it exists for composition and the native multi-chart
    // migration path.
    TCPLOT_API tc_retained_chart2d*
    tc_retained_chart2d_create(void* gpu_host,
                               tc_plot_rect2d viewport,
                               tc_plot_range2d range,
                               const char* font_uri,
                               float pixel_scale,
                               const tc_chart2d_theme* theme);
    TCPLOT_API tc_retained_chart2d*
    tc_retained_chart2d_create_in_scene(void* gpu_host,
                                        tc_visual_scene_handle scene,
                                        tc_plot_rect2d viewport,
                                        tc_plot_range2d range,
                                        const char* font_uri,
                                        float pixel_scale,
                                        const tc_chart2d_theme* theme);
    TCPLOT_API void tc_retained_chart2d_destroy(tc_retained_chart2d* chart);

    TCPLOT_API tc_visual_scene_handle
    tc_retained_chart2d_scene(const tc_retained_chart2d* chart);
    TCPLOT_API tc_plot_projection_handle2d
    tc_retained_chart2d_projection(const tc_retained_chart2d* chart);
    TCPLOT_API tc_graphic_item_handle tc_retained_chart2d_part_handle(
        const tc_retained_chart2d* chart, tc_chart2d_part part);

    TCPLOT_API bool tc_retained_chart2d_layout(const tc_retained_chart2d* chart,
                                               tc_chart2d_layout* out_layout);
    TCPLOT_API bool tc_retained_chart2d_range(const tc_retained_chart2d* chart,
                                              tc_plot_range2d* out_range);
    TCPLOT_API bool tc_retained_chart2d_theme(const tc_retained_chart2d* chart,
                                              tc_chart2d_theme* out_theme);

    // Mutations validate their complete input and perform one native layout
    // pass.
    TCPLOT_API bool tc_retained_chart2d_set_viewport(tc_retained_chart2d* chart,
                                                     tc_plot_rect2d viewport,
                                                     float pixel_scale);
    TCPLOT_API bool tc_retained_chart2d_set_range(tc_retained_chart2d* chart,
                                                  tc_plot_range2d range);
    TCPLOT_API bool tc_retained_chart2d_fit(tc_retained_chart2d* chart,
                                            double padding_fraction);
    TCPLOT_API bool tc_retained_chart2d_fit_x(tc_retained_chart2d* chart,
                                              double padding_fraction);
    TCPLOT_API bool tc_retained_chart2d_fit_y(tc_retained_chart2d* chart,
                                              double padding_fraction);

    // Frontend-neutral navigation in framebuffer coordinates. Middle-button
    // drag pans; wheel steps zoom around the cursor. The return value reports
    // whether the event was consumed by the plot area.
    TCPLOT_API bool tc_retained_chart2d_pointer_down(tc_retained_chart2d* chart,
                                                     float x,
                                                     float y,
                                                     int button);
    TCPLOT_API bool tc_retained_chart2d_pointer_move(tc_retained_chart2d* chart,
                                                     float x,
                                                     float y);
    TCPLOT_API bool tc_retained_chart2d_pointer_up(tc_retained_chart2d* chart,
                                                   float x,
                                                   float y,
                                                   int button);
    TCPLOT_API bool tc_retained_chart2d_wheel(
        tc_retained_chart2d* chart, float x, float y, float steps, bool x_only);
    TCPLOT_API void
    tc_retained_chart2d_cancel_interaction(tc_retained_chart2d* chart);

    TCPLOT_API bool
    tc_retained_chart2d_set_theme(tc_retained_chart2d* chart,
                                  const tc_chart2d_theme* theme);
    TCPLOT_API bool tc_retained_chart2d_set_title(tc_retained_chart2d* chart,
                                                  const char* utf8);
    TCPLOT_API bool
    tc_retained_chart2d_set_x_axis_label(tc_retained_chart2d* chart,
                                         const char* utf8);
    TCPLOT_API bool
    tc_retained_chart2d_set_y_axis_label(tc_retained_chart2d* chart,
                                         const char* utf8);

    // Replaceable standard leaves retain their semantic slot. Root/group parts
    // are intentionally not replaceable because they define composer topology.
    TCPLOT_API bool
    tc_retained_chart2d_replace_part(tc_retained_chart2d* chart,
                                     tc_chart2d_part part,
                                     tc_graphic_item_handle replacement);
    TCPLOT_API bool tc_retained_chart2d_remove_part(tc_retained_chart2d* chart,
                                                    tc_chart2d_part part);

    TCPLOT_API tc_graphic_item_handle
    tc_retained_chart2d_add_line(tc_retained_chart2d* chart,
                                 const double* x,
                                 const double* y,
                                 const double* scalar,
                                 size_t point_count,
                                 tc_plot_line_style_state2d style);
    TCPLOT_API tc_graphic_item_handle
    tc_retained_chart2d_add_scatter(tc_retained_chart2d* chart,
                                    const double* x,
                                    const double* y,
                                    size_t point_count,
                                    tc_plot_scatter_style_state2d style);
    TCPLOT_API bool
    tc_retained_chart2d_remove_series(tc_retained_chart2d* chart,
                                      tc_graphic_item_handle series);

#ifdef __cplusplus
}
#endif
