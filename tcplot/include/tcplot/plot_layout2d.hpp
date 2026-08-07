#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tcplot/plot_frame2d.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
    class FontAtlas;
}

namespace tcplot {

    struct PlotAxisTicks2D {
        std::vector<double> values;
        std::vector<std::string> labels;
    };

    struct PlotTicks2D {
        PlotAxisTicks2D x;
        PlotAxisTicks2D y;
    };

    struct PlotTextMetrics2D {
        float width = 0.0f;
        float height = 0.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float line_height = 0.0f;
    };

    // Builds ticks for one axis without formatting or allocating the unchanged
    // peer axis. Extent and spacing use the same physical/logical convention as
    // make_plot_ticks2d().
    TCPLOT_API std::optional<PlotAxisTicks2D> make_plot_axis_ticks2d(double minimum,
                                                                     double maximum,
                                                                     float extent_px,
                                                                     float spacing_logical_px,
                                                                     float pixel_scale = 1.0f,
                                                                     int minimum_ticks = 3);

    // Expands finite data bounds by padding_fraction. Degenerate axes receive a
    // stable unit base extent before padding, matching PlotEngine2D::fit().
    TCPLOT_API std::optional<PlotRange2D> fit_plot_range2d(PlotRange2D data_bounds, double padding_fraction = 0.05);

    // Empty data has no bounds. This overload maps it to the established neutral
    // [0, 1] range before applying the same fit policy.
    TCPLOT_API std::optional<PlotRange2D> fit_optional_plot_range2d(std::optional<PlotRange2D> data_bounds,
                                                                    double padding_fraction = 0.05);

    // Tick spacing is specified in logical pixels and scaled through the frame's
    // pixel_scale. Returned values and labels are detached value objects.
    TCPLOT_API std::optional<PlotTicks2D> make_plot_ticks2d(const PlotFrame2D& frame,
                                                            float x_spacing_logical_px = 80.0f,
                                                            float y_spacing_logical_px = 50.0f,
                                                            int minimum_ticks = 3);

    // font_size_logical_px is scaled by pixel_scale. The result is in physical
    // pixels and owns no font/backend pointer.
    TCPLOT_API std::optional<PlotTextMetrics2D> measure_plot_text2d(tgfx::FontAtlas& font,
                                                                    std::string_view text,
                                                                    float font_size_logical_px,
                                                                    float pixel_scale = 1.0f);

} // namespace tcplot
