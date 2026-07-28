#include <array>
#include <cassert>
#include <cmath>

#include "tcplot/engine2d.hpp"
#include "tcplot/plot_frame2d.hpp"

namespace {

bool near(double lhs, double rhs, double epsilon = 1e-6) {
    return std::abs(lhs - rhs) <= epsilon;
}

}  // namespace

int main() {
    using namespace tcplot;

    const PlotFrame2D frame(
        PlotRect2D(10.0f, 20.0f, 400.0f, 300.0f),
        PlotRect2D(60.0f, 40.0f, 300.0f, 200.0f),
        PlotRange2D(-10.0, 30.0, -5.0, 15.0),
        PlotRect2D(60.0f, 40.0f, 300.0f, 200.0f),
        2.0f);

    const PlotPixelPoint2D lower_left = frame.data_to_pixel(-10.0, -5.0);
    assert(near(lower_left.x, 60.0));
    assert(near(lower_left.y, 240.0));

    const PlotPixelPoint2D upper_right = frame.data_to_pixel(30.0, 15.0);
    assert(near(upper_right.x, 360.0));
    assert(near(upper_right.y, 40.0));

    const PlotPixelPoint2D center = frame.data_to_pixel(10.0, 5.0);
    const PlotPoint2D round_trip = frame.pixel_to_data(center.x, center.y);
    assert(near(round_trip.x, 10.0));
    assert(near(round_trip.y, 5.0));
    assert(frame.contains_plot_pixel(center.x, center.y));
    assert(!frame.contains_plot_pixel(59.0f, center.y));
    assert(near(frame.pixel_scale(), 2.0));

    const std::array expected_order{
        PlotRenderPhase2D::Background,
        PlotRenderPhase2D::AnnotationUnderlay,
        PlotRenderPhase2D::Series,
        PlotRenderPhase2D::AnnotationOverlay,
        PlotRenderPhase2D::UnclippedChrome,
    };
    assert(kPlotRenderPhaseOrder2D == expected_order);

    PlotEngine2D left;
    left.set_viewport(5.0f, 7.0f, 640.0f, 480.0f);
    left.set_view(-4.0, 8.0, -2.0, 6.0);
    left.set_pixel_scale(1.5f);
    const PlotFrame2D old_snapshot = left.plot_frame();

    left.set_viewport(100.0f, 200.0f, 320.0f, 240.0f);
    left.set_view(20.0, 40.0, 30.0, 50.0);
    const PlotFrame2D new_snapshot = left.plot_frame();

    assert(near(old_snapshot.viewport().x(), 5.0));
    assert(near(old_snapshot.range().x_min(), -4.0));
    assert(near(old_snapshot.pixel_scale(), 1.5));
    assert(near(new_snapshot.viewport().x(), 100.0));
    assert(near(new_snapshot.range().x_min(), 20.0));

    PlotEngine2D right;
    right.set_viewport(0.0f, 0.0f, 640.0f, 480.0f);
    right.set_view(-100.0, 100.0, -7.0, 7.0);
    left.set_view_x(-3.0, 9.0);
    right.set_view_x(-3.0, 9.0);
    const PlotFrame2D left_frame = left.plot_frame();
    const PlotFrame2D right_frame = right.plot_frame();
    const PlotRange2D& left_range = left_frame.range();
    const PlotRange2D& right_range = right_frame.range();
    assert(near(left_range.x_min(), right_range.x_min()));
    assert(near(left_range.x_max(), right_range.x_max()));
    assert(!near(left_range.y_min(), right_range.y_min()));

    return 0;
}
