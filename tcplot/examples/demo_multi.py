"""Multiple plots side by side.

Run: python3 examples/demo_multi.py
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ctypes
import numpy as np
import sdl2
from sdl2 import video

from tcbase import MouseButton
from tcgui.widgets.ui import UI
from tcgui.widgets.containers import HStack

from tcplot import Plot2D


def create_window(title, width, height):
    if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
        raise RuntimeError(f"SDL_Init failed: {sdl2.SDL_GetError()}")
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MAJOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MINOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_PROFILE_MASK, video.SDL_GL_CONTEXT_PROFILE_CORE)
    video.SDL_GL_SetAttribute(video.SDL_GL_DOUBLEBUFFER, 1)
    flags = video.SDL_WINDOW_OPENGL | video.SDL_WINDOW_RESIZABLE | video.SDL_WINDOW_SHOWN
    window = video.SDL_CreateWindow(title.encode(), video.SDL_WINDOWPOS_CENTERED,
                                     video.SDL_WINDOWPOS_CENTERED, width, height, flags)
    gl_ctx = video.SDL_GL_CreateContext(window)
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    video.SDL_GL_SetSwapInterval(1)
    return window, gl_ctx


_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def main():
    window, gl_ctx = create_window("tcplot — Multi Plot", 1200, 500)

    row = HStack()
    row.spacing = 10

    # Plot 1: polynomials
    p1 = Plot2D()
    p1.stretch = True
    x = np.linspace(-2, 2, 200)
    p1.plot(x, x**2, label="x^2")
    p1.plot(x, x**3, label="x^3")
    p1.plot(x, x**4 - 2*x**2, label="x^4 - 2x^2")
    p1.data.title = "Polynomials"

    # Plot 2: damped oscillations
    p2 = Plot2D()
    p2.stretch = True
    t = np.linspace(0, 10, 500)
    for zeta in [0.1, 0.3, 0.7, 1.0]:
        y = np.exp(-zeta * t) * np.cos(t * np.sqrt(max(1 - zeta**2, 0)))
        p2.plot(t, y, label=f"zeta={zeta}")
    p2.data.title = "Damped Oscillations"
    p2.data.x_label = "t"

    row.add_child(p1)
    row.add_child(p2)

    ui = UI()
    ui.root = row

    event = sdl2.SDL_Event()
    running = True

    def dispatch(ev):
        nonlocal running
        t = ev.type
        if t == sdl2.SDL_QUIT or (t == sdl2.SDL_KEYDOWN and ev.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE):
            running = False
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
            ui.mouse_wheel(float(ev.wheel.x), float(ev.wheel.y), float(mx.value), float(my.value))

    while running:
        if sdl2.SDL_WaitEventTimeout(ctypes.byref(event), 500):
            dispatch(event)
            while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
                dispatch(event)
        if not running:
            break

        w, h = ctypes.c_int(), ctypes.c_int()
        video.SDL_GL_GetDrawableSize(window, ctypes.byref(w), ctypes.byref(h))
        ui.render(w.value, h.value, background_color=(0.10, 0.10, 0.12, 1.0))
        video.SDL_GL_SwapWindow(window)

    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()


if __name__ == "__main__":
    main()
