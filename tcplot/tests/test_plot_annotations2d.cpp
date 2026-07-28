#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <stdexcept>

#include "tcplot/engine2d.hpp"
#include "tcplot/plot_annotations2d.hpp"

namespace {

using namespace tcplot;
using namespace termin::visual;

bool near(double lhs, double rhs, double epsilon = 1e-5) {
    return std::abs(lhs - rhs) <= epsilon;
}

bool same(GraphicItemHandle lhs, GraphicItemHandle rhs) {
    return lhs.scene_id == rhs.scene_id
        && lhs.index == rhs.index
        && lhs.generation == rhs.generation;
}

PlotAnnotationVisual2D target_visual(
    PlotAnnotationPhase2D phase = PlotAnnotationPhase2D::Overlay,
    PlotAnnotationClip2D clip = PlotAnnotationClip2D::PlotArea) {
    PlotAnnotationVisual2D visual;
    visual.payload = RectItem2D{
        {-5.0f, -5.0f, 10.0f, 10.0f},
        tgfx::FillPaint{},
        std::nullopt,
    };
    visual.phase = phase;
    visual.clip = clip;
    return visual;
}

PlotPixelPoint2D projected(
    const PlotAnnotationLayer2D& layer,
    PlotAnnotationHandle handle) {
    const auto value = layer.snapshot(handle);
    assert(value && value->projected_anchor);
    return *value->projected_anchor;
}

}  // namespace

int main() {
    const PlotFrame2D frame(
        PlotRect2D(10.0f, 20.0f, 400.0f, 300.0f),
        PlotRect2D(60.0f, 40.0f, 300.0f, 200.0f),
        PlotRange2D(0.0, 10.0, 0.0, 20.0),
        PlotRect2D(60.0f, 40.0f, 300.0f, 200.0f),
        1.0f);
    PlotData data;
    data.add_line({1.0, 4.0, 8.0}, {2.0, 10.0, 16.0});
    data.add_scatter({2.0, 7.0}, {4.0, 18.0});

    PlotAnnotationLayer2D layer;
    const auto data_handle = layer.create({
        DataAnchor2D{5.0, 10.0},
        {target_visual()},
    });
    const auto line_handle = layer.create({
        SeriesPointRef2D{PlotSeriesKind2D::Line, 0, 1},
        {target_visual(PlotAnnotationPhase2D::Underlay)},
    });
    const auto scatter_handle = layer.create({
        SeriesPointRef2D{PlotSeriesKind2D::Scatter, 0, 1},
        {target_visual()},
    });
    const auto axes_handle = layer.create({
        AxesFractionAnchor2D{0.25, 0.75},
        {target_visual()},
    });
    const auto viewport_handle = layer.create({
        ViewportPixelAnchor2D{12.0f, 18.0f},
        {target_visual(
            PlotAnnotationPhase2D::Chrome,
            PlotAnnotationClip2D::Viewport)},
    });
    assert(data_handle && line_handle && scatter_handle
           && axes_handle && viewport_handle);

    layer.project(frame, data);
    assert(layer.visual_scene(
        PlotAnnotationPhase2D::Underlay,
        PlotAnnotationClip2D::PlotArea).size() == 1);
    assert(layer.visual_scene(
        PlotAnnotationPhase2D::Chrome,
        PlotAnnotationClip2D::Viewport).size() == 1);
    assert(near(projected(layer, *data_handle).x, 210.0));
    assert(near(projected(layer, *data_handle).y, 140.0));
    assert(near(projected(layer, *line_handle).x, 180.0));
    assert(near(projected(layer, *line_handle).y, 140.0));
    assert(near(projected(layer, *scatter_handle).x, 270.0));
    assert(near(projected(layer, *scatter_handle).y, 60.0));
    assert(near(projected(layer, *axes_handle).x, 135.0));
    assert(near(projected(layer, *axes_handle).y, 90.0));
    assert(near(projected(layer, *viewport_handle).x, 22.0));
    assert(near(projected(layer, *viewport_handle).y, 38.0));

    const auto before_pan = layer.snapshot(*data_handle);
    assert(before_pan && before_pan->projected_graphics.size() == 1);
    const GraphicItemHandle stable_item =
        before_pan->projected_graphics.front().item;

    const PlotFrame2D panned(
        frame.viewport(),
        frame.plot_area(),
        PlotRange2D(5.0, 15.0, 10.0, 30.0),
        frame.clip_rect(),
        frame.pixel_scale());
    layer.project(panned, data);
    const auto after_pan = layer.snapshot(*data_handle);
    assert(after_pan && after_pan->projected_graphics.size() == 1);
    assert(same(stable_item, after_pan->projected_graphics.front().item));
    assert(near(after_pan->projected_anchor->x, 60.0));
    assert(near(after_pan->projected_anchor->y, 240.0));

    const auto visual_snapshot =
        layer.visual_scene(
            PlotAnnotationPhase2D::Overlay,
            PlotAnnotationClip2D::PlotArea)
        .snapshot(stable_item);
    assert(visual_snapshot);
    const auto world_origin =
        visual_snapshot->world_transform.transform_point({0.0f, 0.0f});
    assert(near(world_origin.x, 60.0));
    assert(near(world_origin.y, 240.0));

    assert(layer.set_snap_hook(*data_handle, [](const PlotPoint2D& value) {
        return PlotPoint2D{
            std::round(value.x),
            std::round(value.y * 2.0) / 2.0,
        };
    }));
    const auto snapped =
        layer.snap_data(*data_handle, {2.6, 3.26});
    assert(snapped);
    assert(near(snapped->x, 3.0));
    assert(near(snapped->y, 3.5));

    int actions = 0;
    assert(layer.set_action_handler(
        *data_handle,
        [&](const PlotAnnotationAction2D& action) {
            assert(action.annotation == *data_handle);
            assert(action.visual_index == 0);
            assert(action.action == "activate");
            ++actions;
        }));
    assert(layer.route_pointer(panned, {
        7, PointerEventKind2D::Down, {60.0f, 240.0f}, 0}));
    assert(layer.route_pointer(panned, {
        7, PointerEventKind2D::Up, {60.0f, 240.0f}, 0}));
    assert(actions == 1);
    assert(!layer.route_pointer(panned, {
        8, PointerEventKind2D::Down, {400.0f, 300.0f}, 0}));

    // PlotArea policy blocks interaction outside the plot even when the
    // projected local geometry would otherwise hit.
    const auto clipped = layer.create({
        ViewportPixelAnchor2D{0.0f, 0.0f},
        {target_visual(
            PlotAnnotationPhase2D::Overlay,
            PlotAnnotationClip2D::PlotArea)},
    });
    const auto unclipped = layer.create({
        ViewportPixelAnchor2D{0.0f, 0.0f},
        {target_visual(
            PlotAnnotationPhase2D::Chrome,
            PlotAnnotationClip2D::Viewport)},
    });
    assert(clipped && unclipped);
    layer.project(frame, data);
    assert(layer.hit_test(frame, 10.0f, 20.0f));
    assert(layer.destroy(*unclipped));
    assert(!layer.hit_test(frame, 10.0f, 20.0f));

    // Invalid series references drop only projected graphics. Restoring the
    // source recreates graphics without changing the semantic handle.
    data.lines[0].x.resize(1);
    data.lines[0].y.resize(1);
    layer.project(frame, data);
    const auto missing_series_point = layer.snapshot(*line_handle);
    assert(missing_series_point);
    assert(!missing_series_point->projected_anchor);
    assert(missing_series_point->projected_graphics.empty());
    data.lines[0].x = {1.0, 4.0};
    data.lines[0].y = {2.0, 10.0};
    layer.project(frame, data);
    assert(layer.snapshot(*line_handle)->projected_graphics.size() == 1);

    const PlotFrame2D resized(
        PlotRect2D(30.0f, 50.0f, 800.0f, 600.0f),
        PlotRect2D(100.0f, 90.0f, 650.0f, 440.0f),
        frame.range(),
        PlotRect2D(100.0f, 90.0f, 650.0f, 440.0f),
        2.0f);
    const GraphicItemHandle before_resize =
        layer.snapshot(*line_handle)->projected_graphics.front().item;
    layer.project(resized, data);
    const auto after_resize = layer.snapshot(*line_handle);
    assert(after_resize && after_resize->projected_anchor);
    assert(same(
        before_resize, after_resize->projected_graphics.front().item));
    assert(near(after_resize->projected_anchor->x, 360.0));
    assert(near(after_resize->projected_anchor->y, 310.0));

    const PlotAnnotationHandle old = *axes_handle;
    assert(layer.destroy(old));
    assert(!layer.snapshot(old));
    const auto replacement = layer.create({
        AxesFractionAnchor2D{0.5, 0.5},
        {target_visual()},
    });
    assert(replacement);
    assert(replacement->index == old.index);
    assert(replacement->generation != old.generation);

    bool logged_exception_path = false;
    assert(layer.set_snap_hook(*replacement, [&](const PlotPoint2D&) {
        logged_exception_path = true;
        throw std::runtime_error("expected snap failure");
        return PlotPoint2D{};
    }));
    try {
        layer.snap_data(*replacement, {});
        assert(false);
    } catch (const std::runtime_error&) {
        assert(logged_exception_path);
    }

    layer.clear();
    assert(layer.size() == 0);
    for (const auto phase : {
             PlotAnnotationPhase2D::Underlay,
             PlotAnnotationPhase2D::Overlay,
             PlotAnnotationPhase2D::Chrome}) {
        for (const auto clip : {
                 PlotAnnotationClip2D::PlotArea,
                 PlotAnnotationClip2D::Viewport}) {
            assert(layer.visual_scene(phase, clip).size() == 0);
        }
    }

    // Engine-level routing distinguishes annotation consumption from plot
    // navigation so multi-panel hosts do not broadcast a fake zoom.
    PlotEngine2D engine;
    engine.set_viewport(0.0f, 0.0f, 400.0f, 300.0f);
    engine.set_view(0.0, 10.0, 0.0, 10.0);
    const auto engine_annotation = engine.annotations().create({
        DataAnchor2D{5.0, 5.0},
        {target_visual()},
    });
    assert(engine_annotation);
    const PlotFrame2D engine_frame = engine.plot_frame();
    engine.annotations().project(engine_frame, engine.data);
    const PlotPixelPoint2D engine_anchor =
        projected(engine.annotations(), *engine_annotation);
    double x0, x1, y0, y1;
    engine.get_view(x0, x1, y0, y1);
    assert(engine.on_mouse_wheel_result(
        engine_anchor.x, engine_anchor.y, 1.0f)
        == PlotInputResult2D::Annotation);
    double ax0, ax1, ay0, ay1;
    engine.get_view(ax0, ax1, ay0, ay1);
    assert(near(x0, ax0) && near(x1, ax1));
    assert(near(y0, ay0) && near(y1, ay1));

    const PlotPixelPoint2D empty =
        engine_frame.data_to_pixel(8.0, 8.0);
    assert(engine.on_mouse_wheel_result(empty.x, empty.y, 1.0f)
        == PlotInputResult2D::Navigation);
    engine.get_view(ax0, ax1, ay0, ay1);
    assert(!near(x0, ax0) && !near(y0, ay0));

    // A navigation capture keeps ownership while crossing an annotation;
    // hover alone must not interrupt middle-button panning.
    const double pan_x0 = ax0;
    assert(engine.on_mouse_down(
        empty.x, empty.y, tcbase::MouseButton::MIDDLE));
    engine.on_mouse_move(engine_anchor.x, engine_anchor.y);
    engine.on_mouse_up(
        engine_anchor.x,
        engine_anchor.y,
        tcbase::MouseButton::MIDDLE);
    engine.get_view(ax0, ax1, ay0, ay1);
    assert(!near(pan_x0, ax0));

    return 0;
}
