"""Basic sine/cosine plot demo.

Run: python3 examples/demo_sin.py
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ctypes
import numpy as np
import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend
from tcbase import Key, MouseButton, Mods
from tcgui.widgets.ui import UI
from tcgui.widgets.containers import VStack, Panel
from tcgui.widgets.units import px, pct

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


_KEY_MAP = {
    sdl2.SDL_SCANCODE_BACKSPACE: Key.BACKSPACE,
    sdl2.SDL_SCANCODE_ESCAPE: Key.ESCAPE,
}

_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def main():
    window, gl_ctx = create_window("tcplot — Sine Demo", 900, 600)
    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    # Build UI
    root = Panel()
    root.preferred_width = pct(100)
    root.preferred_height = pct(100)
    root.background_color = (0.10, 0.10, 0.12, 1.0)
    root.padding = 10

    plot = Plot2D()
    plot.stretch = True

    # Add data
    x = np.linspace(0, 4 * np.pi, 500)
    plot.plot(x, np.sin(x), label="sin(x)")
    plot.plot(x, np.cos(x), label="cos(x)")
    plot.plot(x, np.sin(x) * np.exp(-x / 10), label="sin(x) * e^(-x/10)", thickness=2.0)
    plot.data.title = "Trigonometric Functions"
    plot.data.x_label = "x"
    plot.data.y_label = "y"

    root.add_child(plot)

    ui = UI(graphics)
    ui.root = root

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
            ui.mouse_up(float(ev.button.x), float(ev.button.y))
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
        graphics.bind_framebuffer(None)
        graphics.set_viewport(0, 0, w.value, h.value)
        graphics.clear_color_depth(0.10, 0.10, 0.12, 1.0)
        ui.render(w.value, h.value)
        video.SDL_GL_SwapWindow(window)

    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()


if __name__ == "__main__":
    main()
