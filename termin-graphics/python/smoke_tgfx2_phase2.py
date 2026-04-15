"""Phase 2 visual smoke test — Text3DRenderer on tgfx2.

Opens an SDL window, creates a Tgfx2Context and an OrbitCamera, renders
into an offscreen tgfx1 FBO via tgfx2, and draws a few billboard text
labels. Camera slowly auto-rotates so billboarding is visible.

Expected: dark background, several labels floating in 3D ("0.0 X",
"1.0 Y", "Hello", "Кириллица") staying camera-facing as the view
orbits. ESC to exit.

Usage:
  python termin-graphics/python/smoke_tgfx2_phase2.py
"""

import ctypes
import sys
import time

import numpy as np
import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend
from tgfx.font import get_default_font
from tgfx.text3d import Text3DRenderer
from tgfx._tgfx_native import Tgfx2Context, wrap_fbo_color_as_tgfx2

# Reuse tcplot's OrbitCamera — it already has mvp() + view_matrix().
sys.path.insert(
    0,
    __import__("os").path.join(
        __import__("os").path.dirname(__file__),
        "..", "..", "tcplot",
    ),
)
from tcplot.camera3d import OrbitCamera  # noqa: E402


def create_window(title, width, height):
    if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
        raise RuntimeError(f"SDL_Init failed: {sdl2.SDL_GetError()}")
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MAJOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MINOR_VERSION, 3)
    video.SDL_GL_SetAttribute(
        video.SDL_GL_CONTEXT_PROFILE_MASK,
        video.SDL_GL_CONTEXT_PROFILE_CORE,
    )
    video.SDL_GL_SetAttribute(video.SDL_GL_DOUBLEBUFFER, 1)
    video.SDL_GL_SetAttribute(video.SDL_GL_DEPTH_SIZE, 24)
    flags = (
        video.SDL_WINDOW_OPENGL
        | video.SDL_WINDOW_RESIZABLE
        | video.SDL_WINDOW_SHOWN
    )
    window = video.SDL_CreateWindow(
        title.encode(),
        video.SDL_WINDOWPOS_CENTERED,
        video.SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags,
    )
    gl_ctx = video.SDL_GL_CreateContext(window)
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    video.SDL_GL_SetSwapInterval(1)
    return window, gl_ctx


def main() -> int:
    window, gl_ctx = create_window("tgfx2 Phase 2 — Text3D smoke", 900, 700)

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    print("[1] constructing Tgfx2Context...")
    holder = Tgfx2Context()
    ctx = holder.context

    print("[2] loading default font...")
    font = get_default_font(size=32)
    if font is None:
        print("    ERROR: no system font available")
        return 1

    print("[3] creating Text3DRenderer...")
    text3d = Text3DRenderer(font=font)

    print("[4] setting up OrbitCamera...")
    camera = OrbitCamera()
    # Fit a roughly unit-cube scene so billboard text at size 0.15
    # is readable.
    camera.fit_bounds(
        np.array([-1.0, -1.0, -1.0], dtype=np.float32),
        np.array([ 1.0,  1.0,  1.0], dtype=np.float32),
    )

    offscreen_fbo = None
    window_fbo = None
    offscreen_size = (0, 0)

    def ensure_offscreen(w: int, h: int):
        nonlocal offscreen_fbo, window_fbo, offscreen_size
        if offscreen_size == (w, h) and offscreen_fbo is not None:
            return
        offscreen_fbo = graphics.create_framebuffer(w, h, 1, "")
        window_fbo = graphics.create_external_framebuffer(0, w, h)
        offscreen_size = (w, h)

    labels = [
        ((0.0, 0.0, 0.0),     (1.0, 1.0, 1.0, 1.0), "origin"),
        ((1.0, 0.0, 0.0),     (1.0, 0.3, 0.3, 1.0), "X"),
        ((0.0, 1.0, 0.0),     (0.3, 1.0, 0.3, 1.0), "Y"),
        ((0.0, 0.0, 1.0),     (0.3, 0.4, 1.0, 1.0), "Z"),
        ((0.7, 0.7, 0.5),     (1.0, 1.0, 0.3, 1.0), "Hello"),
        ((-0.6, -0.6, 0.3),   (1.0, 1.0, 0.3, 1.0), "Кириллица"),
    ]

    t0 = time.time()
    event = sdl2.SDL_Event()
    running = True
    frame = 0
    while running:
        while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
            t = event.type
            if t == sdl2.SDL_QUIT:
                running = False
            elif (
                t == sdl2.SDL_KEYDOWN
                and event.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE
            ):
                running = False

        w, h = ctypes.c_int(), ctypes.c_int()
        video.SDL_GL_GetDrawableSize(window, ctypes.byref(w), ctypes.byref(h))
        dw, dh = w.value, h.value
        ensure_offscreen(dw, dh)

        # Slow auto-orbit so billboarding is visually obvious.
        elapsed = time.time() - t0
        camera.azimuth = 0.6 * elapsed
        camera.elevation = 0.3 + 0.15 * np.sin(elapsed * 0.4)

        aspect = dw / max(dh, 1)

        ctx.begin_frame()
        offscreen_tex2 = wrap_fbo_color_as_tgfx2(ctx, offscreen_fbo)
        if not offscreen_tex2:
            print("ERROR: wrap_fbo_color_as_tgfx2 returned null")
            ctx.end_frame()
            break

        ctx.begin_pass(
            color=offscreen_tex2,
            clear_color_enabled=True,
            r=0.10, g=0.10, b=0.12, a=1.0,
        )
        ctx.set_viewport(0, 0, dw, dh)
        ctx.set_depth_test(False)
        ctx.set_blend(True)

        text3d.begin(holder, camera, aspect)
        for pos, color, text in labels:
            text3d.draw(text, pos, color=color, size=0.15)
        text3d.end()

        ctx.end_pass()
        ctx.end_frame()

        graphics.blit_framebuffer(
            offscreen_fbo,
            window_fbo,
            (0, 0, dw, dh),
            (0, 0, dw, dh),
            True,
            False,
        )
        video.SDL_GL_SwapWindow(window)

        frame += 1

    print(f"[loop] ran {frame} frames")
    print("[cleanup] tearing down GL context")
    del ctx
    del holder
    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
