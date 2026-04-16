// styles.hpp - Color palettes and helper colormaps for tcplot.
//
// Port of tcplot/tcplot/styles.py. Colors are RGBA 0-1 tuples,
// represented as Color4 (4-float struct). Keeping structural parity
// with the Python module so callers can reason about them the same way.
#pragma once

#include <cstdint>

#include "tcplot/tcplot_api.h"

namespace tcplot {

struct Color4 {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color4() = default;
    constexpr Color4(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

namespace styles {

// Default color cycle (tab10-like palette). 10 entries.
TCPLOT_API const Color4* default_colors();
TCPLOT_API uint32_t default_colors_count();

// UI colors.
TCPLOT_API Color4 axis_color();
TCPLOT_API Color4 grid_color();
TCPLOT_API Color4 label_color();
TCPLOT_API Color4 bg_color();
TCPLOT_API Color4 plot_area_bg();

// Cycle through the default palette (index % count).
TCPLOT_API Color4 cycle_color(uint32_t index);

// Jet colormap: t in [0,1] → RGB. Alpha left to the caller.
// Returns a Color4 with a=1; caller may override.
TCPLOT_API Color4 jet(float t);

}  // namespace styles
}  // namespace tcplot
