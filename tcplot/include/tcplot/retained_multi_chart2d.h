#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tcplot/retained_chart2d.h"
#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_retained_multi_chart2d tc_retained_multi_chart2d;

typedef struct tc_multi_chart_panel_handle2d {
    uint64_t multi_chart_id;
    uint32_t index;
    uint32_t generation;
} tc_multi_chart_panel_handle2d;

typedef struct tc_retained_multi_chart2d_state {
    tc_plot_rect2d viewport;
    tc_plot_range2d shared_range;
    float pixel_scale;
    float panel_height;
    float panel_gap;
    float scroll_offset;
    float total_virtual_height;
    float maximum_scroll_offset;
    size_t panel_count;
    uint64_t layout_revision;
} tc_retained_multi_chart2d_state;

TCPLOT_API tc_retained_multi_chart2d* tc_retained_multi_chart2d_create(void* gpu_host,
                                                                       tc_plot_rect2d viewport,
                                                                       tc_plot_range2d initial_range,
                                                                       size_t panel_count,
                                                                       float panel_height,
                                                                       float panel_gap,
                                                                       const char* font_uri,
                                                                       float pixel_scale,
                                                                       const tc_chart2d_theme* theme);
TCPLOT_API void tc_retained_multi_chart2d_destroy(tc_retained_multi_chart2d* chart);

TCPLOT_API tc_visual_scene_handle tc_retained_multi_chart2d_scene(const tc_retained_multi_chart2d* chart);
TCPLOT_API tc_graphic_item_handle tc_retained_multi_chart2d_root(const tc_retained_multi_chart2d* chart);
TCPLOT_API bool tc_retained_multi_chart2d_snapshot(const tc_retained_multi_chart2d* chart,
                                                   tc_retained_multi_chart2d_state* out_snapshot);

TCPLOT_API bool
tc_retained_multi_chart2d_set_viewport(tc_retained_multi_chart2d* chart, tc_plot_rect2d viewport, float pixel_scale);
TCPLOT_API bool tc_retained_multi_chart2d_set_panel_count(tc_retained_multi_chart2d* chart, size_t panel_count);
// A positive panel height enables virtual scrolling. Zero distributes all
// panels evenly over the viewport.
TCPLOT_API bool
tc_retained_multi_chart2d_set_panel_layout(tc_retained_multi_chart2d* chart, float panel_height, float panel_gap);
TCPLOT_API bool tc_retained_multi_chart2d_set_scroll_offset(tc_retained_multi_chart2d* chart, float scroll_offset);
TCPLOT_API bool tc_retained_multi_chart2d_set_shared_x(tc_retained_multi_chart2d* chart, double x_min, double x_max);
TCPLOT_API bool tc_retained_multi_chart2d_set_theme(tc_retained_multi_chart2d* chart, const tc_chart2d_theme* theme);

TCPLOT_API tc_multi_chart_panel_handle2d tc_retained_multi_chart2d_panel_at(const tc_retained_multi_chart2d* chart,
                                                                            size_t index);
TCPLOT_API bool tc_retained_multi_chart2d_panel_is_valid(const tc_retained_multi_chart2d* chart,
                                                         tc_multi_chart_panel_handle2d panel);
// The returned chart is borrowed and remains owned by the multi-chart.
TCPLOT_API tc_retained_chart2d* tc_retained_multi_chart2d_panel_chart(tc_retained_multi_chart2d* chart,
                                                                      tc_multi_chart_panel_handle2d panel);

#ifdef __cplusplus
}
#endif
