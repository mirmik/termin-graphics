"""Phase 3 visual smoke test — Text2DRenderer on tgfx2.

Draws UI-style text labels at various pixel positions, anchors and
sizes. No 3D, no billboarding — pure screen-space with orthographic
projection and Y-flip.

Expected result:
  - Top-left label "Top-left (20, 20)"
  - Centered label in the middle of the window
  - Right-aligned label on the right edge, size 24
  - Cyrillic label "Кириллица" below centered
  - Large label "HELLO" with alpha 0.5 in the bottom half
  - Font resizes correctly between sizes

ESC to exit.

Usage:
  python termin-graphics/python/smoke_tgfx2_phase3.py
"""

import ctypes
import sys

import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend
from tgfx.font import get_default_font
from tgfx.text2d import Text2DRenderer
from tgfx._tgfx_native import Tgfx2Context, wrap_fbo_color_as_tgfx2


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
    window, gl_ctx = create_window("tgfx2 Phase 3 — Text2D smoke", 900, 600)

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    print("[1] constructing Tgfx2Context...")
    holder = Tgfx2Context()
    ctx = holder.context

    print("[2] loading default font (size=32 for atlas quality)...")
    font = get_default_font(size=32)
    if font is None:
        print("    ERROR: no system font")
        return 1

    print("[3] creating Text2DRenderer...")
    text2d = Text2DRenderer(font=font)

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

    event = sdl2.SDL_Event()
    running = True
    frame = 0
    while running:
        while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
            if event.type == sdl2.SDL_QUIT:
                running = False
            elif (
                event.type == sdl2.SDL_KEYDOWN
                and event.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE
            ):
                running = False

        w, h = ctypes.c_int(), ctypes.c_int()
        video.SDL_GL_GetDrawableSize(window, ctypes.byref(w), ctypes.byref(h))
        dw, dh = w.value, h.value
        ensure_offscreen(dw, dh)

        ctx.begin_frame()
        offscreen_tex2 = wrap_fbo_color_as_tgfx2(ctx, offscreen_fbo)
        if not offscreen_tex2:
            print("ERROR: wrap failed")
            ctx.end_frame()
            break

        ctx.begin_pass(
            color=offscreen_tex2,
            clear_color_enabled=True,
            r=0.08, g=0.10, b=0.14, a=1.0,
        )
        ctx.set_viewport(0, 0, dw, dh)
        ctx.set_depth_test(False)
        ctx.set_blend(True)

        text2d.begin(holder, dw, dh)

        # Top-left anchor (default).
        text2d.draw(
            "Top-left (20, 20)",
            x=20, y=20,
            color=(1.0, 1.0, 1.0, 1.0),
            size=18,
        )

        # Centered in the middle of the window.
        text2d.draw(
            "Centered",
            x=dw / 2, y=dh / 2,
            color=(0.4, 1.0, 0.4, 1.0),
            size=28,
            anchor="center",
        )

        # Right-aligned on the right edge, larger size.
        text2d.draw(
            "Right edge",
            x=dw - 20, y=20,
            color=(1.0, 0.7, 0.3, 1.0),
            size=24,
            anchor="right",
        )

        # Cyrillic label below the center.
        text2d.draw(
            "Кириллица",
            x=dw / 2, y=dh / 2 + 50,
            color=(0.7, 0.9, 1.0, 1.0),
            size=22,
            anchor="center",
        )

        # Large semi-transparent marquee in the lower half.
        text2d.draw(
            "HELLO",
            x=dw / 2, y=dh - 80,
            color=(1.0, 1.0, 1.0, 0.5),
            size=60,
            anchor="center",
        )

        # Row of small labels demonstrating measure() + manual layout.
        y_row = 80
        gap = 8
        cursor = 20
        for word, col in [
            ("tgfx2",  (1.0, 1.0, 1.0, 1.0)),
            (" + ",    (0.6, 0.6, 0.6, 1.0)),
            ("Text2D", (0.4, 0.9, 1.0, 1.0)),
            (" = ",    (0.6, 0.6, 0.6, 1.0)),
            ("ok",     (0.4, 1.0, 0.4, 1.0)),
        ]:
            text2d.draw(word, x=cursor, y=y_row, color=col, size=20)
            cursor += text2d.measure(word, 20)[0] + gap

        text2d.end()

        ctx.end_pass()
        ctx.end_frame()

        graphics.blit_framebuffer(
            offscreen_fbo,
            window_fbo,
            (0, 0, dw, dh),
            (0, 0, dw, dh),
            True, False,
        )
        video.SDL_GL_SwapWindow(window)
        frame += 1

    print(f"[loop] ran {frame} frames")
    del ctx
    del holder
    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
