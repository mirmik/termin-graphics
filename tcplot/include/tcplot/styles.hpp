// styles.hpp - Color palettes and helper colormaps for tcplot.
//
// Port of tcplot/tcplot/styles.py. Colors are authored sRGB RGBA values.
#pragma once

#include <cstdint>

#include <termin/geom/color.hpp>

#include "tcplot/tcplot_api.h"

namespace tcplot {
    using termin::SrgbColor;

    enum class SurfaceColorMap {
        Jet,
        Viridis,
        Plasma,
        Grayscale,
        CoolWarm,
        Solid,
    };

    enum class LineStyle {
        Solid,
        Dash,
        Dot,
    };

    namespace styles {

        // Default color cycle (tab10-like palette). 10 entries.
        TCPLOT_API const SrgbColor* default_colors();
        TCPLOT_API uint32_t default_colors_count();

        // UI colors.
        TCPLOT_API SrgbColor axis_color();
        TCPLOT_API SrgbColor grid_color();
        TCPLOT_API SrgbColor label_color();
        TCPLOT_API SrgbColor bg_color();
        TCPLOT_API SrgbColor plot_area_bg();

        // Cycle through the default palette (index % count).
        TCPLOT_API SrgbColor cycle_color(uint32_t index);

        // Jet colormap: t in [0,1] → RGB. Alpha left to the caller.
        // Returns an SrgbColor with a=1; caller may override.
        TCPLOT_API SrgbColor jet(float t);
        TCPLOT_API SrgbColor colormap(SurfaceColorMap map, float t);

    } // namespace styles
} // namespace tcplot
