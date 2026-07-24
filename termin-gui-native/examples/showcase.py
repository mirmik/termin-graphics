#!/usr/bin/env python3

from __future__ import annotations

import os
from pathlib import Path
import sys
import time

import tgfx
from termin.display import WindowedGraphicsSession, quit_sdl
from termin.gui_native import (
    DrawList,
    DrawListRenderer,
    PaintContext,
    Rect,
    tc_ui_document_create,
    tc_ui_document_destroy,
    build_python_showcase,
)


def _example_seconds() -> float:
    try:
        return max(float(os.environ.get("TERMIN_GUI_NATIVE_EXAMPLE_SECONDS", "0")), 0.0)
    except ValueError:
        return 0.0


def _font_path() -> Path | None:
    configured = os.environ.get("TERMIN_UI_FONT")
    candidates = [Path(configured)] if configured else []
    candidates.append(
        Path.cwd()
        / "termin-thirdparty"
        / "recastnavigation"
        / "RecastDemo"
        / "Bin"
        / "DroidSans.ttf"
    )
    return next((path for path in candidates if path.is_file()), None)


def main() -> int:
    if not tgfx.configure_default_shader_runtime("termin-gui-native-python-showcase"):
        return 77

    document = None
    try:
        runtime = WindowedGraphicsSession.create_native()
        window = runtime.create_window("termin-gui-native Python showcase", 800, 600)
        graphics = tgfx.Tgfx2Context.from_runtime(runtime.graphics)
        context = graphics.context
        document = tc_ui_document_create()
        build_python_showcase(document)
        draw_list = DrawList()
        paint_context = PaintContext(draw_list)
        renderer = DrawListRenderer()
        font = _font_path()
        if font is None or not renderer.set_default_font_path(str(font), 15):
            raise RuntimeError("native UI showcase font was not found")
        renderer.bind_text_measurer(document)

        color_target = None
        target_size = (0, 0)
        max_seconds = _example_seconds()
        start = time.monotonic()
        while not window.should_close():
            window.poll_events()
            width, height = window.framebuffer_size()
            if width <= 0 or height <= 0:
                continue
            if color_target is None or target_size != (width, height):
                if color_target is not None:
                    context.destroy_texture(color_target)
                color_target = context.create_color_attachment(width, height)
                target_size = (width, height)

            draw_list.clear()
            document.layout_roots(Rect(0.0, 0.0, float(width), float(height)))
            document.paint_roots(paint_context)
            context.begin_frame()
            context.begin_pass(
                color_target,
                clear_color_enabled=True,
                r=0.03,
                g=0.035,
                b=0.045,
                a=1.0,
            )
            renderer.render(context, draw_list, width, height)
            context.end_pass()
            context.end_frame()
            window.present(color_target)

            if max_seconds > 0.0 and time.monotonic() - start >= max_seconds:
                break

        renderer.release_gpu()
        if color_target is not None:
            context.destroy_texture(color_target)
        window.close()
        runtime.close()
        quit_sdl()
        return 0
    except Exception as exc:
        print(f"termin-gui-native Python showcase failed: {exc}", file=sys.stderr)
        message = str(exc)
        if (
            "No available video device" in message
            or "Vulkan support is either not configured in SDL" in message
        ):
            return 77
        return 1
    finally:
        if document is not None:
            tc_ui_document_destroy(document)


if __name__ == "__main__":
    raise SystemExit(main())
