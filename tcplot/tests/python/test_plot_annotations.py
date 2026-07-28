import pytest

from tcplot import (
    MouseButton,
    Plot2D,
    PlotDataMarker2D,
    PlotEngine2D,
)


def configured_engine():
    engine = PlotEngine2D()
    engine.set_viewport(0.0, 0.0, 400.0, 300.0)
    engine.set_view(0.0, 10.0, 0.0, 10.0)
    return engine


def marker_at(x=5.0, y=5.0, text="sample"):
    marker = PlotDataMarker2D()
    marker.x = x
    marker.y = y
    marker.text = text
    return marker


def test_marker_handle_mutation_snap_action_and_stale_safety():
    engine = configured_engine()
    handle = engine.create_data_marker(marker_at())
    assert handle
    assert engine.annotation_count() == 1
    assert engine.data_marker_snapshot(handle).marker.text == "sample"

    updated = marker_at(4.0, 6.0, "updated")
    assert engine.update_data_marker(handle, updated)
    assert engine.data_marker_snapshot(handle).marker.x == 4.0

    other = configured_engine()
    assert not other.update_data_marker(handle, updated)
    assert other.data_marker_snapshot(handle) is None
    assert not other.destroy_annotation(handle)

    assert engine.destroy_annotation(handle)
    assert engine.data_marker_snapshot(handle) is None
    assert not engine.destroy_annotation(handle)


def test_plot2d_widget_exposes_marker_api_without_engine_access():
    plot = Plot2D()
    plot.set_view(0.0, 10.0, 0.0, 10.0)
    handle = plot.create_data_marker(marker_at())
    assert handle
    assert plot.data_marker_snapshot(handle).marker.text == "sample"
    assert plot.set_marker_snap_handler(handle, lambda x, y: (x, y))
    assert plot.set_marker_action_handler(handle, lambda _handle, _action: None)
    assert plot.destroy_annotation(handle)
    assert plot.data_marker_snapshot(handle) is None


def test_marker_callbacks_propagate_and_semantic_close_is_pollable():
    engine = configured_engine()
    handle = engine.create_data_marker(marker_at())
    assert handle
    assert engine.set_marker_snap_handler(
        handle, lambda x, y: (round(x), round(y)))

    anchor_x, anchor_y = engine.annotation_anchor_pixel(handle)
    assert engine.on_mouse_down(anchor_x, anchor_y, MouseButton.LEFT)
    engine.on_mouse_move(260.0, 125.0)
    engine.on_mouse_up(260.0, 125.0, MouseButton.LEFT)
    moved = engine.data_marker_snapshot(handle)
    assert moved.marker.x == pytest.approx(6.0)
    assert moved.marker.y == pytest.approx(6.0)
    assert engine.take_annotation_action().action == "activate"

    callback_actions = []
    assert engine.set_marker_action_handler(
        handle,
        lambda annotation, action: callback_actions.append(
            (annotation, action)),
    )
    # Callout and close button move with the dragged anchor.
    anchor_x, anchor_y = engine.annotation_anchor_pixel(handle)
    close_x = anchor_x + 54.0 + 75.0 - 14.0
    close_y = anchor_y - 46.0
    assert engine.on_mouse_down(close_x, close_y, MouseButton.LEFT)
    engine.on_mouse_up(close_x, close_y, MouseButton.LEFT)
    assert callback_actions[0][0] == handle
    assert callback_actions[0][1] == "close"
    event = engine.take_annotation_action()
    assert event.annotation == handle
    assert event.action == "close"
    assert engine.data_marker_snapshot(handle) is None


def test_marker_callback_failure_is_logged_and_propagated():
    engine = configured_engine()
    handle = engine.create_data_marker(marker_at())

    def fail(_annotation, _action):
        raise RuntimeError("python marker callback failed")

    assert engine.set_marker_action_handler(handle, fail)
    anchor_x, anchor_y = engine.annotation_anchor_pixel(handle)
    close_x = anchor_x + 54.0 + 75.0 - 14.0
    close_y = anchor_y - 46.0
    assert engine.on_mouse_down(close_x, close_y, MouseButton.LEFT)
    with pytest.raises(RuntimeError, match="python marker callback failed"):
        engine.on_mouse_up(close_x, close_y, MouseButton.LEFT)
    assert engine.data_marker_snapshot(handle) is not None
    assert engine.take_annotation_action().action == "close"
