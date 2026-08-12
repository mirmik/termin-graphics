#!/usr/bin/env python3

from __future__ import annotations

from termin.geombase import LinearColor

import os
import sys
import time

import tgfx
from termin.window import WindowedGraphicsSession, quit_sdl
from termin.gui_native import (
    tc_ui_document_create,
    DrawList,
    DrawListRenderer,
    PaintContext,
    Point,
    Rect,
    SrgbColor,
    tc_ui_document_destroy,
    Widget,
)


class DemoWidget(Widget):
    def paint(self, context: PaintContext) -> None:
        context.fill_rect(
            Rect(0.0, 0.0, 800.0, 600.0),
            SrgbColor(0.08, 0.09, 0.11, 1.0),
        )
        context.push_clip(Rect(40.0, 40.0, 720.0, 520.0))
        context.fill_rect(
            Rect(90.0, 90.0, 260.0, 120.0),
            SrgbColor(0.18, 0.42, 0.72, 1.0),
        )
        context.stroke_rect(
            Rect(90.0, 90.0, 260.0, 120.0),
            SrgbColor(0.85, 0.92, 1.0, 1.0),
            3.0,
        )
        context.fill_rect(
            Rect(120.0, 250.0, 180.0, 44.0),
            SrgbColor(0.20, 0.62, 0.42, 1.0),
        )
        context.stroke_rect(
            Rect(120.0, 250.0, 180.0, 44.0),
            SrgbColor(0.75, 1.0, 0.86, 1.0),
            2.0,
        )
        context.draw_line(
            Point(410.0, 120.0),
            Point(690.0, 320.0),
            SrgbColor(0.98, 0.72, 0.20, 1.0),
            5.0,
        )
        context.pop_clip()


def example_seconds() -> float:
    value = os.environ.get("TERMIN_GUI_NATIVE_EXAMPLE_SECONDS", "")
    if not value:
        return 0.0
    try:
        return max(float(value), 0.0)
    except ValueError:
        return 0.0


def main() -> int:
    if not tgfx.configure_default_shader_runtime("termin-gui-native-example"):
        return 77

    document = None
    runtime = None
    window = None
    context = None
    color_target = None
    renderer = None
    try:
        runtime = WindowedGraphicsSession.create_native()
        window = runtime.create_window("termin-gui-native Python rectangle example", 800, 600)
        graphics = tgfx.Tgfx2Context.from_runtime(runtime.graphics)
        context = graphics.context

        document = tc_ui_document_create()
        document.adopt_root(DemoWidget(), "DemoWidget")
        draw_list = DrawList()
        paint_context = PaintContext(draw_list)
        renderer = DrawListRenderer()

        target_width = 0
        target_height = 0

        max_seconds = example_seconds()
        start = time.monotonic()

        while not window.should_close():
            window.poll_events()

            width, height = window.framebuffer_size()
            if width <= 0 or height <= 0:
                continue

            if color_target is None or target_width != width or target_height != height:
                if color_target is not None:
                    context.destroy_texture(color_target)
                color_target = context.create_color_attachment(width, height)
                target_width = width
                target_height = height

            draw_list.clear()
            document.paint_roots(paint_context)

            context.begin_frame()
            context.begin_pass(
                color_target,
                clear_linear_color=LinearColor(0.03, 0.035, 0.045, 1.0),
            )
            renderer.render(context, draw_list, width, height)
            context.end_pass()
            context.end_frame()
            window.present(color_target)

            if max_seconds > 0.0 and time.monotonic() - start >= max_seconds:
                break

        return 0
    except Exception as exc:
        print(f"termin-gui-native Python rectangle example failed: {exc}", file=sys.stderr)
        message = str(exc)
        if (
            "No available video device" in message
            or "Vulkan support is either not configured in SDL" in message
        ):
            return 77
        return 1
    finally:
        if renderer is not None:
            try:
                renderer.release_gpu()
            except Exception as exc:
                print(f"termin-gui-native Python rectangle example GPU cleanup failed: {exc}", file=sys.stderr)
        if color_target is not None and context is not None:
            try:
                context.destroy_texture(color_target)
            except Exception as exc:
                print(f"termin-gui-native Python rectangle example texture cleanup failed: {exc}", file=sys.stderr)
        if window is not None:
            try:
                window.close()
            except Exception as exc:
                print(f"termin-gui-native Python rectangle example window cleanup failed: {exc}", file=sys.stderr)
        if runtime is not None:
            try:
                runtime.close()
            except Exception as exc:
                print(f"termin-gui-native Python rectangle example runtime cleanup failed: {exc}", file=sys.stderr)
        quit_sdl()
        if document is not None:
            tc_ui_document_destroy(document)


if __name__ == "__main__":
    raise SystemExit(main())
