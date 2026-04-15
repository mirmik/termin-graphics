"""Phase 0 smoke test for tgfx2 Python bindings.

Tests the production rendering pathway used by present.py and that the
tcgui UIRenderer migration will use:

  tgfx1 offscreen FBO  ──wrap──►  tgfx2 TextureHandle
         ▲                               │
         │                               ▼
   tgfx1 blit_framebuffer           ctx2.begin_pass(offscreen_tex)
         │                               │
         ▼                               ▼
   SDL window (default FB)          draw_immediate_triangles

tgfx2 does not render to the default framebuffer directly — the
architectural pattern is "tgfx2 owns offscreen textures, tgfx1 owns the
window target". present.py demonstrates this in production.

Stages:
  A. Construct Tgfx2Context
  B. Compile shader via TcShader + tc_shader_ensure_tgfx2
  C. Each frame: offscreen tgfx1 FBO → wrap as tgfx2 tex → begin_pass
     with clear → draw_immediate_triangles → end_pass → end_frame →
     tgfx1 blit offscreen to window → SwapWindow

Expected result: dark red window with a green triangle in the middle.
ESC or window close to exit.

Usage:
  python termin-graphics/python/smoke_tgfx2_phase0.py
"""

import ctypes
import sys

import numpy as np
import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend, TcShader
from tgfx._tgfx_native import (
    Tgfx2Context,
    Tgfx2TextureHandle,
    tc_shader_ensure_tgfx2,
    wrap_fbo_color_as_tgfx2,
)


# Trivial pos+color shader. Matches the fixed vertex layout of
# RenderContext2::draw_immediate_triangles (loc 0 = vec3, loc 1 = vec4).
_VS = """#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_col;
out vec4 v_col;
void main() {
    gl_Position = vec4(a_pos, 1.0);
    v_col = a_col;
}
"""

_FS = """#version 330 core
in vec4 v_col;
out vec4 frag;
void main() {
    frag = v_col;
}
"""


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
    if not window:
        raise RuntimeError(f"SDL_CreateWindow failed: {sdl2.SDL_GetError()}")
    gl_ctx = video.SDL_GL_CreateContext(window)
    if not gl_ctx:
        raise RuntimeError(
            f"SDL_GL_CreateContext failed: {sdl2.SDL_GetError()}"
        )
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    video.SDL_GL_SetSwapInterval(1)
    return window, gl_ctx


def build_triangle_verts():
    # Single triangle in NDC, 7 floats per vertex: [x,y,z, r,g,b,a].
    return np.array(
        [
            0.0,  0.6, 0.0,  0.2, 0.9, 0.2, 1.0,
           -0.6, -0.6, 0.0,  0.2, 0.9, 0.2, 1.0,
            0.6, -0.6, 0.0,  0.2, 0.9, 0.2, 1.0,
        ],
        dtype=np.float32,
    )


def main():
    window, gl_ctx = create_window("tgfx2 Phase 0 smoke", 800, 600)

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    # --- Stage A — construct the tgfx2 context holder -------------------
    print("[A] constructing Tgfx2Context...")
    holder = Tgfx2Context()
    ctx = holder.context
    print(f"[A] ctx = {ctx}")

    # --- Stage B — prepare a shader for immediate draw ------------------
    print("[B] compiling shader via tc_shader_ensure_tgfx2...")
    tri_shader = TcShader.from_sources(_VS, _FS, "", "phase0_tri")
    tri_shader.ensure_ready()
    pair = tc_shader_ensure_tgfx2(ctx, tri_shader)
    print(f"[B] shader handles: vs.id={pair.vs.id}, fs.id={pair.fs.id}")
    if pair.vs.id == 0 or pair.fs.id == 0:
        print("[B] ERROR: tc_shader_ensure_tgfx2 returned null handles")
        return 1

    tri_verts = build_triangle_verts()

    # --- Stage C — offscreen FBO for production render path ------------
    # tgfx2 does not render to the default framebuffer directly. The
    # production pattern (see present.py) is: tgfx1 owns an offscreen
    # FBO, tgfx2 renders into it via wrap_fbo_color_as_tgfx2, tgfx1
    # blits the result into the window.
    #
    # The window target is wrapped as an external FramebufferHandle
    # around GL FBO 0 — that's the bindable form tgfx1 blit_framebuffer
    # expects for the "dst=window" side.
    offscreen_fbo = None
    window_fbo = None
    offscreen_size = (0, 0)

    def ensure_offscreen(w: int, h: int):
        nonlocal offscreen_fbo, window_fbo, offscreen_size
        if offscreen_size == (w, h) and offscreen_fbo is not None:
            return
        print(f"[C] (re)creating offscreen FBO {w}x{h}")
        offscreen_fbo = graphics.create_framebuffer(w, h, 1, "")
        window_fbo = graphics.create_external_framebuffer(0, w, h)
        offscreen_size = (w, h)

    # --- Frame loop -----------------------------------------------------
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

        ctx.begin_frame()

        # Wrap the tgfx1 offscreen FBO's color attachment as a tgfx2
        # TextureHandle. Non-owning — released at end_frame.
        offscreen_tex2 = wrap_fbo_color_as_tgfx2(ctx, offscreen_fbo)
        if not offscreen_tex2:
            print("[C] ERROR: wrap_fbo_color_as_tgfx2 returned null")
            ctx.end_frame()
            break

        ctx.begin_pass(
            color=offscreen_tex2,
            clear_color_enabled=True,
            r=0.30, g=0.08, b=0.08, a=1.0,
        )
        ctx.set_viewport(0, 0, dw, dh)
        ctx.set_depth_test(False)
        ctx.set_blend(False)

        ctx.bind_shader(pair.vs, pair.fs)
        ctx.draw_immediate_triangles(tri_verts, 3)

        ctx.end_pass()
        ctx.end_frame()

        # Blit offscreen to the window via tgfx1. The window side is a
        # FramebufferHandle wrapping GL FBO 0.
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
        if frame == 1:
            print(f"[loop] first frame submitted at {dw}x{dh}")

    print(f"[loop] ran {frame} frames")
    print("[cleanup] dropping holder + destroying GL context")
    del ctx
    del holder

    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
