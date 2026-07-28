#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <termin_visual_scene/builtin_items2d.hpp>

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
    visual.item = std::make_unique<RectItem2D>(
            termin::Rect2f{-5.0f, -5.0f, 10.0f, 10.0f},
            tgfx::FillPaint{},
            std::nullopt);
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

const tc_graphic_item& projected_item(
    const PlotAnnotationLayer2D& layer,
    PlotAnnotationPhase2D phase,
    PlotAnnotationClip2D clip,
    GraphicItemHandle handle) {
    const auto& scene = layer.visual_scene(phase, clip);
    const tc_graphic_item* item = scene.resolve(handle);
    assert(item);
    return *item;
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
        target_visual(),
    });
    const auto line_handle = layer.create({
        SeriesPointRef2D{PlotSeriesKind2D::Line, 0, 1},
        target_visual(PlotAnnotationPhase2D::Underlay),
    });
    const auto scatter_handle = layer.create({
        SeriesPointRef2D{PlotSeriesKind2D::Scatter, 0, 1},
        target_visual(),
    });
    const auto axes_handle = layer.create({
        AxesFractionAnchor2D{0.25, 0.75},
        target_visual(),
    });
    const auto viewport_handle = layer.create({
        ViewportPixelAnchor2D{12.0f, 18.0f},
        target_visual(
            PlotAnnotationPhase2D::Chrome,
            PlotAnnotationClip2D::Viewport),
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

    const auto& stable_scene = layer.visual_scene(
        PlotAnnotationPhase2D::Overlay,
        PlotAnnotationClip2D::PlotArea);
    const auto& visual_item = projected_item(
        layer,
        PlotAnnotationPhase2D::Overlay,
        PlotAnnotationClip2D::PlotArea,
        stable_item);
    const auto world_origin =
        stable_scene.world_transform(visual_item)
            .transform_point({0.0f, 0.0f});
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
        target_visual(
            PlotAnnotationPhase2D::Overlay,
            PlotAnnotationClip2D::PlotArea),
    });
    const auto unclipped = layer.create({
        ViewportPixelAnchor2D{0.0f, 0.0f},
        target_visual(
            PlotAnnotationPhase2D::Chrome,
            PlotAnnotationClip2D::Viewport),
    });
    assert(clipped && unclipped);
    layer.project(frame, data);
    assert(layer.hit_test(frame, 10.0f, 20.0f));
    assert(layer.destroy(*unclipped));
    assert(!layer.hit_test(frame, 10.0f, 20.0f));

    // Invalid series references hide their live graphic. Restoring the source
    // reuses both semantic and graphic handles.
    const GraphicItemHandle line_item =
        layer.snapshot(*line_handle)->projected_graphics.front().item;
    data.lines[0].x.resize(1);
    data.lines[0].y.resize(1);
    layer.project(frame, data);
    const auto missing_series_point = layer.snapshot(*line_handle);
    assert(missing_series_point);
    assert(!missing_series_point->projected_anchor);
    assert(missing_series_point->projected_graphics.size() == 1);
    assert(same(
        missing_series_point->projected_graphics.front().item,
        line_item));
    const auto& hidden_scene = layer.visual_scene(
        PlotAnnotationPhase2D::Underlay,
        PlotAnnotationClip2D::PlotArea);
    assert(!hidden_scene.effective_visible(
        projected_item(
            layer,
            PlotAnnotationPhase2D::Underlay,
            PlotAnnotationClip2D::PlotArea,
            line_item)));
    data.lines[0].x = {1.0, 4.0};
    data.lines[0].y = {2.0, 10.0};
    layer.project(frame, data);
    assert(same(
        layer.snapshot(*line_handle)->projected_graphics.front().item,
        line_item));

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
        target_visual(),
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

    PlotAnnotationLayer2D marker_layer;
    PlotDataMarker2D marker;
    marker.data_position = {5.0, 10.0};
    marker.text = "peak 10.0";
    const auto marker_handle =
        marker_layer.create_data_marker(marker);
    assert(marker_handle);
    marker_layer.project(frame, data);
    const auto marker_annotation =
        marker_layer.snapshot(*marker_handle);
    assert(marker_annotation);
    assert(marker_annotation->projected_graphics.size() == 5);
    assert(marker_annotation->projected_graphics[0].phase
        == PlotAnnotationPhase2D::Overlay);
    assert(marker_annotation->projected_graphics[0].clip
        == PlotAnnotationClip2D::PlotArea);
    assert(marker_annotation->projected_graphics[2].phase
        == PlotAnnotationPhase2D::Chrome);
    assert(marker_annotation->projected_graphics[2].clip
        == PlotAnnotationClip2D::PlotArea);

    const PlotPixelPoint2D marker_pixel =
        *marker_annotation->projected_anchor;
    const GraphicItemHandle marker_item =
        marker_annotation->projected_graphics[0].item;
    const auto& marker_scene = marker_layer.visual_scene(
        PlotAnnotationPhase2D::Overlay,
        PlotAnnotationClip2D::PlotArea);
    const auto& normal_item = projected_item(
        marker_layer,
        PlotAnnotationPhase2D::Overlay,
        PlotAnnotationClip2D::PlotArea,
        marker_item);
    assert(std::string(tc_graphic_item_type_name(&normal_item))
           == "termin.visual.Ellipse2D");
    const auto normal_bounds = marker_scene.local_bounds(normal_item);
    assert(normal_bounds);
    const double normal_width =
        normal_bounds->x1 - normal_bounds->x0;
    assert(normal_width > 0.0);

    assert(marker_layer.route_pointer(frame, {
        20,
        PointerEventKind2D::Move,
        {marker_pixel.x, marker_pixel.y},
        0,
    }));
    assert(marker_layer.data_marker_snapshot(*marker_handle)->hovered);
    const auto& hovered_item = projected_item(
        marker_layer,
        PlotAnnotationPhase2D::Overlay,
        PlotAnnotationClip2D::PlotArea,
        marker_item);
    assert(std::string(tc_graphic_item_type_name(&hovered_item))
           == "termin.visual.Ellipse2D");
    const auto hovered_bounds = marker_scene.local_bounds(hovered_item);
    assert(hovered_bounds);
    const double hovered_width =
        hovered_bounds->x1 - hovered_bounds->x0;
    assert(hovered_width > normal_width);

    assert(marker_layer.set_snap_hook(
        *marker_handle,
        [](const PlotPoint2D& value) {
            return PlotPoint2D{
                std::round(value.x),
                std::round(value.y),
            };
        }));
    assert(marker_layer.route_pointer(frame, {
        21,
        PointerEventKind2D::Down,
        {marker_pixel.x, marker_pixel.y},
        0,
    }));
    assert(marker_layer.data_marker_snapshot(*marker_handle)->dragging);
    const PlotPixelPoint2D drag_pixel = frame.data_to_pixel(7.2, 15.7);
    assert(marker_layer.route_pointer(frame, {
        21,
        PointerEventKind2D::Move,
        {drag_pixel.x, drag_pixel.y},
        0,
    }));
    auto dragged = marker_layer.data_marker_snapshot(*marker_handle);
    assert(dragged);
    assert(near(dragged->marker.data_position.x, 7.0));
    assert(near(dragged->marker.data_position.y, 16.0));
    assert(marker_layer.route_pointer(frame, {
        21,
        PointerEventKind2D::Up,
        {frame.data_to_pixel(7.0, 16.0).x,
         frame.data_to_pixel(7.0, 16.0).y},
        0,
    }));
    assert(!marker_layer.data_marker_snapshot(*marker_handle)->dragging);

    const GraphicItemHandle bubble_item =
        marker_layer.snapshot(*marker_handle)->projected_graphics[2].item;
    const auto& bubble_scene = marker_layer.visual_scene(
        PlotAnnotationPhase2D::Chrome,
        PlotAnnotationClip2D::PlotArea);
    const auto& bubble_before_pan = projected_item(
        marker_layer,
        PlotAnnotationPhase2D::Chrome,
        PlotAnnotationClip2D::PlotArea,
        bubble_item);
    assert(std::string(tc_graphic_item_type_name(&bubble_before_pan))
           == "termin.visual.RoundedRect2D");
    const auto bubble_bounds =
        bubble_scene.local_bounds(bubble_before_pan);
    assert(bubble_bounds);
    const double bubble_width =
        bubble_bounds->x1 - bubble_bounds->x0;
    const auto bubble_transform_before =
        bubble_scene.world_transform(bubble_before_pan);
    marker_layer.project(panned, data);
    const auto& bubble_after_pan = projected_item(
        marker_layer,
        PlotAnnotationPhase2D::Chrome,
        PlotAnnotationClip2D::PlotArea,
        bubble_item);
    assert(std::string(tc_graphic_item_type_name(&bubble_after_pan))
           == "termin.visual.RoundedRect2D");
    const auto bubble_bounds_after =
        bubble_scene.local_bounds(bubble_after_pan);
    assert(bubble_bounds_after);
    assert(near(
        bubble_bounds_after->x1 - bubble_bounds_after->x0,
        bubble_width));
    assert(!near(
        bubble_transform_before.tx,
        bubble_scene.world_transform(bubble_after_pan).tx));

    int close_actions = 0;
    while (marker_layer.take_action()) {
        // Discard earlier activation events before checking close ordering.
    }
    assert(marker_layer.set_action_handler(
        *marker_handle,
        [&](const PlotAnnotationAction2D& action) {
            if (action.action == "close") ++close_actions;
        }));
    const auto close_projection =
        marker_layer.snapshot(*marker_handle)->projected_graphics[4];
    const auto& close_scene = marker_layer.visual_scene(
        close_projection.phase,
        close_projection.clip);
    const auto& close_item = projected_item(
        marker_layer,
        close_projection.phase,
        close_projection.clip,
        close_projection.item);
    const termin::Vec2f close_point =
        close_scene.world_transform(close_item)
            .transform_point({0.0f, 0.0f});
    assert(marker_layer.route_pointer(panned, {
        22, PointerEventKind2D::Down, close_point, 0}));
    assert(marker_layer.route_pointer(panned, {
        22, PointerEventKind2D::Up, close_point, 0}));
    assert(close_actions == 1);
    const auto close_action = marker_layer.take_action();
    assert(close_action);
    assert(close_action->annotation == *marker_handle);
    assert(close_action->action == "close");
    assert(!marker_layer.take_action());
    assert(!marker_layer.data_marker_snapshot(*marker_handle));

    PlotDataMarker2D clipped_marker;
    clipped_marker.data_position = {-0.2, 10.0};
    clipped_marker.text = "clipped anchor";
    const auto clipped_marker_handle =
        marker_layer.create_data_marker(clipped_marker);
    assert(clipped_marker_handle);
    marker_layer.project(frame, data);
    const auto clipped_marker_snapshot =
        marker_layer.snapshot(*clipped_marker_handle);
    const PlotPixelPoint2D clipped_anchor =
        *clipped_marker_snapshot->projected_anchor;
    assert(!frame.contains_plot_pixel(clipped_anchor.x, clipped_anchor.y));
    assert(!marker_layer.hit_test(
        frame, clipped_anchor.x, clipped_anchor.y));
    const auto clipped_bubble_projection =
        clipped_marker_snapshot->projected_graphics[2];
    const auto& clipped_bubble_scene = marker_layer.visual_scene(
        clipped_bubble_projection.phase,
        clipped_bubble_projection.clip);
    const auto& clipped_bubble = projected_item(
        marker_layer,
        clipped_bubble_projection.phase,
        clipped_bubble_projection.clip,
        clipped_bubble_projection.item);
    const termin::Vec2f clipped_bubble_center =
        clipped_bubble_scene.world_transform(clipped_bubble)
            .transform_point({0.0f, 0.0f});
    assert(marker_layer.hit_test(
        frame, clipped_bubble_center.x, clipped_bubble_center.y));
    assert(!marker_layer.hit_test(
        frame, frame.plot_area().x() - 1.0f, clipped_bubble_center.y));

    const PlotPixelPoint2D teardown_anchor =
        *marker_layer.snapshot(*clipped_marker_handle)->projected_anchor;
    // Move the marker into the plot before starting the teardown gesture.
    clipped_marker.data_position = {2.0, 5.0};
    assert(marker_layer.update_data_marker(
        *clipped_marker_handle, clipped_marker));
    marker_layer.project(frame, data);
    const PlotPixelPoint2D teardown_inside =
        *marker_layer.snapshot(*clipped_marker_handle)->projected_anchor;
    assert(marker_layer.route_pointer(frame, {
        23, PointerEventKind2D::Down, {teardown_inside.x, teardown_inside.y}, 0}));
    assert(marker_layer.destroy(*clipped_marker_handle));
    assert(!marker_layer.route_pointer(frame, {
        23, PointerEventKind2D::Move, {teardown_anchor.x, teardown_anchor.y}, 0}));

    PlotDataMarker2D invalid_marker;
    invalid_marker.callout_width =
        std::numeric_limits<float>::quiet_NaN();
    assert(!marker_layer.create_data_marker(invalid_marker));
    invalid_marker.callout_width = 100.0f;
    invalid_marker.text_color.r =
        std::numeric_limits<float>::infinity();
    assert(!marker_layer.create_data_marker(invalid_marker));

    // Engine-level routing distinguishes annotation consumption from plot
    // navigation so multi-panel hosts do not broadcast a fake zoom.
    PlotEngine2D engine;
    engine.set_viewport(0.0f, 0.0f, 400.0f, 300.0f);
    engine.set_view(0.0, 10.0, 0.0, 10.0);
    const auto engine_annotation = engine.annotations().create({
        DataAnchor2D{5.0, 5.0},
        target_visual(),
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
