"""Shared native window host for the restored tcplot examples."""

from __future__ import annotations

import os
from pathlib import Path
import sys
import time
from typing import Callable

import tgfx
from termin.gui_native import tc_ui_document_create, tc_ui_document_destroy
from termin.gui_native.window import GuiWindowAdapter
from termin.window import WindowManager, WindowedGraphicsSession, quit_sdl


def _font_path() -> Path:
    font = (
        Path(sys.executable).resolve().parent.parent
        / "share"
        / "termin"
        / "fonts"
        / "DroidSans.ttf"
    )
    if not font.is_file():
        raise FileNotFoundError(f"tcplot example requires the SDK font: {font}")
    return font


def _second_limit() -> float:
    try:
        return max(float(os.environ.get("TCPLOT_EXAMPLE_SECONDS", "0")), 0.0)
    except ValueError:
        return 0.0


def run_demo(
    title: str,
    make_widget: Callable,
    size: tuple[int, int] = (900, 700),
) -> None:
    """Run one example through termin.window and GuiWindowAdapter."""

    if not tgfx.configure_default_shader_runtime("tcplot-examples"):
        raise RuntimeError("failed to configure tcplot example shader runtime")
    graphics_session = None
    window_manager = None
    document = None
    adapter = None
    try:
        graphics_session = WindowedGraphicsSession.create_native()
        window_manager = WindowManager(graphics_session)
        handle = window_manager.create_window(title, size[0], size[1])
        document = tc_ui_document_create()
        adapter = GuiWindowAdapter(
            window_manager,
            handle,
            document,
            font_path=str(_font_path()),
            font_size=15,
            enable_text_input=True,
        )
        root = make_widget(document)
        if not document.add_root(root.handle):
            raise RuntimeError("failed to attach tcplot example root")

        adapter.request_repaint()
        limit = _second_limit()
        started = time.monotonic()
        while not adapter.should_close:
            window_manager.pump_events()
            adapter.consume_pending_events(window_manager, handle, None)
            if adapter.should_close:
                break
            if adapter.repaint_requested:
                if not adapter.render_frame():
                    raise RuntimeError("tcplot example failed to render")
            else:
                time.sleep(0.01)
            if limit and time.monotonic() - started >= limit:
                break
    finally:
        if adapter is not None:
            adapter.close()
        if document is not None:
            tc_ui_document_destroy(document)
        if window_manager is not None:
            window_manager.close()
        if graphics_session is not None:
            graphics_session.close()
        quit_sdl()
