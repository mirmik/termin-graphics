from __future__ import annotations

import logging
import time

import tgfx
from termin.gui_native import tc_ui_document_create, tc_ui_document_destroy

from .sections import sdk_font_path, section_registry


_LOG = logging.getLogger("graphics_showcase.windowed")


class _WindowedApplication:
    def __init__(self, document, request_repaint) -> None:
        self.document = document
        self.request_repaint = request_repaint


def _integration_section():
    matches = [section for section in section_registry() if section.artifact]
    if len(matches) != 1:
        raise RuntimeError(
            f"windowed showcase requires exactly one integration section, got {len(matches)}"
        )
    return matches[0]


def run_windowed_showcase(
    *,
    width: int,
    height: int,
    frame_limit: int = 0,
    second_limit: float = 0.0,
) -> int:
    """Present the integration section through the graphics-profile window host."""

    if width < 320 or height < 240:
        raise ValueError("showcase window must be at least 320x240")
    if frame_limit < 0:
        raise ValueError("frame_limit must be non-negative")
    if second_limit < 0.0:
        raise ValueError("second_limit must be non-negative")
    if not tgfx.configure_default_shader_runtime("graphics-profile-showcase-windowed"):
        raise RuntimeError("failed to configure the graphics SDK shader runtime")

    # termin-window is a conditional member of SDL-enabled graphics SDKs. Keep
    # these imports out of the mandatory no-SDL headless startup path.
    from termin.gui_native.window import GuiWindowAdapter
    from termin.window import WindowManager, WindowedGraphicsSession, quit_sdl

    graphics_session = None
    window_manager = None
    window_handle = None
    document = None
    adapter = None
    content = None
    try:
        graphics_session = WindowedGraphicsSession.create_native()
        window_manager = WindowManager(graphics_session)
        window_handle = window_manager.create_window(
            "Termin graphics profile showcase", width, height
        )
        document = tc_ui_document_create()
        adapter = GuiWindowAdapter(
            window_manager,
            window_handle,
            document,
            font_path=str(sdk_font_path()),
            font_size=15,
            enable_text_input=True,
        )
        application = _WindowedApplication(document, adapter.request_repaint)
        content = _integration_section().build(application)

        adapter.request_repaint()
        started = time.monotonic()
        frame_count = 0
        while not adapter.should_close:
            window_manager.pump_events()
            adapter.consume_pending_events(window_manager, window_handle, None)
            if adapter.should_close:
                break
            if adapter.repaint_requested:
                if not adapter.render_frame():
                    raise RuntimeError("windowed showcase failed to render a frame")
                frame_count += 1
                if frame_count < 3 or (frame_limit and frame_count < frame_limit):
                    adapter.request_repaint()
            else:
                time.sleep(0.01)
            if frame_limit and frame_count >= frame_limit:
                break
            if second_limit and time.monotonic() - started >= second_limit:
                break
        _LOG.info("windowed showcase rendered %d frame(s)", frame_count)
        return 0
    except Exception:
        _LOG.exception("windowed graphics showcase failed")
        raise
    finally:
        if content is not None:
            content.cleanup()
        if adapter is not None:
            adapter.close()
        if document is not None:
            tc_ui_document_destroy(document)
        if window_manager is not None:
            window_manager.close()
        if graphics_session is not None:
            graphics_session.close()
        quit_sdl()
