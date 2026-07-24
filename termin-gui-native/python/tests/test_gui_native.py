import gc
import os
import weakref
from pathlib import Path

import pytest

import termin.gui_native as gui_native
from termin.gui_native import (
    Color,
    Constraints,
    CursorIntent,
    tc_ui_document_create,
    tc_ui_document_destroy,
    DrawCommandType,
    DrawList,
    DrawListRenderer,
    EdgeInsets,
    EventResult,
    FrameTimeModel,
    FrameTimelineModel,
    FrameTimelineSample,
    ImageFit,
    KeyCode,
    KeyEvent,
    KeyEventType,
    LayoutPolicy,
    ModifierFlag,
    OverlayDismissReason,
    OverlayFlag,
    PaintContext,
    Point,
    PointerEvent,
    PointerEventType,
    Rect,
    RichTextModel,
    RichTextSegment,
    RichTextStyle,
    Size,
    StyleField,
    StyleOverride,
    StyleRole,
    StyleState,
    TextureSampling,
    Widget,
    WidgetFlag,
    WidgetLanguage,
    WidgetOwnerReloadPolicy,
    WidgetOwnership,
    build_python_showcase,
    has_widget_type,
    register_widget_type,
    registered_widget_types,
    tooltip_rect,
    unregister_widget_owner,
    unregister_widget_type,
)


def test_python_exposes_only_the_non_owning_tc_document_handle():
    assert gui_native.TcDocument is not None
    assert not hasattr(gui_native, "Document")
    assert not hasattr(gui_native.TcDocument, "close")
    assert not hasattr(gui_native.TcDocument, "__enter__")


def _bundled_font_path() -> Path:
    sdk_root = os.environ.get("TERMIN_SDK")
    if not sdk_root:
        pytest.fail("TERMIN_SDK must point to the SDK used by the native GUI tests")

    font_path = Path(sdk_root) / "share" / "termin" / "fonts" / "DroidSans.ttf"
    if not font_path.is_file():
        pytest.fail(f"Bundled native GUI test font is missing: {font_path}")
    return font_path


class DemoWidget(Widget):
    def __init__(self):
        self.paint_count = 0

    def paint(self, context):
        self.paint_count += 1
        context.push_clip(Rect(0.0, 0.0, 64.0, 32.0))
        context.fill_rect(Rect(1.0, 2.0, 30.0, 10.0), Color(0.1, 0.2, 0.3, 1.0))
        context.stroke_rect(Rect(2.0, 3.0, 40.0, 12.0), Color(0.2, 0.4, 0.6, 1.0), 1.5)
        context.draw_line(
            Point(4.0, 5.0),
            Point(6.0, 7.0),
            Color(0.8, 0.7, 0.6, 1.0),
            2.0,
        )
        context.draw_text("hello", Point(8.0, 9.0), 13.0, Color(0.9, 0.1, 0.2, 1.0))
        context.pop_clip()


def test_python_native_showcase_builds_stable_headless_snapshot():
    document = tc_ui_document_create()
    showcase = build_python_showcase(document)
    renderer = DrawListRenderer()
    assert renderer.set_default_font_path(str(_bundled_font_path()), 14)
    renderer.bind_text_measurer(document)

    document.layout_roots(Rect(0.0, 0.0, 800.0, 600.0))
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))

    assert showcase.root.stable_id == "python-showcase.root"
    assert document.live_widget_count == 15
    assert draw_list.command_count == 117
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) == 31
    assert sum(command.type == DrawCommandType.PushClip for command in draw_list.commands) == 26
    assert sum(command.type == DrawCommandType.PopClip for command in draw_list.commands) == 26
    assert showcase.widgets["list"].selected_indices == [1]
    assert showcase.widgets["tree"].visible_count == 3
    assert showcase.widgets["table"].model.row_count == 3
    assert showcase.widgets["frame_graph"].model.samples == pytest.approx([16.0, 16.8, 15.7, 18.2, 16.3])

    focus_order = []
    for _ in range(document.live_widget_count):
        if not document.focus_next():
            break
        focus_order.append((document.focused_widget.index, document.focused_widget.generation))
    text_input = showcase.widgets["text_input"].handle
    text_area = showcase.widgets["text_area"].handle
    assert (text_input.index, text_input.generation) in focus_order
    assert (text_area.index, text_area.generation) in focus_order


def test_python_box_layout_policies_padding_spacing_and_limits():
    document = tc_ui_document_create()
    root = document.create_vstack("policy-root")
    fixed = document.create_label("Fixed")
    preferred = document.create_label("Preferred")
    flexible = document.create_label("Flexible")
    preferred.preferred_size = Size(40.0, 40.0)

    root.set_layout_padding(EdgeInsets(10.0, 10.0, 10.0, 10.0))
    root.set_layout_spacing(5.0)
    root.set_layout_background(Color(0.1, 0.2, 0.3, 1.0))
    root.set_layout_border(Color(0.8, 0.8, 0.8, 1.0), 2.0)
    root.add_fixed_child(fixed, 30.0)
    root.add_preferred_child(preferred)
    root.add_flex_child(flexible, 2.0)
    assert root.set_child_extent_limits(flexible, 20.0, 120.0)
    assert document.add_root(root.handle)
    document.layout_roots(Rect(0.0, 0.0, 300.0, 200.0))

    assert fixed.bounds.x == pytest.approx(10.0)
    assert fixed.bounds.width == pytest.approx(280.0)
    assert fixed.bounds.height == pytest.approx(30.0)
    assert preferred.bounds.y == pytest.approx(45.0)
    assert preferred.bounds.height == pytest.approx(40.0)
    assert flexible.bounds.y == pytest.approx(90.0)
    assert flexible.bounds.height == pytest.approx(100.0)

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.FillRect for command in draw_list.commands) >= 1
    assert sum(command.type == DrawCommandType.StrokeRect for command in draw_list.commands) >= 1

    with pytest.raises(RuntimeError, match="BoxLayout"):
        fixed.append_child(document.create_label("invalid"), LayoutPolicy.Fixed, 10.0)


def test_python_registered_widget_type_identity_lifetime_and_reload():
    type_name = "test.python.RegisteredWidget"
    created = []
    destroyed = []

    class RegisteredWidget(Widget):
        def __init__(self):
            created.append(self)

        def on_destroy(self):
            destroyed.append(self)

    assert unregister_widget_type(type_name)
    try:
        register_widget_type(type_name, RegisteredWidget, owner="test.python")
        assert has_widget_type(type_name)
        assert type_name in registered_widget_types()

        document = tc_ui_document_create()
        parent = document.create_registered_widget(type_name)
        child = document.create_registered_widget(type_name)
        stale_handle = parent.handle
        assert parent.append_child(child)
        assert len(created) == 2
        assert created[0].native.handle == parent.handle
        assert parent.type_name == type_name
        assert parent.native_language == WidgetLanguage.Python
        assert parent.ownership == WidgetOwnership.Owned
        registered_snapshot = document.inspect_snapshot()
        registered_data = next(item for item in registered_snapshot["widgets"] if item["handle"] == parent.handle)
        assert registered_data["type_name"] == type_name
        assert registered_data["native_language"] == WidgetLanguage.Python
        assert registered_data["ownership"] == WidgetOwnership.Owned

        with pytest.raises(RuntimeError, match="failed to register"):
            register_widget_type(type_name, RegisteredWidget, owner="replacement")

        assert unregister_widget_type(type_name)
        assert not parent.alive
        assert not child.alive
        assert not created[0].alive
        assert not created[1].alive
        assert destroyed == [created[1], created[0]]
        assert not has_widget_type(type_name)

        register_widget_type(type_name, RegisteredWidget, owner="test.python.reload")
        fresh = document.create_registered_widget(type_name)
        assert fresh.alive
        assert fresh.handle != stale_handle
        assert not parent.alive
    finally:
        assert unregister_widget_type(type_name)


def test_python_registered_widget_constructor_failure_rolls_back():
    type_name = "test.python.FailingRegisteredWidget"

    def fail_factory():
        raise RuntimeError("registered constructor failed")

    assert unregister_widget_type(type_name)
    try:
        register_widget_type(type_name, fail_factory, owner="test.python")
        document = tc_ui_document_create()
        with pytest.raises(RuntimeError, match="registered constructor failed"):
            document.create_registered_widget(type_name)
        assert document.live_widget_count == 0
    finally:
        assert unregister_widget_type(type_name)


def test_python_widget_owner_reload_invalidates_nested_trees_and_stale_refs():
    parent_type = "test.python.ReloadParent"
    sibling_type = "test.python.ReloadSibling"
    foreign_type = "test.python.ReloadForeignChild"
    destroyed = []

    class ReloadWidget(Widget):
        def __init__(self, label):
            self.label = label

        def on_destroy(self):
            destroyed.append(self.label)

    class Factory:
        def __init__(self, label):
            self.label = label

        def __call__(self):
            return ReloadWidget(self.label)

    for type_name in (parent_type, sibling_type, foreign_type):
        assert unregister_widget_type(type_name)
    parent_factory = Factory("parent")
    sibling_factory = Factory("sibling")
    foreign_factory = Factory("foreign")
    parent_factory_ref = weakref.ref(parent_factory)
    sibling_factory_ref = weakref.ref(sibling_factory)
    try:
        register_widget_type(parent_type, parent_factory, owner="test.python.reload.owner")
        register_widget_type(sibling_type, sibling_factory, owner="test.python.reload.owner")
        register_widget_type(foreign_type, foreign_factory, owner="test.python.reload.foreign")
        del parent_factory
        del sibling_factory

        first_document = tc_ui_document_create()
        second_document = tc_ui_document_create()
        parent = first_document.create_registered_widget(parent_type)
        foreign_child = first_document.create_registered_widget(foreign_type)
        sibling = second_document.create_registered_widget(sibling_type)
        stale_parent_handle = parent.handle
        stale_foreign_handle = foreign_child.handle
        assert parent.append_child(foreign_child)

        assert unregister_widget_owner("test.python.reload.owner", WidgetOwnerReloadPolicy.Invalidate) == 2
        assert not parent.alive
        assert not foreign_child.alive
        assert not sibling.alive
        assert destroyed.count("parent") == 1
        assert destroyed.count("foreign") == 1
        assert destroyed.count("sibling") == 1
        assert destroyed.index("foreign") < destroyed.index("parent")
        assert not has_widget_type(parent_type)
        assert not has_widget_type(sibling_type)
        assert has_widget_type(foreign_type)
        gc.collect()
        assert parent_factory_ref() is None
        assert sibling_factory_ref() is None

        fresh_foreign = first_document.create_registered_widget(foreign_type)
        assert fresh_foreign.handle != stale_foreign_handle
        replacement_factory = Factory("replacement-parent")
        register_widget_type(parent_type, replacement_factory, owner="test.python.reload.owner")
        fresh_parent = first_document.create_registered_widget(parent_type)
        assert fresh_parent.handle != stale_parent_handle
        assert not parent.alive
    finally:
        unregister_widget_owner("test.python.reload.owner")
        unregister_widget_owner("test.python.reload.foreign")


def test_python_document_inspect_snapshot_is_neutral_and_independent():
    document = tc_ui_document_create()
    root = Widget()
    child = Widget()
    overlay = Widget()
    root_handle = document.adopt_root(root, "snapshot-root")
    child_handle = document.adopt(child, "snapshot-child")
    overlay_handle = document.adopt(overlay, "snapshot-overlay")
    assert root.native.append_child(child.native)
    root.bounds = Rect(0.0, 0.0, 100.0, 60.0)
    child.bounds = Rect(5.0, 6.0, 40.0, 20.0)
    child.focusable = True
    root.cursor_intent = CursorIntent.Hand
    assert document.set_focus(child_handle)
    assert document.set_pointer_capture(child_handle)
    assert document.show_overlay(
        overlay_handle,
        int(OverlayFlag.Tooltip | OverlayFlag.PointerTransparent),
    )

    snapshot = document.inspect_snapshot()
    assert snapshot["theme_revision"] == document.theme_revision
    assert snapshot["roots"] == [root_handle]
    assert snapshot["overlays"] == [
        {
            "handle": overlay_handle,
            "flags": int(OverlayFlag.Tooltip | OverlayFlag.PointerTransparent),
        }
    ]
    assert snapshot["interaction"]["focused"] == child_handle
    assert snapshot["interaction"]["pointer_capture"] == child_handle
    assert snapshot["interaction"]["cursor_intent"] == CursorIntent.Default
    assert "hovered" in snapshot["interaction"]
    assert "pressed" in snapshot["interaction"]

    by_handle = {item["handle"].index: item for item in snapshot["widgets"]}
    root_data = by_handle[root_handle.index]
    child_data = by_handle[child_handle.index]
    assert root_data["type_name"] == "PythonWidget"
    assert root_data["debug_name"] == "snapshot-root"
    assert root_data["parent"] is None
    assert root_data["children"] == [child_handle]
    assert root_data["cursor_intent"] == CursorIntent.Hand
    assert root_data["native_language"] == WidgetLanguage.Python
    assert root_data["ownership"] == WidgetOwnership.Owned
    assert child_data["parent"] == root_handle
    assert child_data["bounds"].x == 5.0
    assert child_data["bounds"].height == 20.0
    assert child_data["flags"] & int(WidgetFlag.Focusable)
    assert child_data["dirty_flags"] == child.native.dirty_flags

    assert document.destroy_widget_recursive(root_handle)
    assert not root.alive
    assert root_data["debug_name"] == "snapshot-root"
    assert root_data["children"] == [child_handle]


def test_python_registered_widget_document_serialization_round_trip_and_rollback():
    type_name = "test.python.SerializableWidget"
    created = []
    serializer_mode = {"invalid": False}

    class SerializableWidget(Widget):
        def __init__(self):
            self.value = -1
            created.append(self)

    def serialize_state(widget):
        if serializer_mode["invalid"]:
            return object()
        return {"value": widget.value, "nested": [True, None, 2.5, "state"]}

    def deserialize_state(widget, state):
        assert state["nested"] == [True, None, 2.5, "state"]
        widget.value = state["value"]

    assert unregister_widget_type(type_name)
    try:
        register_widget_type(
            type_name,
            SerializableWidget,
            owner="test.python.serialization",
            serialize_state=serialize_state,
            deserialize_state=deserialize_state,
        )
        source = tc_ui_document_create()
        parent = source.create_registered_widget(type_name)
        child = source.create_registered_widget(type_name)
        created[0].value = 41
        created[1].value = 82
        parent.stable_id = "serialization-root"
        parent.name = "Serializable Root"
        parent.debug_name = "serialization-debug"
        parent.bounds = Rect(1.0, 2.0, 300.0, 180.0)
        child.preferred_size = Size(70.0, 24.0)
        child.focusable = True
        child.enabled = False
        child.cursor_intent = CursorIntent.Crosshair
        child.style_role = StyleRole.Button
        assert parent.append_child(child)
        assert source.add_root(parent.handle)

        serialized = source.serialize()
        assert serialized["$schema"] == "termin.gui.document"
        assert serialized["version"] == 2
        assert serialized["widgets"][0]["type"] == type_name
        assert serialized["widgets"][0]["state"]["value"] == 41
        assert serialized["widgets"][0]["children"] == [1]
        assert serialized["roots"] == [0]

        restored = tc_ui_document_create()
        restored.restore(serialized)
        assert len(created) == 4
        assert created[2].value == 41
        assert created[3].value == 82
        assert created[2].native.stable_id == "serialization-root"
        assert created[2].native.name == "Serializable Root"
        assert created[2].native.debug_name == "serialization-debug"
        assert created[2].bounds.width == 300.0
        assert [item.handle for item in created[2].native.children] == [created[3].handle]
        assert created[3].native.parent.handle == created[2].handle
        assert created[3].native.preferred_size.width == 70.0
        assert created[3].focusable
        assert not created[3].enabled
        assert created[3].cursor_intent == CursorIntent.Crosshair
        assert created[3].native.style_role == StyleRole.Button
        restored_snapshot = restored.inspect_snapshot()
        assert restored_snapshot["roots"] == [created[2].handle]

        with pytest.raises(RuntimeError, match="failed to restore"):
            restored.restore(serialized)
        assert restored.live_widget_count == 2

        malformed = source.serialize()
        malformed["widgets"][0]["children"][0] = 0
        rejected = tc_ui_document_create()
        with pytest.raises(RuntimeError, match="failed to restore"):
            rejected.restore(malformed)
        assert rejected.live_widget_count == 0

        serializer_mode["invalid"] = True
        with pytest.raises(ValueError, match="serialized widget state"):
            source.serialize()
        assert source.live_widget_count == 2
    finally:
        assert unregister_widget_type(type_name)


def test_python_widget_paint_builds_draw_list():
    document = tc_ui_document_create()
    widget = DemoWidget()
    handle = document.adopt_root(widget, "DemoWidget")
    assert handle
    assert document.root_count == 1
    assert document.live_widget_count == 1

    draw_list = DrawList()
    paint_context = PaintContext(draw_list)
    document.paint_roots(paint_context)

    assert widget.paint_count == 1
    assert draw_list.command_count == 6

    clip, fill, stroke, line, text, pop = draw_list.commands
    assert clip.type == DrawCommandType.PushClip
    assert clip.rect.width == 64.0
    assert fill.type == DrawCommandType.FillRect
    assert fill.rect.x == 1.0
    assert fill.rect.width == 30.0
    assert fill.color.g == pytest.approx(0.2)
    assert stroke.type == DrawCommandType.StrokeRect
    assert stroke.rect.y == 3.0
    assert stroke.rect.height == 12.0
    assert stroke.color.b == pytest.approx(0.6)
    assert stroke.thickness == 1.5
    assert line.type == DrawCommandType.Line
    assert line.p0.x == 4.0
    assert line.p1.y == 7.0
    assert line.thickness == 2.0
    assert text.type == DrawCommandType.Text
    assert text.p0.x == 8.0
    assert text.p0.y == 9.0
    assert text.text == "hello"
    assert text.font_size == 13.0
    assert text.color.r == pytest.approx(0.9)
    assert pop.type == DrawCommandType.PopClip


def test_python_extended_draw_commands_copy_polyline_points():
    draw_list = DrawList()
    context = PaintContext(draw_list)
    white = Color(1.0, 1.0, 1.0, 1.0)
    points = [Point(1.0, 2.0), Point(3.0, 4.0), Point(5.0, 6.0)]

    context.fill_rounded_rect(Rect(0.0, 0.0, 20.0, 10.0), 3.0, white)
    context.stroke_rounded_rect(Rect(1.0, 1.0, 18.0, 8.0), 2.0, white, 1.5)
    context.fill_circle(Point(10.0, 10.0), 5.0, white, 20)
    context.stroke_circle(Point(10.0, 10.0), 6.0, white, 2.0, 24)
    context.draw_arc(Point(10.0, 10.0), 7.0, 0.25, 2.5, white, 1.0, 12)
    context.draw_polyline(points, white, 3.0)
    context.draw_text("snapshot", Point(1.0, 1.0), 12.0, white)
    points[1].x = 99.0

    commands = draw_list.commands
    draw_list.clear()
    assert [command.type for command in commands] == [
        DrawCommandType.FillRoundedRect,
        DrawCommandType.StrokeRoundedRect,
        DrawCommandType.FillCircle,
        DrawCommandType.StrokeCircle,
        DrawCommandType.Arc,
        DrawCommandType.Polyline,
        DrawCommandType.Text,
    ]
    assert commands[0].radius == 3.0
    assert commands[4].segments == 12
    assert commands[5].points[1].x == 3.0
    assert commands[6].text == "snapshot"


def test_python_paint_exception_propagates():
    class FailingWidget(Widget):
        def paint(self, context):
            raise RuntimeError("paint failed")

    document = tc_ui_document_create()
    document.adopt_root(FailingWidget(), "FailingWidget")

    draw_list = DrawList()
    paint_context = PaintContext(draw_list)

    try:
        document.paint_roots(paint_context)
    except RuntimeError as exc:
        assert "paint failed" in str(exc)
    else:
        raise AssertionError("paint exception did not propagate")


def test_python_widget_complete_vtable_and_common_state():
    class InteractiveWidget(Widget):
        def __init__(self):
            self.measured = []
            self.layouts = []
            self.pointer_events = []
            self.key_events = []
            self.text_events = []
            self.focus_events = []

        def measure(self, constraints):
            self.measured.append(constraints.max_size.width)
            return Size(80.0, 24.0)

        def layout(self, rect):
            assert self.bounds.x == rect.x
            assert self.bounds.width == rect.width
            self.layouts.append(rect.width)

        def pointer_event(self, event):
            self.pointer_events.append(event.type)
            return EventResult.Handled

        def key_event(self, event):
            self.key_events.append(event.key)
            return EventResult.Handled

        def text_event(self, text):
            self.text_events.append(text)
            return EventResult.Handled

        def focus_event(self, focused):
            self.focus_events.append(focused)

    document = tc_ui_document_create()
    widget = InteractiveWidget()
    handle = document.adopt_root(widget, "InteractiveWidget")
    widget.focusable = True
    widget.native.preferred_size = Size(80.0, 24.0)

    measured = widget.native.measure(Constraints(Size(), Size(100.0, 50.0)))
    assert measured.width == 80.0
    assert widget.measured == [100.0]
    document.layout_roots(Rect(5.0, 6.0, 100.0, 40.0))
    assert widget.layouts == [100.0]
    assert widget.bounds.x == 5.0
    assert widget.bounds.height == 40.0
    assert document.hit_test(20.0, 20.0) == handle

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 20.0
    pointer.y = 20.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.focused_widget == handle
    assert widget.pointer_events == [PointerEventType.Enter, PointerEventType.Down]
    assert widget.focus_events == [True]

    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert document.dispatch_text_event("hello") == EventResult.Handled
    assert widget.key_events == [KeyCode.Enter]
    assert widget.text_events == ["hello"]


def test_python_routing_bubbles_and_survives_target_destroy():
    events = []
    document = tc_ui_document_create()

    class Parent(Widget):
        def pointer_event(self, event):
            events.append(("parent", event.type))
            return EventResult.Handled if event.type == PointerEventType.Down else EventResult.Ignored

    class SelfDestroyingChild(Widget):
        def pointer_event(self, event):
            events.append(("child", event.type))
            if event.type == PointerEventType.Down:
                document.destroy_widget(self.handle)
            return EventResult.Ignored

    parent = Parent()
    child = SelfDestroyingChild()
    parent_handle = document.adopt_root(parent, "parent")
    child_handle = document.adopt(child, "child")
    assert parent.native.append_child(child.native)
    parent.bounds = Rect(0.0, 0.0, 100.0, 40.0)
    child.bounds = Rect(0.0, 0.0, 100.0, 40.0)

    event = PointerEvent()
    event.type = PointerEventType.Down
    event.x = 10.0
    event.y = 10.0
    assert document.dispatch_pointer_event(event) == EventResult.Handled
    assert events == [
        ("child", PointerEventType.Enter),
        ("child", PointerEventType.Down),
        ("parent", PointerEventType.Down),
    ]
    assert not document.is_alive(child_handle)
    assert document.pressed_widget == parent_handle


def test_python_cursor_intents_inherit_notify_and_reset():
    changes = []
    document = tc_ui_document_create()
    parent = Widget()
    child = Widget()
    document.adopt_root(parent, "cursor-parent")
    child_handle = document.adopt(child, "cursor-child")
    assert parent.native.append_child(child.native)
    parent.bounds = Rect(0.0, 0.0, 100.0, 40.0)
    child.bounds = Rect(0.0, 0.0, 100.0, 40.0)
    parent.cursor_intent = CursorIntent.Hand
    document.set_cursor_changed_handler(changes.append)

    event = PointerEvent()
    event.type = PointerEventType.Move
    event.x = 10.0
    event.y = 10.0
    document.dispatch_pointer_event(event)
    assert document.cursor_intent == CursorIntent.Hand
    assert changes == [CursorIntent.Hand]

    child.cursor_intent = CursorIntent.Crosshair
    assert document.cursor_intent == CursorIntent.Crosshair
    child.cursor_intent = CursorIntent.Default
    assert document.cursor_intent == CursorIntent.Default
    child.cursor_intent = CursorIntent.Text
    child.visible = False
    assert document.cursor_intent == CursorIntent.Default
    assert changes[-1] == CursorIntent.Default

    child.visible = True
    document.dispatch_pointer_event(event)
    assert document.cursor_intent == CursorIntent.Text
    document.destroy_widget(child_handle)
    assert document.cursor_intent == CursorIntent.Default

    document.set_cursor_changed_handler(None)
    parent.cursor_intent = CursorIntent.Move
    assert changes[-1] == CursorIntent.Default


def test_builtin_widgets_publish_semantic_cursor_intents():
    document = tc_ui_document_create()
    assert document.create_text_input().widget.cursor_intent == CursorIntent.Text
    assert document.create_text_area().widget.cursor_intent == CursorIntent.Text
    assert document.create_button().widget.cursor_intent == CursorIntent.Hand
    assert document.create_checkbox().widget.cursor_intent == CursorIntent.Hand
    assert document.create_canvas().widget.cursor_intent == CursorIntent.Inherit


def test_python_focus_events_and_tab_traversal():
    document = tc_ui_document_create()
    root = Widget()
    first = Widget()
    skipped = Widget()
    third = Widget()
    focus_events = []

    first.focus_event = lambda focused: focus_events.append(("first", focused))
    skipped.focus_event = lambda focused: focus_events.append(("skipped", focused))
    third.focus_event = lambda focused: focus_events.append(("third", focused))

    document.adopt_root(root, "root")
    first_handle = document.adopt(first, "first")
    document.adopt(skipped, "skipped")
    third_handle = document.adopt(third, "third")
    root.native.append_child(first.native)
    root.native.append_child(skipped.native)
    root.native.append_child(third.native)
    first.focusable = True
    skipped.focusable = True
    third.focusable = True
    skipped.enabled = False

    assert document.focus_next()
    assert document.focused_widget == first_handle
    assert document.focus_next()
    assert document.focused_widget == third_handle

    event = KeyEvent()
    event.type = KeyEventType.Down
    event.key = KeyCode.Tab
    event.modifiers = int(ModifierFlag.Shift)
    assert document.dispatch_key_event(event) == EventResult.Handled
    assert document.focused_widget == first_handle
    assert focus_events == [
        ("first", True),
        ("first", False),
        ("third", True),
        ("third", False),
        ("first", True),
    ]


def test_python_overlay_order_dismissal_and_tooltip_placement():
    paint_order = []
    dismissals = []

    class OverlayWidget(Widget):
        def __init__(self, name):
            self.name = name

        def paint(self, context):
            paint_order.append(self.name)

        def overlay_dismissed(self, reason):
            dismissals.append(reason)

    document = tc_ui_document_create()
    root = OverlayWidget("root")
    popup = OverlayWidget("popup")
    tooltip = OverlayWidget("tooltip")
    document.adopt_root(root, "root")
    popup_handle = document.adopt(popup, "popup")
    tooltip_handle = document.adopt(tooltip, "tooltip")
    root.bounds = Rect(0.0, 0.0, 100.0, 80.0)
    popup.bounds = Rect(10.0, 10.0, 40.0, 30.0)
    tooltip.bounds = Rect(12.0, 12.0, 30.0, 20.0)
    assert document.show_overlay(
        popup_handle,
        int(OverlayFlag.DismissOnOutside),
    )
    assert document.show_overlay(tooltip_handle, int(OverlayFlag.Tooltip))
    assert document.overlay_count == 2
    assert document.hit_test(20.0, 20.0) == popup_handle

    document.paint(PaintContext(DrawList()))
    assert paint_order == ["root", "popup", "tooltip"]

    event = PointerEvent()
    event.type = PointerEventType.Down
    event.x = 90.0
    event.y = 70.0
    assert document.dispatch_pointer_event(event) == EventResult.Handled
    assert dismissals == [OverlayDismissReason.Outside]
    assert document.overlay_count == 1

    placed = tooltip_rect(
        Rect(0.0, 0.0, 100.0, 80.0),
        Point(95.0, 75.0),
        Size(30.0, 20.0),
    )
    assert placed.x == 66.0
    assert placed.y == 56.0


def test_python_widget_tree_recursive_destroy_and_stale_refs():
    destroyed = []

    class TrackedWidget(Widget):
        def __init__(self, name):
            self.name = name

        def on_destroy(self):
            destroyed.append(self.name)

    document = tc_ui_document_create()
    parent = TrackedWidget("parent")
    child = TrackedWidget("child")
    parent_handle = document.adopt_root(parent, "parent")
    child_handle = document.adopt(child, "child")
    parent_ref = parent.native
    child_ref = child.native

    assert parent_ref.append_child(child_ref)
    assert parent_ref.children[0].handle == child_handle
    assert child_ref.parent.handle == parent_handle
    assert document.root_count == 1

    assert document.destroy_widget_recursive(parent_handle)
    assert destroyed == ["child", "parent"]
    assert not parent_ref.alive
    assert not child_ref.alive
    assert not document.is_alive(parent_handle)
    assert not document.is_alive(child_handle)
    with pytest.raises(RuntimeError, match="stale"):
        _ = child_ref.bounds


def test_document_retain_and_deleter_own_python_widget_lifetime():
    destroyed = []

    class RetainedWidget(Widget):
        def on_destroy(self):
            destroyed.append("destroyed")

    document = tc_ui_document_create()
    widget = RetainedWidget()
    weak_widget = weakref.ref(widget)
    handle = document.adopt(widget, "retained")
    native = widget.native
    del widget
    gc.collect()

    assert weak_widget() is not None
    assert native.alive
    assert document.destroy_widget(handle)
    gc.collect()
    assert weak_widget() is None
    assert destroyed == ["destroyed"]
    assert not native.alive


def test_explicit_document_destruction_invalidates_widget_refs():
    widget = Widget()
    document = tc_ui_document_create()
    document.adopt(widget, "document-child")
    native = widget.native
    assert native.alive

    tc_ui_document_destroy(document)
    del document
    gc.collect()

    assert not native.alive
    assert not widget.alive
    with pytest.raises(RuntimeError, match="stale"):
        _ = native.visible


def test_python_base_widget_routes_canonical_children():
    class ChildWidget(Widget):
        def __init__(self):
            self.paint_count = 0
            self.pointer_events = []

        def paint(self, context):
            self.paint_count += 1

        def pointer_event(self, event):
            self.pointer_events.append(event.type)
            return EventResult.Handled

    document = tc_ui_document_create()
    parent = Widget()
    child = ChildWidget()
    document.adopt_root(parent, "parent")
    child_handle = document.adopt(child, "child")
    assert parent.native.append_child(child.native)
    parent.bounds = Rect(0.0, 0.0, 100.0, 100.0)
    child.bounds = Rect(10.0, 10.0, 40.0, 30.0)
    parent.mouse_transparent = True

    assert document.hit_test(20.0, 20.0) == child_handle
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert child.paint_count == 1

    event = PointerEvent()
    event.type = PointerEventType.Down
    event.x = 20.0
    event.y = 20.0
    assert document.dispatch_pointer_event(event) == EventResult.Handled
    assert child.pointer_events == [PointerEventType.Enter, PointerEventType.Down]


def test_python_widget_rejects_double_adoption_without_false_destroy():
    destroyed = []

    class TrackedWidget(Widget):
        def on_destroy(self):
            destroyed.append("destroyed")

    first_document = tc_ui_document_create()
    second_document = tc_ui_document_create()
    widget = TrackedWidget()
    first_handle = first_document.adopt(widget, "tracked")

    with pytest.raises(RuntimeError, match="already adopted"):
        second_document.adopt(widget, "duplicate")

    assert destroyed == []
    assert widget.alive
    assert first_document.is_alive(first_handle)
    assert first_document.destroy_widget(first_handle)
    assert destroyed == ["destroyed"]

    second_handle = second_document.adopt(widget, "readopted")
    assert widget.alive
    assert second_document.is_alive(second_handle)


def test_widget_ref_wraps_native_cpp_widgets_without_duplicate_state():
    document = tc_ui_document_create()
    root = document.create_hstack("native-root")
    panel = document.create_panel("native-panel")
    label = document.create_label("native", "native-label")
    assert document.add_root(root.handle)
    assert root.append_child(panel)
    assert root.append_child(label)
    panel.preferred_size = Size(40.0, 20.0)
    label.preferred_size = Size(60.0, 20.0)

    document.layout_roots(Rect(0.0, 0.0, 120.0, 30.0))
    assert root.debug_name == "native-root"
    assert panel.parent.handle == root.handle
    assert label.parent.handle == root.handle
    assert panel.bounds.width == pytest.approx(50.0)
    assert label.bounds.width == pytest.approx(70.0)

    assert document.destroy_widget_recursive(root.handle)
    assert not root.alive
    assert not panel.alive
    assert not label.alive


def test_theme_style_inheritance_state_and_runtime_update():
    document = tc_ui_document_create()
    parent = Widget()
    child = Widget()
    document.adopt_root(parent, "style-parent")
    document.adopt(child, "style-child")
    assert parent.native.append_child(child.native)
    child.native.style_role = StyleRole.Button

    inherited = StyleOverride()
    inherited.fields = StyleField.FontSize | StyleField.Foreground
    inherited.flags = 1
    inherited.value.font_size = 19.0
    inherited.value.foreground = Color(1.0, 0.5, 0.25, 1.0)
    parent.native.style_override = inherited

    resolved = child.native.resolve_style()
    assert resolved.font_size == 19.0
    assert resolved.foreground.g == pytest.approx(0.5)

    local = StyleOverride()
    local.fields = StyleField.FontSize
    local.value.font_size = 23.0
    child.native.style_override = local
    resolved = child.native.resolve_style(int(StyleState.Hovered))
    assert resolved.font_size == 23.0
    assert resolved.background.r > 0.20

    initial_revision = document.theme_revision
    theme = document.theme
    theme.role(StyleRole.Button).base.font_size = 17.0
    theme.role(StyleRole.Button).base.background = Color(0.44, 0.2, 0.1, 1.0)
    child.native.clear_style_override()
    document.theme = theme
    assert document.theme_revision == initial_revision + 1
    assert child.native.resolve_style().font_size == 19.0
    assert child.native.resolve_style().background.r == pytest.approx(0.44)
    assert child.native.dirty_flags != 0


def test_default_theme_borders_are_opt_in():
    document = tc_ui_document_create()

    assert document.theme.role(StyleRole.Generic).base.border_width == 0.0
    assert document.theme.role(StyleRole.Panel).base.border_width == 0.0
    assert document.theme.role(StyleRole.Label).base.border_width == 0.0
    assert document.theme.role(StyleRole.Button).base.border_width == 0.0
    assert document.theme.role(StyleRole.Separator).base.border_width == 1.0
    assert document.theme.role(StyleRole.Checkbox).base.border_width == 1.0


def test_renderer_font_exposes_document_text_metrics():
    document = tc_ui_document_create()
    renderer = DrawListRenderer()
    assert renderer.set_default_font_path(str(_bundled_font_path()), 14)
    renderer.bind_text_measurer(document)

    narrow = document.measure_text("iii", 18.0)
    wide = document.measure_text("WWW", 18.0)
    assert wide.width > narrow.width
    assert wide.line_height > 0.0

    del renderer
    gc.collect()
    assert document.measure_text("still alive", 14.0).width > 0.0


def test_native_rich_text_model_view_wrap_selection_and_lifetime():
    model = RichTextModel()
    model.set_html("<pre>A<br><span style='color:#50fa7b; font-weight:bold; font-style:italic'>B &amp; λ</span></pre>")
    assert model.text == "A\nB & λ"
    segment = model.lines[1][0]
    assert segment.style.bold
    assert segment.style.italic
    assert segment.style.color.g == pytest.approx(250 / 255.0)

    model.set_lines([[RichTextSegment("alpha beta gamma", RichTextStyle(Color(0.2, 0.8, 0.3, 1.0), True))]])
    document = tc_ui_document_create()
    renderer = DrawListRenderer()
    assert renderer.set_default_font_path(str(_bundled_font_path()), 14)
    renderer.bind_text_measurer(document)
    clipboard = {"text": ""}
    document.set_clipboard_handlers(
        lambda: clipboard["text"],
        lambda text: clipboard.__setitem__("text", text),
    )
    view = document.create_rich_text_view(model)
    assert document.add_root(view.handle)
    document.layout_roots(Rect(0.0, 0.0, 70.0, 60.0))
    assert view.visual_line_count > 1
    view.select_all()
    assert view.selected_text == "alpha beta gamma"
    assert document.set_focus(view.handle)
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.C
    key.modifiers = int(ModifierFlag.Ctrl)
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert clipboard["text"] == "alpha beta gamma"

    del model
    gc.collect()
    assert view.model.text == "alpha beta gamma"
    stale_handle = view.handle
    assert document.destroy_widget_recursive(view.handle)
    assert not view.widget.alive
    assert stale_handle == view.handle


def test_native_frame_time_graph_uses_bounded_injected_model():
    model = FrameTimeModel()
    model.max_samples = 3
    for sample in (10.0, 15.0, 20.0, 40.0):
        model.add_sample(sample)
    assert model.samples == [15.0, 20.0, 40.0]

    document = tc_ui_document_create()
    graph = document.create_frame_time_graph(model)
    assert graph.model is model
    assert graph.target_frame_ms == pytest.approx(1000 / 60)
    assert graph.warning_frame_ms == pytest.approx(1000 / 30)
    assert document.add_root(graph.handle)
    document.layout_roots(Rect(0.0, 0.0, 300.0, 80.0))
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    fills = [command for command in draw_list.commands if command.type == DrawCommandType.FillRect]
    assert len(fills) == 4
    assert fills[1].color.g > fills[1].color.r
    assert fills[2].color.r > 0.7 and fills[2].color.g > 0.6
    assert fills[3].color.r > fills[3].color.g

    del model
    gc.collect()
    assert graph.model.samples == [15.0, 20.0, 40.0]
    with pytest.raises(ValueError, match="non-negative"):
        graph.model.add_sample(-1.0)


def test_native_frame_timeline_selection_follow_and_projection():
    model = FrameTimelineModel()
    model.set_samples(
        [
            FrameTimelineSample(
                index,
                16.0 + index,
                5.0,
                max(0.0, index - 2.0),
                16.0,
                index == 8,
            )
            for index in range(1, 11)
        ]
    )
    document = tc_ui_document_create()
    timeline = document.create_frame_timeline(model)
    selected = []
    timeline.connect_selection_changed(selected.append)
    assert document.add_root(timeline.handle)
    document.layout_roots(Rect(0.0, 0.0, 300.0, 180.0))
    assert timeline.selected_id == 10
    timeline.window_size = 8
    timeline.scroll_offset = 2
    assert timeline.visible_range == [0, 8]
    assert timeline.select(4)
    assert timeline.selected_id == 4
    assert not timeline.follow_latest
    assert selected[-1] == 4
    timeline.follow_latest = True
    assert timeline.selected_id == 10

    model.set_samples([FrameTimelineSample(20, 20.0, 7.0, target_ms=16.0)])
    assert timeline.selected_id == 20
    with pytest.raises(ValueError, match="unique"):
        model.set_samples([FrameTimelineSample(1, 1.0, 1.0), FrameTimelineSample(1, 2.0, 1.0)])


def test_native_text_input_utf8_selection_uses_injected_clipboard():
    clipboard = {"text": ""}
    document = tc_ui_document_create()
    document.set_clipboard_handlers(
        lambda: clipboard["text"],
        lambda text: clipboard.__setitem__("text", text),
    )
    widget = document.create_text_input("aé🙂b")
    changed = []
    submitted = []
    widget.connect_changed(changed.append)
    widget.connect_submitted(submitted.append)
    assert document.add_root(widget.handle)
    assert document.set_focus(widget.handle)

    widget.select(1, 7)
    assert widget.selected_text == "é🙂"
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.modifiers = int(ModifierFlag.Ctrl)
    key.key = KeyCode.C
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert clipboard["text"] == "é🙂"

    key.key = KeyCode.X
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.text == "ab"
    assert widget.caret == 1
    key.key = KeyCode.V
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.text == "aé🙂b"
    assert widget.caret == 7
    assert changed == ["ab", "aé🙂b"]

    key.modifiers = 0
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert submitted == ["aé🙂b"]


def test_native_text_area_multiline_selection_and_navigation():
    clipboard = {"text": ""}
    document = tc_ui_document_create()
    document.set_clipboard_handlers(
        lambda: clipboard["text"],
        lambda text: clipboard.__setitem__("text", text),
    )
    area = document.create_text_area("aé\nwide\n🙂z")
    assert document.add_root(area.handle)
    assert document.set_focus(area.handle)

    area.select(1, 9)
    assert area.selected_text == "é\nwide\n"
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.modifiers = int(ModifierFlag.Ctrl)
    key.key = KeyCode.X
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert clipboard["text"] == "é\nwide\n"
    assert area.text == "a🙂z"

    key.key = KeyCode.V
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert area.text == "aé\nwide\n🙂z"
    area.caret = len(area.text.encode())
    key.modifiers = 0
    key.key = KeyCode.Home
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert area.caret == 9


def test_native_basic_input_and_media_widget_factories():
    document = tc_ui_document_create()

    button = document.create_button("Apply")
    button.widget.bounds = Rect(0.0, 0.0, 80.0, 28.0)
    button_clicks = []
    button.connect_clicked(lambda: button_clicks.append(True))
    button.set_text("Commit")
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 4.0
    pointer.y = 4.0
    assert button.widget.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Up
    assert button.widget.dispatch_pointer_event(pointer) == EventResult.Handled
    assert button_clicks == [True]

    checkbox = document.create_checkbox(True)
    checkbox.widget.bounds = Rect(0.0, 0.0, 22.0, 22.0)
    checkbox_changes = []
    checkbox.connect_changed(checkbox_changes.append)
    checkbox.checked = False
    assert not checkbox.checked
    assert checkbox_changes == [False]

    group = document.create_group_box("Rendering", "python-group")
    group_content = document.create_panel("group-content")
    replacement = document.create_panel("group-replacement")
    group.title = "Display"
    group.set_padding(EdgeInsets(8.0, 6.0, 10.0, 12.0))
    group.set_background(Color(0.1, 0.2, 0.3, 1.0))
    group.set_border(Color(0.8, 0.9, 1.0, 1.0), 2.0)
    group.set_content(group_content)
    assert group.title == "Display"
    assert group.content_handle == group_content.handle
    group.set_content(replacement)
    assert group.content_handle == replacement.handle
    assert group_content.parent is None
    assert group_content.alive
    group.widget.layout(Rect(0.0, 0.0, 180.0, 120.0))
    assert replacement.bounds.x == pytest.approx(8.0)
    assert replacement.bounds.y == pytest.approx(36.0)
    assert replacement.bounds.width == pytest.approx(162.0)
    assert replacement.bounds.height == pytest.approx(72.0)
    group_draw_list = DrawList()
    group.widget.paint(PaintContext(group_draw_list))
    assert any(
        command.type == DrawCommandType.Text and command.text == "Display"
        for command in group_draw_list.commands
    )
    assert document.destroy_widget_recursive(group.handle)
    assert not group.widget.alive
    assert not replacement.alive
    assert group_content.alive
    with pytest.raises(RuntimeError, match="stale"):
        _ = group.title

    scroll = document.create_scroll_area("python-scroll")
    scroll_content = document.create_vstack("python-scroll-content")
    scroll_content.preferred_size = Size(200.0, 300.0)
    scroll.set_content(scroll_content)
    scroll.set_scroll_axes(False, True)
    scroll.widget.layout(Rect(0.0, 0.0, 100.0, 80.0))
    assert scroll.content_handle == scroll_content.handle
    assert not scroll.horizontal_scroll_enabled
    assert scroll.vertical_scroll_enabled
    assert scroll.content_size.width == pytest.approx(100.0)
    assert scroll.content_size.height == pytest.approx(300.0)
    scroll.scroll_x = 48.0
    assert scroll.scroll_x == pytest.approx(0.0)
    scroll.scroll_y = 48.0
    assert scroll.scroll_y == pytest.approx(48.0)

    spin = document.create_spin_box(2.0)
    spin_changes = []
    spin.connect_changed(spin_changes.append)
    spin.set_range(-5.0, 5.0)
    spin.step = 0.5
    spin.decimals = 1
    spin.value = 3.5
    assert spin.value == pytest.approx(3.5)
    assert spin_changes == [pytest.approx(3.5)]

    tabs = document.create_tab_view("python-tabs")
    first_page = document.create_panel("first-page")
    second_page = document.create_panel("second-page")
    tab_changes = []
    tabs.connect_selection_changed(tab_changes.append)
    tabs.add_page("First", first_page)
    tabs.add_page("Second", second_page)
    assert tabs.page_count == 2
    assert tabs.page_title(1) == "Second"
    assert tabs.page_handle(0) == first_page.handle
    tabs.selected_index = 1
    assert tab_changes == [1]
    tabs.set_page_title(1, "Renamed")
    assert tabs.page_title(1) == "Renamed"

    splitter = document.create_splitter(True, "python-splitter")
    split_first = document.create_panel("split-first")
    split_second = document.create_panel("split-second")
    splitter.set_first(split_first)
    splitter.set_second(split_second)
    assert splitter.divider_thickness == pytest.approx(4.0)
    splitter.set_min_extents(40.0, 50.0)
    splitter.set_divider_thickness(6.0)
    splitter.split_fraction = 0.25
    splitter.widget.layout(Rect(0.0, 0.0, 200.0, 80.0))
    assert splitter.split_fraction == pytest.approx(0.25)
    assert split_first.bounds.width == pytest.approx(48.5)
    assert split_second.bounds.width == pytest.approx(145.5)
    assert tabs.remove_page(1)
    assert tabs.page_count == 1
    assert tabs.selected_index == 0
    assert tab_changes == [1, 0]

    slider_edit = document.create_slider_edit(0.25)
    slider_edit.set_range(0.0, 1.0)
    slider_edit.set_step(0.05)
    slider_edit.label = "Exposure"
    slider_edit.widget.layout(Rect(0.0, 0.0, 300.0, 52.0))
    assert slider_edit.slider_handle
    assert slider_edit.spin_box_handle
    assert len(slider_edit.widget.children) == 2

    combo = document.create_combo_box()
    combo.add_item("First")
    combo.add_item("Second")
    combo_changes = []
    combo.connect_changed(lambda index, text: combo_changes.append((index, text)))
    combo.selected_index = 1
    assert combo.item_count == 2
    assert combo.item_text(1) == "Second"
    assert combo.selected_text == "Second"
    assert combo_changes == [(1, "Second")]

    icon = document.create_icon_button("I")
    icon.widget.bounds = Rect(0.0, 0.0, 28.0, 28.0)
    clicks = []
    icon.connect_clicked(lambda: clicks.append(True))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 4.0
    pointer.y = 4.0
    assert icon.widget.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Up
    assert icon.widget.dispatch_pointer_event(pointer) == EventResult.Handled
    assert clicks == [True]

    image = document.create_image_widget()
    assert image.intrinsic_size.width == pytest.approx(64.0)
    assert image.fit == ImageFit.Contain
    image.set_preserve_aspect(False)
    assert image.fit == ImageFit.Stretch
    image.fit = ImageFit.Cover
    assert image.fit == ImageFit.Cover

    label = document.create_label("Loading")
    assert label.text == "Loading"
    label.text = "Complete"
    assert label.text == "Complete"

    progress = document.create_progress_bar(0.25)
    assert progress.value == pytest.approx(0.25)
    progress.value = 0.75
    assert progress.value == pytest.approx(0.75)

    canvas = document.create_canvas()
    canvas.widget.bounds = Rect(0.0, 0.0, 160.0, 100.0)
    assert canvas.fit_mode
    assert canvas.zoom == pytest.approx(1.0)
    assert canvas.texture_sampling == TextureSampling.Linear
    canvas.texture_sampling = TextureSampling.Nearest
    assert canvas.texture_sampling == TextureSampling.Nearest
    zooms = []
    points = []
    canvas.connect_zoom_changed(zooms.append)
    canvas.connect_pointer_input(lambda point, _event: points.append(point))
    canvas.set_zoom(2.0, Point(80.0, 50.0))
    assert not canvas.fit_mode
    assert zooms == [pytest.approx(2.0)]
    pointer = PointerEvent()
    pointer.type = PointerEventType.Move
    pointer.x = 80.0
    pointer.y = 50.0
    assert canvas.widget.dispatch_pointer_event(pointer) == EventResult.Handled
    assert points[-1].x == pytest.approx(80.0)
    assert points[-1].y == pytest.approx(50.0)
    canvas.clear_texture()
    paints = []

    def paint_overlay(context):
        paints.append(True)
        context.draw_line(
            Point(0.0, 0.0),
            Point(10.0, 10.0),
            Color(1.0, 0.0, 0.0, 1.0),
            1.0,
        )

    canvas.set_paint_callback(paint_overlay)
    draw_list = DrawList()
    canvas.widget.paint(PaintContext(draw_list))
    assert paints == [True]
    assert [command.type for command in draw_list.commands] == [
        DrawCommandType.FillRect,
        DrawCommandType.PushClip,
        DrawCommandType.Line,
        DrawCommandType.PopClip,
    ]


def test_native_value_setters_propagate_callback_exceptions_immediately():
    document = tc_ui_document_create()

    def fail_spin(_value):
        raise ValueError("spin callback failed")

    spin = document.create_spin_box()
    spin.connect_changed(fail_spin)
    with pytest.raises(ValueError, match="spin callback failed"):
        spin.value = 1.0

    def fail_slider_edit(_value):
        raise RuntimeError("slider edit callback failed")

    slider_edit = document.create_slider_edit()
    slider_edit.connect_changed(fail_slider_edit)
    with pytest.raises(RuntimeError, match="slider edit callback failed"):
        slider_edit.value = 0.5

    def fail_combo(_index, _text):
        raise LookupError("combo callback failed")

    combo = document.create_combo_box()
    combo.add_item("First")
    combo.connect_changed(fail_combo)
    with pytest.raises(LookupError, match="combo callback failed"):
        combo.selected_index = 0
