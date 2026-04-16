// plot_data.hpp - Data models for 2D/3D plot series.
//
// Port of tcplot/tcplot/data.py. Series carry std::vector<double>
// rather than numpy arrays — a 1:1 translation of numpy.float64 arrays.
// Color is std::optional: an unset color means "pick from the default
// palette cycle" (resolved by the engine at render time).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tcplot {

struct TCPLOT_API LineSeries {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> z;  // empty for 2D series
    std::optional<Color4> color;
    double thickness = 1.5;
    std::string label;
};

struct TCPLOT_API ScatterSeries {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> z;  // empty for 2D series
    std::optional<Color4> color;
    double size = 4.0;
    std::string label;
};

// Grid-sampled surface. X/Y/Z are row-major flats of shape (rows, cols).
// X[j * cols + i] is the x-coordinate of grid cell (j, i); same for Y/Z.
struct TCPLOT_API SurfaceSeries {
    std::vector<double> X;
    std::vector<double> Y;
    std::vector<double> Z;
    uint32_t rows = 0;
    uint32_t cols = 0;
    std::optional<Color4> color;
    bool wireframe = false;
    std::string label;
};

class TCPLOT_API PlotData {
public:
    std::vector<LineSeries> lines;
    std::vector<ScatterSeries> scatters;
    std::vector<SurfaceSeries> surfaces;

    std::string title;
    std::string x_label;
    std::string y_label;

    // Add-line helper: copies x/y/z vectors into a new LineSeries and
    // returns a reference to it (valid until the next modification).
    // If `color` is nullopt, the engine will pick a palette color at
    // render time based on the series index.
    LineSeries& add_line(std::vector<double> x,
                         std::vector<double> y,
                         std::vector<double> z = {},
                         std::optional<Color4> color = std::nullopt,
                         double thickness = 1.5,
                         std::string label = "");

    ScatterSeries& add_scatter(std::vector<double> x,
                                std::vector<double> y,
                                std::vector<double> z = {},
                                std::optional<Color4> color = std::nullopt,
                                double size = 4.0,
                                std::string label = "");

    // 2D bounds across lines and scatter series (z ignored).
    // Returns {x_min, x_max, y_min, y_max}. For an empty plot: {0, 1, 0, 1}.
    std::array<double, 4> data_bounds_2d() const;

    // 3D bounds across lines/scatter/surface series. Returns
    // {min, max} as two 3-element arrays. For an empty plot or when
    // any axis has zero extent, the bound is expanded by ±0.5 on that
    // axis so the downstream camera `fit_bounds` doesn't produce NaN.
    void data_bounds_3d(double out_min[3], double out_max[3]) const;
};

}  // namespace tcplot
