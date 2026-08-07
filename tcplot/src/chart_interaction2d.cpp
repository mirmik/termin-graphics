#include "tcplot/chart_interaction2d.hpp"

#include <cmath>

namespace tcplot {

    namespace {

        constexpr int kMiddleMouseButton = 2;
        constexpr double kWheelStepFactor = 0.85;

        bool finite_pointer(float x, float y) {
            return std::isfinite(x) && std::isfinite(y);
        }

    } // namespace

    bool ChartInteraction2D::pointer_down(const PlotFrame2D& frame, float x, float y, int button) {
        if (button != kMiddleMouseButton || !finite_pointer(x, y) || !frame.contains_plot_pixel(x, y))
            return false;

        dragging_ = true;
        drag_button_ = button;
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_frame_ = frame;
        return true;
    }

    std::optional<PlotRange2D> ChartInteraction2D::pointer_move(float x, float y) const {
        if (!dragging_ || !finite_pointer(x, y))
            return std::nullopt;

        const PlotRect2D& area = drag_frame_.plot_area();
        if (area.width() <= 0.0f || area.height() <= 0.0f)
            return std::nullopt;

        const PlotRange2D& start = drag_frame_.range();
        const double dx = -(x - drag_start_x_) / area.width() * start.x_span();
        const double dy = (y - drag_start_y_) / area.height() * start.y_span();
        return PlotRange2D{start.x_min() + dx, start.x_max() + dx, start.y_min() + dy, start.y_max() + dy};
    }

    bool ChartInteraction2D::pointer_up(int button) {
        if (!dragging_ || button != drag_button_)
            return false;
        cancel();
        return true;
    }

    std::optional<PlotRange2D>
    ChartInteraction2D::wheel(const PlotFrame2D& frame, float x, float y, float steps, bool x_only) const {
        if (!finite_pointer(x, y) || !std::isfinite(steps) || steps == 0.0f || !frame.contains_plot_pixel(x, y))
            return std::nullopt;

        const double factor = std::pow(kWheelStepFactor, steps);
        if (!std::isfinite(factor) || factor <= 0.0)
            return std::nullopt;

        const PlotPoint2D center = frame.pixel_to_data(x, y);
        const PlotRange2D& range = frame.range();
        return PlotRange2D{center.x + (range.x_min() - center.x) * factor,
                           center.x + (range.x_max() - center.x) * factor,
                           x_only ? range.y_min() : center.y + (range.y_min() - center.y) * factor,
                           x_only ? range.y_max() : center.y + (range.y_max() - center.y) * factor};
    }

    void ChartInteraction2D::cancel() {
        dragging_ = false;
        drag_button_ = -1;
    }

} // namespace tcplot
