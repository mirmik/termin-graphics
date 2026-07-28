#include "tcplot/plot_frame2d.hpp"

#include <algorithm>
#include <cmath>

namespace tcplot {

PlotFrame2D::PlotFrame2D(
    PlotRect2D viewport,
    PlotRect2D plot_area,
    PlotRange2D range,
    PlotRect2D clip_rect,
    float pixel_scale)
    : viewport_(viewport),
      plot_area_(plot_area),
      range_(range),
      clip_rect_(clip_rect),
      pixel_scale_(
          std::isfinite(pixel_scale) && pixel_scale > 0.0f
              ? pixel_scale
              : 1.0f) {}

PlotPixelPoint2D PlotFrame2D::data_to_pixel(double x, double y) const {
    const double x_span = range_.x_span();
    const double y_span = range_.y_span();
    const double sx = x_span != 0.0
        ? (x - range_.x_min()) / x_span
        : 0.5;
    const double sy = y_span != 0.0
        ? (y - range_.y_min()) / y_span
        : 0.5;
    return {
        plot_area_.x() + static_cast<float>(sx) * plot_area_.width(),
        plot_area_.y()
            + (1.0f - static_cast<float>(sy)) * plot_area_.height(),
    };
}

PlotPoint2D PlotFrame2D::pixel_to_data(float x, float y) const {
    const double width = std::max<double>(plot_area_.width(), 1.0);
    const double height = std::max<double>(plot_area_.height(), 1.0);
    const double sx = (x - plot_area_.x()) / width;
    const double sy = 1.0 - (y - plot_area_.y()) / height;
    return {
        range_.x_min() + sx * range_.x_span(),
        range_.y_min() + sy * range_.y_span(),
    };
}

}  // namespace tcplot
