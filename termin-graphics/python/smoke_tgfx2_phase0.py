"""Phase 0 smoke test for tgfx2 Python bindings.

Opens an SDL window with an OpenGL context, creates a Tgfx2Context over
it, and exercises the new bindings:

Stage A — frame lifecycle + clear:
  Tgfx2Context() → begin_frame → begin_pass(default FB, clear red)
                 → end_pass → end_frame → SwapWindow

If the window is solid red, the factory works, begin_pass on the default
framebuffer works, end_frame submits the command list correctly.

Stage B — immediate draw:
  Compile a tiny pos+color shader via TcShader → tc_shader_ensure_tgfx2.
  bind_shader + draw_immediate_triangles (1 triangle in NDC).

If a green triangle appears over the red background, immediate draw +
shader bridge + end_frame ordering all work.

Stage C — texture create + sampled bind:
  create_texture_rgba8 with a 2×2 checkerboard. Would need an image
  shader to display. SKIPPED in this smoke (no image shader compiled
  yet); left as TODO for Phase 1 once Text2D/Text3D are on tgfx2.

Exit: ESC or window close.

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
    PIXEL_RGBA8,
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
    # Green, pointing up, centered.
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

    # tgfx1 side: initialise the GL context. We still need this because
    # TcShader (the legacy shader struct used as the input to
    # tc_shader_ensure_tgfx2) lives in the tgfx1 resource system.
    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    # --- Stage A — construct the tgfx2 context holder -------------------
    print("[A] constructing Tgfx2Context...")
    holder = Tgfx2Context()
    ctx = holder.context
    print(f"[A] ctx = {ctx}")

    # --- Stage B — prepare a shader for immediate draw ------------------
    # Legacy TcShader still owns the GLSL source + tc_gpu_slot. The
    # bridge compiles into tgfx2 shader handles and caches them on the
    # slot so repeated calls are cheap.
    print("[B] compiling shader via tc_shader_ensure_tgfx2...")
    tri_shader = TcShader.from_sources(_VS, _FS, "", "phase0_tri")
    tri_shader.ensure_ready()
    pair = tc_shader_ensure_tgfx2(ctx, tri_shader)
    print(f"[B] shader handles: vs.id={pair.vs.id}, fs.id={pair.fs.id}")
    if pair.vs.id == 0 or pair.fs.id == 0:
        print("[B] ERROR: tc_shader_ensure_tgfx2 returned null handles")
        return 1

    tri_verts = build_triangle_verts()

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

        ctx.begin_frame()
        # Default framebuffer (empty TextureHandle) + clear to red.
        ctx.begin_pass(
            color=Tgfx2TextureHandle(),
            clear_color_enabled=True,
            r=0.30, g=0.08, b=0.08, a=1.0,
        )
        ctx.set_viewport(0, 0, w.value, h.value)

        # Bind the triangle shader and issue an immediate draw.
        ctx.bind_shader(pair.vs, pair.fs)
        ctx.draw_immediate_triangles(tri_verts, 3)

        ctx.end_pass()
        ctx.end_frame()

        video.SDL_GL_SwapWindow(window)

        frame += 1
        if frame == 1:
            print(f"[loop] first frame submitted at {w.value}x{h.value}")

    print(f"[loop] ran {frame} frames")
    print("[cleanup] dropping holder + destroying GL context")
    # Explicitly drop the holder BEFORE tearing down the GL context,
    # otherwise ~OpenGLRenderDevice runs against a dead context.
    del ctx
    del holder

    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
