import pytest

from termin.gui_native import (
    Color,
    Document,
    DrawCommandType,
    DrawList,
    PaintContext,
    Point,
    Rect,
    Widget,
)


class DemoWidget(Widget):
    def __init__(self):
        self.paint_count = 0

    def paint(self, context):
        self.paint_count += 1
        context.push_clip(Rect(0.0, 0.0, 64.0, 32.0))
        context.fill_rect(Rect(1.0, 2.0, 30.0, 10.0), Color(0.1, 0.2, 0.3, 1.0))
        context.draw_line(
            Point(4.0, 5.0),
            Point(6.0, 7.0),
            Color(0.8, 0.7, 0.6, 1.0),
            2.0,
        )
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
    assert draw_list.command_count == 4

    clip, fill, line, pop = draw_list.commands
    assert clip.type == DrawCommandType.PushClip
    assert clip.rect.width == 64.0
    assert fill.type == DrawCommandType.FillRect
    assert fill.rect.x == 1.0
    assert fill.rect.width == 30.0
    assert fill.color.g == pytest.approx(0.2)
    assert line.type == DrawCommandType.Line
    assert line.p0.x == 4.0
    assert line.p1.y == 7.0
    assert line.thickness == 2.0
    assert pop.type == DrawCommandType.PopClip


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
