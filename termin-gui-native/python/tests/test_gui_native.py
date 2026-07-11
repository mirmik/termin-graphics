import gc
import weakref
from pathlib import Path

import pytest

from termin.gui_native import (
    Color,
    ColorPickerModel,
    ColorPickerSurfaceKind,
    ColorPickerTextureIds,
    CollectionItem,
    CollectionModel,
    CommandData,
    CommandKind,
    CommandModel,
    Constraints,
    Document,
    DialogAction,
    DialogDismissReason,
    DrawCommandType,
    DrawList,
    DrawListRenderer,
    EdgeInsets,
    EventResult,
    FileDialogFilter,
    FileDialogMode,
    FileDialogModel,
    FrameTimeModel,
    GraphicsItem,
    GraphicsScene,
    KeyCode,
    KeyEvent,
    KeyEventType,
    LayoutPolicy,
    MessageBoxKind,
    MenuBarEntry,
    SelectionMode,
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
    SceneTransform,
    Size,
    StyleField,
    StyleOverride,
    StyleRole,
    StyleState,
    TableColumn,
    TableColumnModel,
    TableColumnPolicy,
    TableModel,
    TableRowData,
    TreeExpansionModel,
    TreeDropPosition,
    TreeModel,
    ViewportExternalDragEvent,
    ViewportExternalDragPhase,
    ViewportSurfaceHost,
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
    document = Document()
    showcase = build_python_showcase(document)
    renderer = DrawListRenderer()
    font = (
        Path(__file__).resolve().parents[3]
        / "termin-thirdparty"
        / "recastnavigation"
        / "RecastDemo"
        / "Bin"
        / "DroidSans.ttf"
    )
    assert renderer.set_default_font_path(str(font), 14)
    renderer.bind_text_measurer(document)

    document.layout_roots(Rect(0.0, 0.0, 800.0, 600.0))
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))

    assert showcase.root.stable_id == "python-showcase.root"
    assert document.live_widget_count == 15
    assert draw_list.command_count == 118
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
    document = Document()
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

        document = Document()
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
        document = Document()
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

        first_document = Document()
        second_document = Document()
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
    document = Document()
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
    assert "hovered" in snapshot["interaction"]
    assert "pressed" in snapshot["interaction"]

    by_handle = {item["handle"].index: item for item in snapshot["widgets"]}
    root_data = by_handle[root_handle.index]
    child_data = by_handle[child_handle.index]
    assert root_data["type_name"] == "PythonWidget"
    assert root_data["debug_name"] == "snapshot-root"
    assert root_data["parent"] is None
    assert root_data["children"] == [child_handle]
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
        source = Document()
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
        child.style_role = StyleRole.Button
        assert parent.append_child(child)
        assert source.add_root(parent.handle)

        serialized = source.serialize()
        assert serialized["$schema"] == "termin.gui.document"
        assert serialized["version"] == 1
        assert serialized["widgets"][0]["type"] == type_name
        assert serialized["widgets"][0]["state"]["value"] == 41
        assert serialized["widgets"][0]["children"] == [1]
        assert serialized["roots"] == [0]

        restored = Document()
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
        assert created[3].native.style_role == StyleRole.Button
        restored_snapshot = restored.inspect_snapshot()
        assert restored_snapshot["roots"] == [created[2].handle]

        with pytest.raises(RuntimeError, match="failed to restore"):
            restored.restore(serialized)
        assert restored.live_widget_count == 2

        malformed = source.serialize()
        malformed["widgets"][0]["children"][0] = 0
        rejected = Document()
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
    document = Document()
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

    document = Document()
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

    document = Document()
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
    document = Document()

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


def test_python_focus_events_and_tab_traversal():
    document = Document()
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

    document = Document()
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

    document = Document()
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

    document = Document()
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


def test_document_destruction_invalidates_widget_refs():
    widget = Widget()
    document = Document()
    document.adopt(widget, "document-child")
    native = widget.native
    assert native.alive

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

    document = Document()
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

    first_document = Document()
    second_document = Document()
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
    document = Document()
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
    document = Document()
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


def test_renderer_font_exposes_document_text_metrics():
    font_path = (
        Path(__file__).resolve().parents[3]
        / "termin-thirdparty"
        / "recastnavigation"
        / "RecastDemo"
        / "Bin"
        / "DroidSans.ttf"
    )
    document = Document()
    renderer = DrawListRenderer()
    assert renderer.set_default_font_path(str(font_path), 14)
    renderer.bind_text_measurer(document)

    narrow = document.measure_text("iii", 18.0)
    wide = document.measure_text("WWW", 18.0)
    assert wide.width > narrow.width
    assert wide.line_height > 0.0

    del renderer
    gc.collect()
    assert document.measure_text("still alive", 14.0).width > 0.0


def test_native_rich_text_model_view_wrap_selection_and_lifetime():
    font_path = (
        Path(__file__).resolve().parents[3]
        / "termin-thirdparty"
        / "recastnavigation"
        / "RecastDemo"
        / "Bin"
        / "DroidSans.ttf"
    )
    model = RichTextModel()
    model.set_html("<pre>A<br><span style='color:#50fa7b; font-weight:bold; font-style:italic'>B &amp; λ</span></pre>")
    assert model.text == "A\nB & λ"
    segment = model.lines[1][0]
    assert segment.style.bold
    assert segment.style.italic
    assert segment.style.color.g == pytest.approx(250 / 255.0)

    model.set_lines([[RichTextSegment("alpha beta gamma", RichTextStyle(Color(0.2, 0.8, 0.3, 1.0), True))]])
    document = Document()
    renderer = DrawListRenderer()
    assert renderer.set_default_font_path(str(font_path), 14)
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

    document = Document()
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


def test_native_text_input_utf8_selection_uses_injected_clipboard():
    clipboard = {"text": ""}
    document = Document()
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
    document = Document()
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
    document = Document()

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

    scroll = document.create_scroll_area("python-scroll")
    scroll_content = document.create_vstack("python-scroll-content")
    scroll_content.preferred_size = Size(200.0, 300.0)
    scroll.set_content(scroll_content)
    scroll.widget.layout(Rect(0.0, 0.0, 100.0, 80.0))
    assert scroll.content_handle == scroll_content.handle
    assert scroll.content_size.height == pytest.approx(300.0)
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
    image.set_preserve_aspect(False)

    canvas = document.create_canvas()
    canvas.widget.bounds = Rect(0.0, 0.0, 160.0, 100.0)
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
    document = Document()

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


def _collection_item(index, *, enabled=True, subtitle=""):
    return CollectionItem(f"item-{index}", f"Item {index}", subtitle, enabled)


def test_native_list_widget_model_selection_and_virtualized_paint():
    document = Document()
    model = CollectionModel()
    model.set_items([_collection_item(index, subtitle="even" if index % 2 == 0 else "odd") for index in range(10_000)])
    revision = model.revision
    widget = document.create_list_widget(model)
    assert widget.model is model
    assert widget.model.item_count == 10_000
    assert widget.model.item(7).stable_id == "item-7"
    assert document.add_root(widget.handle)
    widget.selection_mode = SelectionMode.Multiple
    widget.set_row_height(32.0)
    widget.set_row_spacing(2.0)
    document.layout_roots(Rect(0.0, 0.0, 320.0, 110.0))

    first, last = widget.visible_range
    assert first == 0
    assert last <= 6
    assert widget.content_height > 300_000.0

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 12

    changes = []
    widget.connect_selection_changed(lambda selected: changes.append(list(selected)))
    assert widget.select(2)
    assert widget.select(4, extend=True)
    assert widget.selected_indices == [2, 3, 4]
    assert changes == [[2], [2, 3, 4]]
    model.erase(0)
    assert widget.selected_indices == [1, 2, 3]
    assert changes[-1] == [1, 2, 3]
    widget.ensure_visible(9998)
    assert widget.visible_range[0] > 9990

    model.erase(model.item_count - 1)
    assert model.revision == revision + 2
    document.layout_roots(Rect(0.0, 0.0, 320.0, 110.0))
    assert widget.model.item_count == 9998


def test_native_list_widget_input_callbacks_and_model_lifetime():
    document = Document()
    model = CollectionModel()
    model.set_items(
        [
            _collection_item(0),
            _collection_item(1),
            _collection_item(2, enabled=False),
            _collection_item(3),
        ]
    )
    widget = document.create_list_widget(model)
    widget.selection_mode = SelectionMode.Multiple
    widget.set_row_height(30.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 60.0))

    del model
    gc.collect()
    assert widget.model.item_count == 4

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 10.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.selected_indices == [0]

    activated = []
    widget.connect_activated(lambda index, item: activated.append((index, item.stable_id)))
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_indices == [1]
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_indices == [3]
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(3, "item-3")]

    retained_model = widget.model
    assert document.destroy_widget(widget.handle)
    with pytest.raises(RuntimeError, match="stale"):
        _ = widget.selected_indices
    assert retained_model.item_count == 4


def test_host_click_count_is_exposed_and_drives_list_activation():
    document = Document()
    model = CollectionModel()
    model.append(CollectionItem("item", "Item"))
    widget = document.create_list_widget(model)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 80.0))
    activations = []
    widget.connect_activated(lambda index, item: activations.append((index, item.stable_id)))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 10.0
    pointer.y = 10.0
    pointer.click_count = 1
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == []
    pointer.click_count = 2
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(0, "item")]
    pointer.click_count = 3
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(0, "item")]


def test_native_list_widget_callback_exceptions_propagate():
    document = Document()
    model = CollectionModel()
    model.append(_collection_item(0))
    widget = document.create_list_widget(model)

    def fail_selection(_selected):
        raise RuntimeError("list selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="list selection failed"):
        widget.select(0)


def test_native_file_grid_widget_virtualizes_responsive_layout_and_textures():
    document = Document()
    model = CollectionModel()
    model.set_items(
        [
            CollectionItem(
                f"file-{index}",
                f"File {index}",
                ".txt",
                texture_id=77 if index == 0 else 0,
            )
            for index in range(10_000)
        ]
    )
    widget = document.create_file_grid_widget(model)
    widget.set_tile_size(50.0, 60.0)
    widget.set_tile_spacing(4.0)
    widget.set_padding(4.0)
    widget.set_icon_size(20.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 128.0))

    assert widget.model is model
    assert widget.column_count == 4
    assert widget.row_count == 2500
    assert widget.visible_range[1] <= 16
    assert widget.content_height > 150_000.0
    assert widget.has_scrollbar
    assert widget.scrollbar_thumb_rect.height >= 20.0

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 32
    textures = [command for command in draw_list.commands if command.type == DrawCommandType.Texture]
    assert len(textures) == 1
    assert textures[0].texture_id == 77

    assert widget.select(9999)
    assert widget.scroll_y > 150_000.0
    assert widget.visible_range[0] > 9980
    document.layout_roots(Rect(0.0, 0.0, 112.0, 128.0))
    assert widget.column_count == 2
    assert widget.row_count == 5000


def test_native_file_grid_widget_input_callbacks_lifetime_and_errors():
    document = Document()
    model = CollectionModel()
    model.set_items(
        [CollectionItem(f"file-{index}", f"File {index}", ".txt", enabled=index != 3) for index in range(20)]
    )
    widget = document.create_file_grid_widget(model)
    widget.set_tile_size(50.0, 30.0)
    widget.set_tile_spacing(0.0)
    widget.set_padding(0.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 120.0, 90.0))

    activated = []
    deleted = []
    contexts = []
    widget.connect_activated(lambda index, item: activated.append((index, item.stable_id)))
    widget.connect_delete_requested(lambda index, item: deleted.append((index, item.stable_id)))
    widget.connect_context_menu_requested(lambda index, x, y: contexts.append((index, x, y)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 10.0
    pointer.y = 10.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.current_index == 0
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 2
    key.key = KeyCode.Right
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 4
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(4, "file-4")]
    assert deleted == [(4, "file-4")]

    pointer.button = 1
    pointer.x = 105.0
    pointer.y = 85.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts[-1][0] == -1

    pointer.button = 0
    pointer.x = 118.0
    pointer.y = 5.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == widget.handle
    pointer.type = PointerEventType.Move
    pointer.y = 50.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.scroll_y > 0.0
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert not document.pointer_capture

    del model
    gc.collect()
    assert widget.model.item_count == 20

    def fail_selection(_selected):
        raise RuntimeError("file grid selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="file grid selection failed"):
        widget.select(1)


def test_native_command_model_toolbar_and_status_bar_contracts():
    document = Document()
    model = CommandModel()
    save = model.append(
        CommandData(
            "save",
            "Save",
            icon="S",
            shortcut="Ctrl+S",
            tooltip="Save scene",
        )
    )
    model.append(CommandData("separator", kind=CommandKind.Separator))
    snap = model.append(CommandData("snap", "Snap", checkable=True))
    model.append(CommandData("disabled", "Disabled", enabled=False))
    toolbar = document.create_tool_bar(model)
    assert toolbar.model is model
    assert document.add_root(toolbar.handle)
    document.layout_roots(Rect(0.0, 0.0, 360.0, 40.0))
    assert model.command_count == 4
    assert len(toolbar.item_rects) == 4
    assert toolbar.item_rects[0].width >= toolbar.item_height
    assert toolbar.item_rects[1].width < toolbar.item_height

    activations = []
    toolbar.connect_activated(
        lambda index, command_id, command: activations.append((index, command_id, command.stable_id, command.checked))
    )
    pointer = PointerEvent()
    pointer.type = PointerEventType.Move
    pointer.x = toolbar.item_rects[0].x + 2.0
    pointer.y = 20.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert toolbar.hovered_tooltip == "Save scene"

    pointer.type = PointerEventType.Down
    pointer.x = toolbar.item_rects[2].x + 2.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == toolbar.handle
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(2, snap, "snap", True)]
    assert model.command(snap).data.checked
    model.set_enabled(save, False)
    assert not model.command(save).data.enabled

    status = document.create_status_bar("Ready")
    assert status.displayed_text == "Ready"
    status.show_message("Saved ✓")
    status.text = "Idle"
    assert status.has_message
    assert status.displayed_text == "Saved ✓"
    status.clear_message()
    assert status.displayed_text == "Idle"


def test_native_toolbar_model_lifetime_and_callback_errors():
    document = Document()
    model = CommandModel()
    model.append(CommandData("run", "Run"))
    toolbar = document.create_tool_bar(model)
    assert document.add_root(toolbar.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 40.0))
    del model
    gc.collect()
    assert toolbar.model.command_count == 1

    def fail_activation(_index, _command_id, _command):
        raise RuntimeError("toolbar activation failed")

    toolbar.connect_activated(fail_activation)
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = toolbar.item_rects[0].x + 2.0
    pointer.y = 20.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Up
    with pytest.raises(RuntimeError, match="toolbar activation failed"):
        document.dispatch_pointer_event(pointer)


def test_native_menu_and_menu_bar_overlay_shortcut_contracts():
    document = Document()
    submenu = CommandModel()
    submenu.append(CommandData("recent-scene", "Scene.termin"))
    model = CommandModel()
    model.append(CommandData("disabled", "Disabled", enabled=False))
    model.append(CommandData("recent", "Recent", submenu=submenu))
    model.append(CommandData("save", "Save", shortcut="Ctrl+S", checkable=True))
    model.append(CommandData("profiler", "Profiler", shortcut="F7", checkable=True))
    menu = document.create_menu(model)
    menu.max_visible_height = 64.0
    activations = []
    menu.connect_activated(
        lambda index, command_id, command: activations.append((index, command_id, command.stable_id))
    )
    assert menu.show(Point(390.0, 290.0), Rect(0.0, 0.0, 400.0, 300.0))
    assert document.overlay_count == 1
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert menu.current_index == 1
    key.key = KeyCode.Right
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert document.overlay_count == 2
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activations[0][2] == "recent-scene"
    assert document.overlay_count == 0

    bar = document.create_menu_bar()
    bar.entries = [MenuBarEntry("file", "File", model)]
    assert document.add_root(bar.handle)
    document.layout_roots(Rect(0.0, 0.0, 400.0, 30.0))
    bar_activations = []
    bar.connect_activated(
        lambda menu_index, command_id, command: bar_activations.append(
            (menu_index, command_id, command.stable_id, command.checked)
        )
    )
    assert bar.dispatch_shortcut(ord("s"), int(ModifierFlag.Ctrl))
    assert bar_activations[0][2:] == ("save", True)
    assert bar.dispatch_shortcut(KeyCode.F7.value, 0)
    assert bar_activations[1][2:] == ("profiler", True)


def test_native_dialog_message_box_and_input_dialog_contracts():
    document = Document()
    dialog = document.create_dialog("Confirm")
    dialog.actions = [
        DialogAction("apply", "Apply", is_default=True),
        DialogAction("cancel", "Cancel", is_cancel=True),
    ]
    results = []
    dialog.connect_finished(lambda result: results.append((result.action_id, result.reason)))
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.overlay_count == 1
    escape = KeyEvent()
    escape.type = KeyEventType.Down
    escape.key = KeyCode.Escape
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert results == [("cancel", DialogDismissReason.Escape)]
    assert not dialog.open

    message = document.create_message_box("Delete", "Delete selected entity?", MessageBoxKind.Question)
    message_results = []
    message.connect_finished(lambda result: message_results.append(result.action_id))
    assert message.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert message_results == ["no"]

    input_dialog = document.create_input_dialog("Rename", "New name", "Old name")
    values = []
    input_dialog.connect_value_finished(values.append)
    assert input_dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    input_dialog.value = "New name"
    enter = KeyEvent()
    enter.type = KeyEventType.Down
    enter.key = KeyCode.Enter
    assert document.dispatch_key_event(enter) == EventResult.Handled
    assert values == ["New name"]
    assert input_dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert values == ["New name", None]


def test_native_file_dialog_model_and_overlay_contract(tmp_path: Path):
    folder = tmp_path / "folder"
    folder.mkdir()
    text_file = tmp_path / "readme.TXT"
    text_file.write_text("hello", encoding="utf-8")
    (tmp_path / "image.png").write_bytes(b"png")

    filters = FileDialogModel.parse_filter_string("Text | *.txt;;Images | *.png")
    assert [(item.label, item.patterns) for item in filters] == [
        ("Text", ["*.txt"]),
        ("Images", ["*.png"]),
    ]
    model = FileDialogModel(FileDialogMode.OpenFile)
    model.set_filters(filters)
    assert model.navigate(str(tmp_path))
    assert [entry.name for entry in model.entries] == ["folder", "readme.TXT"]
    file_index = next(index for index, entry in enumerate(model.entries) if entry.name == "readme.TXT")
    assert model.select(file_index)
    assert model.confirm().path == str(text_file)

    save = FileDialogModel(FileDialogMode.SaveFile)
    assert save.navigate(str(tmp_path))
    save.file_name = "scene.termin"
    assert save.confirm().path == str(tmp_path / "scene.termin")

    document = Document()
    dialog = document.create_file_dialog(FileDialogMode.OpenFile)
    dialog.set_initial_directory(str(tmp_path))
    dialog.set_filters([FileDialogFilter("Text", ["*.txt"])])
    results = []
    dialog.connect_path_finished(results.append)
    assert dialog.show(Rect(0.0, 0.0, 800.0, 600.0))
    assert not dialog.activate("accept")
    assert dialog.open
    file_index = next(index for index, entry in enumerate(dialog.model.entries) if entry.name == "readme.TXT")
    assert dialog.model.select(file_index)
    assert dialog.activate("accept")
    assert results == [str(text_file)]


def test_native_color_picker_surfaces_and_dialog_contract():
    model = ColorPickerModel(Color(1.0, 0.0, 0.0, 0.5), show_alpha=True)
    assert model.hue == pytest.approx(0.0)
    assert model.saturation == pytest.approx(1.0)
    model.hue = 0.5
    assert model.color.g == pytest.approx(1.0)
    assert model.color.b == pytest.approx(1.0)

    document = Document()
    picker = document.create_color_picker(model)
    assert picker.model is model
    sv = picker.surface(ColorPickerSurfaceKind.SaturationValue)
    assert (sv.width, sv.height, len(sv.rgba)) == (64, 64, 64 * 64 * 4)
    invalidated = []
    picker.connect_surfaces_invalidated(invalidated.append)
    model.hue = 0.25
    assert invalidated
    assert picker.surface(ColorPickerSurfaceKind.SaturationValue).revision == model.revision

    assert document.add_root(picker.handle)
    document.layout_roots(Rect(0.0, 0.0, 250.0, 244.0))
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.FillRect for command in draw_list.commands) > 600
    picker.texture_ids = ColorPickerTextureIds(11, 12, 13)
    draw_list.clear()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Texture for command in draw_list.commands) == 3

    dialog = document.create_color_dialog(Color(1.0, 0.0, 0.0, 0.5), show_alpha=True)
    results = []
    dialog.connect_color_finished(results.append)
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    dialog.color = Color(0.0, 0.5, 1.0, 0.25)
    assert dialog.activate("ok")
    assert results[0].g == pytest.approx(0.5)
    assert results[0].a == pytest.approx(0.25)
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert dialog.activate("cancel")
    assert results[1] is None


def test_native_tree_model_widget_virtualization_and_navigation():
    document = Document()
    model = TreeModel()
    expansion = TreeExpansionModel()
    roots = []
    for root_index in range(100):
        root = model.append_root(_collection_item(root_index))
        roots.append(root)
        expansion.set_expanded(root, True)
        for child_index in range(100):
            model.append_child(
                root,
                CollectionItem(
                    f"node-{root_index}-{child_index}",
                    f"Node {root_index}/{child_index}",
                ),
            )
    widget = document.create_tree_widget(model, expansion)
    assert widget.model is model
    assert widget.expansion_model is expansion
    assert document.add_root(widget.handle)
    widget.set_row_height(24.0)
    widget.set_row_spacing(1.0)
    document.layout_roots(Rect(0.0, 0.0, 320.0, 100.0))

    assert model.node_count == 10_100
    assert widget.visible_count == 10_100
    assert widget.visible_range[1] <= 6
    assert widget.visible_row(0).node == roots[0]
    assert widget.visible_row(1).depth == 1
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 12

    last = model.children(roots[-1])[-1]
    assert widget.select(last)
    assert widget.selected_node == last
    assert widget.visible_range[0] > 10_000

    moved = model.children(roots[0])[0]
    model.move(moved, roots[1])
    assert model.node(moved).parent == roots[1]
    with pytest.raises(ValueError, match="cycle"):
        model.move(roots[1], moved)


def test_native_tree_widget_pointer_callbacks_reconcile_and_propagate_errors():
    document = Document()
    model = TreeModel()
    root = model.append_root(CollectionItem("root", "Root"))
    first = model.append_child(root, CollectionItem("first", "First"))
    disabled = model.append_child(root, CollectionItem("disabled", "Disabled", enabled=False))
    last = model.append_child(root, CollectionItem("last", "Last"))
    widget = document.create_tree_widget(model)
    assert document.add_root(widget.handle)
    widget.set_row_height(30.0)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 90.0))

    expansions = []
    selections = []
    activated = []
    deleted = []
    contexts = []
    widget.connect_expansion_changed(lambda node, value: expansions.append((node, value)))
    widget.connect_selection_changed(lambda node: selections.append(node))
    widget.connect_activated(lambda node, item: activated.append((node, item.stable_id)))
    widget.connect_delete_requested(lambda node, item: deleted.append((node, item.stable_id)))
    widget.connect_context_menu_requested(lambda node, x, y: contexts.append((node, x, y)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 5.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert expansions == [(root, True)]

    pointer.x = 40.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.selected_node == root
    pointer.button = 1
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts == [(root, 40.0, 15.0)]
    pointer.button = 0
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_node == first
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_node == last
    assert widget.selected_node != disabled
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(last, "last")]
    assert deleted == [(last, "last")]

    model.erase(last)
    assert widget.selected_node == 0
    assert selections[-1] == 0

    def fail_selection(_node):
        raise RuntimeError("tree selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="tree selection failed"):
        widget.select(first)


def test_native_tree_widget_drag_drop_signal_reports_position():
    document = Document()
    model = TreeModel()
    first = model.append_root(CollectionItem("first", "First"))
    second = model.append_root(CollectionItem("second", "Second"))
    widget = document.create_tree_widget(model)
    widget.draggable = True
    assert document.add_root(widget.handle)
    widget.set_row_height(30.0)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 90.0))
    drops = []
    widget.connect_drop_requested(lambda dragged, target, position: drops.append((dragged, target, position)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 40.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Move
    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.dragging
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert drops == [(first, second, TreeDropPosition.Inside)]


def test_native_table_widget_models_layout_and_virtualized_paint():
    document = Document()
    model = TableModel()
    model.set_rows([TableRowData(f"row-{index}", [f"Row {index}", str(index), "Ready"]) for index in range(10_000)])
    columns = TableColumnModel()
    columns.set_columns(
        [
            TableColumn(
                "name",
                "Name",
                TableColumnPolicy.Fixed,
                width=80.0,
                min_width=60.0,
            ),
            TableColumn("value", "Value", min_width=40.0),
            TableColumn("state", "State", min_width=40.0, max_width=160.0, stretch=2.0),
        ]
    )
    widget = document.create_table_widget(model, columns)
    assert widget.model is model
    assert widget.column_model is columns
    assert document.add_root(widget.handle)
    widget.set_row_height(24.0)
    widget.set_header_height(28.0)
    document.layout_roots(Rect(0.0, 0.0, 400.0, 128.0))

    assert model.row_count == 10_000
    assert columns.column_count == 3
    assert widget.visible_range[1] <= 6
    assert widget.content_height == pytest.approx(240_000.0)
    assert widget.column_layout[0].width == pytest.approx(80.0)
    assert widget.column_layout[2].width == pytest.approx(160.0)
    assert widget.column_layout[2].x + widget.column_layout[2].width == pytest.approx(400.0)

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 21

    changes = []
    widget.selection_mode = SelectionMode.Multiple
    widget.connect_selection_changed(lambda selected: changes.append(list(selected)))
    assert widget.select(2)
    assert widget.select(4, extend=True)
    assert widget.selected_indices == [2, 3, 4]
    first_id = model.row_at(0).id
    model.erase(first_id)
    assert widget.selected_indices == [1, 2, 3]
    assert changes[-1] == [1, 2, 3]
    widget.ensure_visible(9998)
    assert widget.visible_range[0] > 9990


def test_native_table_widget_input_resize_callbacks_lifetime_and_errors():
    document = Document()
    model = TableModel()
    first = model.append(TableRowData("first", ["First", "1"]))
    model.append(TableRowData("disabled", ["Disabled", "2"], enabled=False))
    last = model.append(TableRowData("last", ["Last", "3"]))
    columns = TableColumnModel()
    columns.set_columns(
        [
            TableColumn(
                "name",
                "Name",
                TableColumnPolicy.Fixed,
                width=100.0,
                min_width=60.0,
                max_width=180.0,
            ),
            TableColumn("value", "Value"),
        ]
    )
    widget = document.create_table_widget(model, columns)
    widget.set_row_height(30.0)
    widget.set_header_height(30.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 260.0, 120.0))

    headers = []
    resized = []
    activated = []
    contexts = []
    widget.connect_header_clicked(lambda index, column: headers.append((index, column.stable_id)))
    widget.connect_column_resized(lambda index, width: resized.append((index, width)))
    widget.connect_activated(lambda index, row, data: activated.append((index, row, data.stable_id)))
    widget.connect_context_menu_requested(lambda index, x, y: contexts.append((index, x, y)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 20.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert headers == [(0, "name")]

    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.current_index == 0
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 2
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(2, last, "last")]

    pointer.button = 1
    pointer.x = 20.0
    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts == [(0, 20.0, 45.0)]
    pointer.button = 0

    pointer.type = PointerEventType.Down
    pointer.x = 100.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == widget.handle
    pointer.type = PointerEventType.Move
    pointer.x = 140.0
    pointer.y = -50.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert resized[-1] == pytest.approx((0, 140.0))
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert not document.pointer_capture

    model.insert(0, TableRowData("inserted", ["Inserted", "0"]))
    assert model.index_of(first) == 1
    del model
    del columns
    gc.collect()
    assert widget.model.row_count == 4
    assert widget.column_model.column_count == 2

    def fail_selection(_selected):
        raise RuntimeError("table selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="table selection failed"):
        widget.select(0)


def test_native_viewport3d_surface_protocol_input_drag_and_lifetime():
    class SurfaceHost:
        def __init__(self):
            self.valid = True
            self.size = (64, 64)
            self.texture_id = 91
            self.calls = []

        def is_valid(self):
            return self.valid

        def get_tgfx_color_tex_id(self):
            return self.texture_id

        def framebuffer_size(self):
            return self.size

        def resize(self, width, height):
            self.calls.append(("resize", width, height))
            self.size = (width, height)
            return True

        def dispatch_pointer_move(self, x, y):
            self.calls.append(("move", x, y))
            return True

        def dispatch_pointer_button(self, button, action, modifiers, click_count):
            self.calls.append(("button", button, action, modifiers, click_count))
            return True

        def dispatch_scroll(self, x, y, modifiers):
            self.calls.append(("scroll", x, y, modifiers))
            return True

        def dispatch_key(self, key, scancode, action, modifiers):
            self.calls.append(("key", key, scancode, action, modifiers))
            return True

        def dispatch_text(self, codepoint):
            self.calls.append(("text", codepoint))
            return True

    document = Document()
    viewport = document.create_viewport3d()
    assert document.add_root(viewport.handle)
    surface = SurfaceHost()
    assert isinstance(surface, ViewportSurfaceHost)
    weak_surface = weakref.ref(surface)
    ordering = []
    viewport.connect_before_resize(
        lambda previous, next_size: ordering.append(
            (previous.width, previous.height, next_size.width, next_size.height)
        )
    )
    document.layout_roots(Rect(10.0, 20.0, 300.8, 180.9))
    viewport.set_surface_host(surface)
    assert ordering == [(64, 64, 300, 180)]
    assert surface.calls == [("resize", 300, 180)]
    assert viewport.has_surface
    assert viewport.surface_valid
    assert viewport.surface_size.width == 300

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    texture_commands = [command for command in draw_list.commands if command.type == DrawCommandType.Texture]
    assert len(texture_commands) == 1
    assert texture_commands[0].texture_id == 91

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 42.0
    pointer.y = 65.0
    pointer.button = 1
    pointer.click_count = 2
    pointer.modifiers = 7
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert surface.calls[-2:] == [
        ("move", 32.0, 45.0),
        ("button", 1, 1, 7, 2),
    ]

    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.A
    key.scancode = 9
    key.modifiers = 3
    key.repeat = True
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert surface.calls[-1] == ("key", 65, 9, 2, 3)
    assert document.dispatch_text_event("AЖ") == EventResult.Handled
    assert surface.calls[-2:] == [("text", ord("A")), ("text", ord("Ж"))]

    drops = []
    viewport.set_external_drag_handler(lambda event: drops.append(event.payload) or True)
    drag = ViewportExternalDragEvent(
        ViewportExternalDragPhase.Drop,
        "text/uri-list",
        "file:///tmp/scene.tscene",
        12.0,
        14.0,
    )
    assert viewport.dispatch_external_drag(drag)
    assert drops == ["file:///tmp/scene.tscene"]

    del surface
    gc.collect()
    assert weak_surface() is not None
    viewport.detach_surface()
    gc.collect()
    assert weak_surface() is None
    assert not viewport.has_surface


def test_native_viewport3d_stale_surface_and_destroy_release_are_safe():
    class StaleSurface:
        def __init__(self):
            self.valid = False

        def is_valid(self):
            return self.valid

        def get_tgfx_color_tex_id(self):
            raise AssertionError("stale texture must not be queried")

        def framebuffer_size(self):
            raise AssertionError("stale size must not be queried")

        def resize(self, width, height):
            raise AssertionError("stale surface must not resize")

        def dispatch_pointer_move(self, x, y):
            raise AssertionError("stale surface must not receive input")

        def dispatch_pointer_button(self, button, action, modifiers, click_count):
            raise AssertionError("stale surface must not receive input")

        def dispatch_scroll(self, x, y, modifiers):
            raise AssertionError("stale surface must not receive input")

        def dispatch_key(self, key, scancode, action, modifiers):
            raise AssertionError("stale surface must not receive input")

        def dispatch_text(self, codepoint):
            raise AssertionError("stale surface must not receive input")

    document = Document()
    viewport = document.create_viewport3d()
    assert document.add_root(viewport.handle)
    surface = StaleSurface()
    weak_surface = weakref.ref(surface)
    viewport.set_surface_host(surface)
    del surface
    gc.collect()
    assert weak_surface() is not None
    assert not viewport.surface_valid
    assert viewport.texture_id == 0
    document.layout_roots(Rect(0.0, 0.0, 200.0, 100.0))
    assert document.destroy_widget(viewport.handle)
    gc.collect()
    assert weak_surface() is None


def test_display_fbo_surface_implements_native_viewport_protocol():
    from termin.display import FBOSurface

    protocol_methods = (
        "is_valid",
        "get_tgfx_color_tex_id",
        "framebuffer_size",
        "resize",
        "dispatch_pointer_move",
        "dispatch_pointer_button",
        "dispatch_scroll",
        "dispatch_key",
        "dispatch_text",
    )
    for method_name in protocol_methods:
        assert callable(getattr(FBOSurface, method_name))


def test_native_scene_view_model_transform_drag_callbacks_and_embedding():
    scene = GraphicsScene()
    node = GraphicsItem("node-a")
    node.position = Point(10.0, 20.0)
    node.size = Size(120.0, 70.0)
    node.draggable = True
    painted = []

    def paint_item(item, context, transform):
        painted.append((item.stable_id, transform.zoom))
        screen = transform.world_to_screen(item.world_position)
        context.fill_rect(
            Rect(screen.x, screen.y, item.size.width * transform.zoom, item.size.height * transform.zoom),
            Color(0.8, 0.2, 0.1, 1.0),
        )

    node.set_paint_callback(paint_item)
    assert scene.add_item(node)
    assert scene.hit_test(20.0, 30.0) is node

    edge = GraphicsItem("edge")
    edge.selectable = False
    edge.z_index = -10.0
    edge.set_hit_test_callback(lambda _item, x, y: abs(y) < 5.0 and 0.0 <= x <= 200.0)
    assert scene.add_item(edge)
    assert scene.hit_test(50.0, 2.0) is edge

    document = Document()
    view = document.create_scene_view(scene)
    assert document.add_root(view.handle)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    assert view.world_to_screen(Point(10.0, 20.0)).x == pytest.approx(110.0)

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert painted == [("node-a", 1.0)]
    assert any(command.type == DrawCommandType.FillRect for command in draw_list.commands)

    moved = []
    view.connect_item_moved(lambda item: moved.append(item.stable_id))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 120.0
    pointer.y = 80.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert scene.selected_items == [node]
    pointer.type = PointerEventType.Move
    pointer.x = 150.0
    pointer.y = 110.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert node.position.x == pytest.approx(40.0)
    assert node.position.y == pytest.approx(50.0)
    assert moved == ["node-a"]
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled

    anchor = Point(250.0, 160.0)
    before = view.screen_to_world(anchor)
    pointer.type = PointerEventType.Wheel
    pointer.wheel_y = 1.0
    pointer.x = anchor.x
    pointer.y = anchor.y
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    after = view.screen_to_world(anchor)
    assert view.zoom > 1.0
    assert after.x == pytest.approx(before.x)
    assert after.y == pytest.approx(before.y)

    embedded = document.create_button("Embedded")
    view.set_zoom(1.0, Point(100.0, 50.0))
    view.offset = Point(0.0, 0.0)
    editor_item = GraphicsItem("editor")
    editor_item.position = Point(5.0, 6.0)
    editor_item.size = Size(100.0, 30.0)
    editor_item.embedded_widget = embedded.handle
    assert scene.add_item(editor_item)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    screen = view.world_to_screen(editor_item.position)
    assert document.hit_test(screen.x + 2.0, screen.y + 2.0) == embedded.handle
    assert scene.remove_item(editor_item)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    assert embedded.widget.parent is None
    view.set_pointer_handler(None)
    view.set_key_handler(None)
    view.set_text_handler(None)


def test_native_scene_transform_and_scene_view_handler_errors_propagate():
    transform = SceneTransform(10.0, 20.0, 2.0)
    screen = transform.world_to_screen(Point(3.0, 4.0))
    assert (screen.x, screen.y) == pytest.approx((16.0, 28.0))
    world = transform.screen_to_world(screen)
    assert (world.x, world.y) == pytest.approx((3.0, 4.0))

    document = Document()
    view = document.create_scene_view()
    assert document.add_root(view.handle)
    document.layout_roots(Rect(0.0, 0.0, 100.0, 100.0))

    def fail_pointer(_world, _event):
        raise RuntimeError("scene pointer failed")

    view.set_pointer_handler(fail_pointer)
    event = PointerEvent()
    event.type = PointerEventType.Down
    event.x = 10.0
    event.y = 10.0
    with pytest.raises(RuntimeError, match="scene pointer failed"):
        document.dispatch_pointer_event(event)
