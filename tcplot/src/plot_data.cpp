// plot_data.cpp

#include "tcplot/plot_data.hpp"

#include <algorithm>
#include <limits>

namespace tcplot {

LineSeries& PlotData::add_line(std::vector<double> x,
                                std::vector<double> y,
                                std::vector<double> z,
                                std::optional<Color4> color,
                                double thickness,
                                std::string label) {
    LineSeries s;
    s.x = std::move(x);
    s.y = std::move(y);
    s.z = std::move(z);
    s.color = color;
    s.thickness = thickness;
    s.label = std::move(label);
    lines.push_back(std::move(s));
    return lines.back();
}

ScatterSeries& PlotData::add_scatter(std::vector<double> x,
                                      std::vector<double> y,
                                      std::vector<double> z,
                                      std::optional<Color4> color,
                                      double size,
                                      std::string label) {
    ScatterSeries s;
    s.x = std::move(x);
    s.y = std::move(y);
    s.z = std::move(z);
    s.color = color;
    s.size = size;
    s.label = std::move(label);
    scatters.push_back(std::move(s));
    return scatters.back();
}

namespace {

void update_bounds(double& lo, double& hi,
                   const std::vector<double>& xs) {
    for (double v : xs) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
}

}  // namespace

std::array<double, 4> PlotData::data_bounds_2d() const {
    double x_min = std::numeric_limits<double>::infinity();
    double x_max = -std::numeric_limits<double>::infinity();
    double y_min = std::numeric_limits<double>::infinity();
    double y_max = -std::numeric_limits<double>::infinity();

    for (const auto& s : lines) {
        update_bounds(x_min, x_max, s.x);
        update_bounds(y_min, y_max, s.y);
    }
    for (const auto& s : scatters) {
        update_bounds(x_min, x_max, s.x);
        update_bounds(y_min, y_max, s.y);
    }

    if (x_min > x_max) {
        // No data — fall back to a neutral unit range, same as Python.
        return {0.0, 1.0, 0.0, 1.0};
    }
    return {x_min, x_max, y_min, y_max};
}

void PlotData::data_bounds_3d(double out_min[3], double out_max[3]) const {
    double lo[3] = {
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    double hi[3] = {
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    auto push_xyz = [&](const std::vector<double>& xs,
                        const std::vector<double>& ys,
                        const std::vector<double>& zs) {
        update_bounds(lo[0], hi[0], xs);
        update_bounds(lo[1], hi[1], ys);
        update_bounds(lo[2], hi[2], zs);
    };

    for (const auto& s : lines) {
        if (!s.z.empty()) push_xyz(s.x, s.y, s.z);
    }
    for (const auto& s : scatters) {
        if (!s.z.empty()) push_xyz(s.x, s.y, s.z);
    }
    for (const auto& s : surfaces) {
        push_xyz(s.X, s.Y, s.Z);
    }

    if (lo[0] > hi[0]) {
        // Empty — match Python's [-1,-1,-1]..[1,1,1] fallback.
        for (int i = 0; i < 3; ++i) {
            out_min[i] = -1.0;
            out_max[i] =  1.0;
        }
        return;
    }

    // Expand any zero-extent axis so downstream camera/tick math
    // doesn't produce NaN or empty intervals.
    constexpr double kMinExtent = 1e-6;
    for (int i = 0; i < 3; ++i) {
        if (hi[i] - lo[i] < kMinExtent) {
            lo[i] -= 0.5;
            hi[i] += 0.5;
        }
        out_min[i] = lo[i];
        out_max[i] = hi[i];
    }
}

}  // namespace tcplot
