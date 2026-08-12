from __future__ import annotations

import logging
import time

import tgfx
from termin.gui_native import Size, tc_ui_document_create, tc_ui_document_destroy

from .sections import sdk_font_path, section_registry


_LOG = logging.getLogger("graphics_showcase.windowed")


class _WindowedApplication:
    def __init__(self, document, request_repaint, graphics=None) -> None:
        self.document = document
        self.request_repaint = request_repaint
        self.graphics = graphics


_SECTION_TITLES = {
    "native_ui": "Native UI",
    "graphics_lines": "Graphics Lines",
    "tcplot_sine": "Sine",
    "tcplot_scatter": "Scatter",
    "tcplot_multi": "Multi Plot",
    "tcplot_marker": "Marker",
    "tcplot_helix": "3D Helix",
    "tcplot_surface": "3D Surface",
    "visual_scene_gallery": "Visual Scene",
    "visual_scene_nodegraph": "Nodegraph",
    "visual_scene3d_widget": "SceneView3D",
    "plot_nodegraph_composition": "Composition",
}


def _append(document, parent, child) -> None:
    if not parent.widget.append_child(child.widget):
        raise RuntimeError("failed to append showcase overview widget")


def _build_overview(document):
    overview = document.create_vstack("graphics-showcase-overview")
    overview.widget.preferred_size = Size(1160.0, 700.0)
    title = document.create_label("Termin graphics profile showcase")
    subtitle = document.create_label(
        "One installed-SDK surface: native UI, retained plots, visual scene and nodegraph composition."
    )
    hint = document.create_label(
        "Choose a tab above. Every feature page is also rendered independently by the headless acceptance gate."
    )
    _append(document, overview, title)
    _append(document, overview, subtitle)
    _append(document, overview, hint)
    for section in section_registry():
        label = document.create_label(
            f"{_SECTION_TITLES[section.name]} — {section.description}"
        )
        _append(document, overview, label)
    return overview.widget


def _build_tabbed_showcase(application):
    document = application.document
    tabs = document.create_tab_view("graphics-showcase-tabs")
    tabs.widget.preferred_size = Size(1280.0, 820.0)
    if not document.add_root(tabs.handle):
        raise RuntimeError("failed to add graphics showcase tab root")
    tabs.add_page("Overview", _build_overview(document))

    contents = []
    for section in section_registry():
        content = section.build(application)
        if content.root is None:
            raise RuntimeError(f"showcase section '{section.name}' has no windowed root")
        if not document.remove_root(content.root.handle):
            content.cleanup()
            raise RuntimeError(
                f"failed to transfer showcase section '{section.name}' into its tab"
            )
        tabs.add_page(_SECTION_TITLES[section.name], content.root)
        contents.append(content)
    tabs.selected_index = 0
    return tabs, contents


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
    contents = []
    selection_connection = None
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
        application = _WindowedApplication(
            document,
            adapter.request_repaint,
            graphics_session.graphics,
        )
        tabs, contents = _build_tabbed_showcase(application)
        selection_connection = tabs.connect_selection_changed(
            lambda _index: adapter.request_repaint()
        )

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
        if selection_connection is not None:
            tabs.disconnect_selection_changed(selection_connection)
        for content in reversed(contents):
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
