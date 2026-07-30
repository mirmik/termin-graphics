#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tcplot/tc_plot_projection2d.h"
#include "tcplot/tcplot_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_plot_axis_ticks_desc2d {
  double minimum;
  double maximum;
  float extent_px;
  float spacing_logical_px;
  float pixel_scale;
  int32_t minimum_tick_count;
} tc_plot_axis_ticks_desc2d;

TCPLOT_API bool tc_plot_fit_range2d(tc_plot_range2d data_bounds,
                                    double padding_fraction,
                                    tc_plot_range2d *out_range);

// Returns the required value count. Passing out_values == NULL is a size
// query. Invalid input or insufficient capacity returns zero and logs an
// error.
TCPLOT_API size_t
tc_plot_axis_ticks2d_copy(const tc_plot_axis_ticks_desc2d *desc,
                          double *out_values, size_t capacity);

// Returns the required UTF-8 buffer size including the trailing NUL. Passing
// out_utf8 == NULL is a size query.
TCPLOT_API size_t tc_plot_format_tick2d(double value, char *out_utf8,
                                        size_t capacity);

#ifdef __cplusplus
}
#endif
