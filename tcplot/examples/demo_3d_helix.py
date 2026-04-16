"""3D helix and scatter demo.

Run: python3 examples/demo_3d_helix.py
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

from tcplot import Plot3D


def create_window(title, width, height):
    if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
        raise RuntimeError(f"SDL_Init failed: {sdl2.SDL_GetError()}")
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MAJOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MINOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_PROFILE_MASK, video.SDL_GL_CONTEXT_PROFILE_CORE)
    video.SDL_GL_SetAttribute(video.SDL_GL_DOUBLEBUFFER, 1)
    video.SDL_GL_SetAttribute(video.SDL_GL_DEPTH_SIZE, 24)
    flags = video.SDL_WINDOW_OPENGL | video.SDL_WINDOW_RESIZABLE | video.SDL_WINDOW_SHOWN
    window = video.SDL_CreateWindow(title.encode(), video.SDL_WINDOWPOS_CENTERED,
                                     video.SDL_WINDOWPOS_CENTERED, width, height, flags)
    gl_ctx = video.SDL_GL_CreateContext(window)
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    video.SDL_GL_SetSwapInterval(1)
    return window, gl_ctx


_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def main():
    window, gl_ctx = create_window("tcplot — 3D Helix", 900, 700)

    plot = Plot3D()
    plot.data.title = "Double Helix"

    # Helix 1
    t = np.linspace(0, 6 * np.pi, 500)
    x1 = np.cos(t)
    y1 = np.sin(t)
    z1 = t / (2 * np.pi)
    plot.plot(x1, y1, z1, color=(0.12, 0.47, 0.71, 1.0), label="Helix 1")

    # Helix 2 (offset)
    x2 = np.cos(t + np.pi)
    y2 = np.sin(t + np.pi)
    plot.plot(x2, y2, z1, color=(1.0, 0.50, 0.05, 1.0), label="Helix 2")

    # Scatter: random points inside cylinder
    rng = np.random.default_rng(42)
    n = 200
    r = rng.uniform(0, 0.3, n)
    theta = rng.uniform(0, 2 * np.pi, n)
    sx = r * np.cos(theta)
    sy = r * np.sin(theta)
    sz = rng.uniform(0, 3, n)
    plot.scatter(sx, sy, sz, color=(0.17, 0.63, 0.17, 1.0), label="Points")

    ui = UI()
    ui.root = plot

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
        if sdl2.SDL_WaitEventTimeout(ctypes.byref(event), 16):
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
