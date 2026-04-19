"""Shared host loop for tcplot examples.

Spins up a BackendWindow + tcgui UI and runs a wait-for-event main
loop until the user closes the window. Picks OpenGL or Vulkan based
on the ``TERMIN_BACKEND`` env-var, same as every other BackendWindow
host in the project.
"""

from __future__ import annotations

import ctypes
from typing import Callable

import sdl2

from tcbase import MouseButton
from tcgui.widgets.ui import UI
from termin.display import BackendWindow
from tgfx._tgfx_native import Tgfx2Context


_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def run_demo(title: str, make_widget: Callable[[], object],
             size: tuple[int, int] = (900, 600),
             bg: tuple[float, float, float, float] = (0.10, 0.10, 0.12, 1.0)
             ) -> None:
    """Host a tcplot widget inside a BackendWindow until the user closes it."""
    window = BackendWindow(title, size[0], size[1])
    ctx = Tgfx2Context.from_window(window.device_ptr(), window.context_ptr())

    ui = UI(graphics=ctx)
    ui.root = make_widget()

    event = sdl2.SDL_Event()

    def dispatch(ev):
        t = ev.type
        if t == sdl2.SDL_QUIT or (
            t == sdl2.SDL_KEYDOWN
            and ev.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE
        ):
            window.set_should_close(True)
        elif t == sdl2.SDL_MOUSEMOTION:
            ui.mouse_move(float(ev.motion.x), float(ev.motion.y))
        elif t == sdl2.SDL_MOUSEBUTTONDOWN:
            ui.mouse_down(float(ev.button.x), float(ev.button.y),
                          _SDL_BUTTON_MAP.get(ev.button.button, MouseButton.LEFT))
        elif t == sdl2.SDL_MOUSEBUTTONUP:
            ui.mouse_up(float(ev.button.x), float(ev.button.y),
                        _SDL_BUTTON_MAP.get(ev.button.button, MouseButton.LEFT))
        elif t == sdl2.SDL_MOUSEWHEEL:
            mx, my = ctypes.c_int(), ctypes.c_int()
            sdl2.SDL_GetMouseState(ctypes.byref(mx), ctypes.byref(my))
            ui.mouse_wheel(float(ev.wheel.x), float(ev.wheel.y),
                           float(mx.value), float(my.value))

    while not window.should_close():
        if sdl2.SDL_WaitEventTimeout(ctypes.byref(event), 500):
            dispatch(event)
            while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
                dispatch(event)

        w, h = window.framebuffer_size()
        if w <= 0 or h <= 0:
            continue
        tex = ui.render_compose(w, h, background_color=bg)
        ui.process_deferred()
        if tex is not None:
            window.present(tex)
