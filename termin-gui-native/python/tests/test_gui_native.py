import gc
import weakref
from pathlib import Path

import pytest

from termin.gui_native import (
    Color,
    Constraints,
    Document,
    DrawCommandType,
    DrawList,
    DrawListRenderer,
    EventResult,
    KeyCode,
    KeyEvent,
    KeyEventType,
    ModifierFlag,
    OverlayDismissReason,
    OverlayFlag,
    PaintContext,
    Point,
    PointerEvent,
    PointerEventType,
    Rect,
    Size,
    StyleField,
    StyleOverride,
    StyleRole,
    StyleState,
    Widget,
    tooltip_rect,
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
