"""Shared host loop for tcplot examples.

Spins up a WindowedGraphicsSession window + tcgui UI and runs a wait-for-event
main loop until the user closes the window. Native Termin events keep the
examples independent from the third-party PySDL2 package.
"""

from __future__ import annotations

from typing import Callable

from tcbase import MouseButton
from tcgui.widgets.ui import UI
from termin.display.window import (
    WindowedGraphicsSession,
    quit_sdl,
    wait_sdl_events_timeout,
)
from tgfx import Tgfx2Context, configure_default_shader_runtime


_KEY_ESCAPE = 256


def run_demo(title: str, make_widget: Callable[[], object],
             size: tuple[int, int] = (900, 600),
             bg: tuple[float, float, float, float] = (0.10, 0.10, 0.12, 1.0)
             ) -> None:
    """Host a tcplot widget inside an WindowedGraphicsSession window until it closes."""
    configure_default_shader_runtime("examples")
    runtime = WindowedGraphicsSession.create_native()
    window = runtime.create_window(title, size[0], size[1])
    ctx = Tgfx2Context.from_runtime(runtime.graphics)

    ui = UI(graphics=ctx)
    ui.root = make_widget()

    def dispatch(ev):
        event_type = ev["type"]
        if event_type == "quit" or (
            event_type == "key_down" and ev["key"] == _KEY_ESCAPE
        ):
            window.set_should_close(True)
        elif event_type == "window_close":
            window.set_should_close(True)
        elif event_type == "mouse_move":
            ui.mouse_move(ev["x"], ev["y"])
        elif event_type == "mouse_down":
            ui.mouse_down(ev["x"], ev["y"], MouseButton(ev["button"]))
        elif event_type == "mouse_up":
            ui.mouse_up(ev["x"], ev["y"], MouseButton(ev["button"]))
        elif event_type == "mouse_wheel":
            ui.mouse_wheel(ev["dx"], ev["dy"], ev["x"], ev["y"])

    while not window.should_close():
        for event in wait_sdl_events_timeout(500):
            dispatch(event)

        w, h = window.framebuffer_size()
        if w <= 0 or h <= 0:
            continue
        tex = ui.render_compose(w, h, background_color=bg)
        if tex is not None:
            window.present(tex)

    window.close()
    runtime.close()
    quit_sdl()
