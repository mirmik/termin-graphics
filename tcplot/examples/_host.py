"""Shared host loop for tcplot examples.

Spins up a WindowedGraphicsSession window + tcgui UI and runs a wait-for-event
main loop until the user closes the window. Native Termin events keep the
examples independent from the third-party PySDL2 package.
"""

from __future__ import annotations

from typing import Callable, Protocol

from tcbase import Key, MouseButton
from tcgui.widgets.ui import UI
from termin.geombase import SrgbColor
from termin.display.window import (
    WindowedGraphicsSession,
    quit_sdl,
    wait_sdl_events_timeout,
)
from tgfx import Tgfx2Context, configure_default_shader_runtime


class PlotWidget(Protocol):
    def release_gpu(self) -> None: ...


_DEFAULT_BACKGROUND = SrgbColor(0.10, 0.10, 0.12, 1.0)


def _event_button(value: int) -> MouseButton:
    try:
        return MouseButton(value)
    except ValueError:
        return MouseButton.OTHER


def run_demo(
    title: str,
    make_widget: Callable[[], PlotWidget],
    size: tuple[int, int] = (900, 600),
    bg: SrgbColor = _DEFAULT_BACKGROUND,
) -> None:
    """Host a tcplot widget inside a WindowedGraphicsSession window until it closes."""
    configure_default_shader_runtime("examples")
    runtime = WindowedGraphicsSession.create_native()
    window = None
    ctx = None
    ui = None
    root = None
    try:
        window = runtime.create_window(title, size[0], size[1])
        ctx = Tgfx2Context.from_runtime(runtime.graphics)

        ui = UI(graphics=ctx)
        root = make_widget()
        ui.root = root

        def dispatch(event: dict) -> None:
            event_type = event.get("type")
            if event_type in ("quit", "window_close"):
                window.set_should_close(True)
            elif event_type == "key_down":
                key = int(event.get("key", Key.UNKNOWN.value))
                if key == Key.ESCAPE.value:
                    window.set_should_close(True)
            elif event_type == "mouse_move":
                ui.mouse_move(
                    float(event.get("x", 0.0)),
                    float(event.get("y", 0.0)),
                    int(event.get("mods", 0)),
                )
            elif event_type in ("mouse_down", "mouse_up"):
                handler = ui.mouse_down if event_type == "mouse_down" else ui.mouse_up
                handler(
                    float(event.get("x", 0.0)),
                    float(event.get("y", 0.0)),
                    _event_button(int(event.get("button", MouseButton.OTHER.value))),
                    int(event.get("mods", 0)),
                )
            elif event_type == "mouse_wheel":
                ui.mouse_wheel(
                    float(event.get("dx", 0.0)),
                    float(event.get("dy", 0.0)),
                    float(event.get("x", 0.0)),
                    float(event.get("y", 0.0)),
                    int(event.get("mods", 0)),
                )

        while not window.should_close():
            for event in wait_sdl_events_timeout(500):
                dispatch(event)

            w, h = window.framebuffer_size()
            if w <= 0 or h <= 0:
                continue
            tex = ui.render_compose(w, h, background_color=bg)
            if tex is not None:
                window.present(tex)
    finally:
        if root is not None:
            root.release_gpu()
        if ui is not None:
            ui.root = None
            ui.close()
        if window is not None:
            window.close()
        root = None
        ui = None
        window = None
        ctx = None
        try:
            runtime.close()
        finally:
            quit_sdl()
