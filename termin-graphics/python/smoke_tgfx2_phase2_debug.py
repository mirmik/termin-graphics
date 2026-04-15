"""Phase 2 diagnostic — isolate where Text3D rendering breaks.

Three test cases in one script, rendered in stacked NDC regions:

  TOP THIRD:    Single quad with loc 1 reinterpreted as UV. Fragment
                shader shows UV as color (red = u, green = v). No MVP,
                no sampling. Proves the vertex layout and
                draw_immediate_triangles work with a text-style shader.

  MIDDLE THIRD: Same quad, but fragment shader samples the font atlas
                at the quad's UV range [0..0.25, 0..0.25] (a slice of
                the top-left of the atlas showing preloaded glyphs).
                Proves bind_sampled_texture + u_font_atlas work.

  BOTTOM THIRD: Text3DRenderer with a SIMPLE identity MVP and unit
                cam_right=(1,0,0), cam_up=(0,1,0). Draws "Test" at
                origin. If this fails where the top two work, the
                issue is in MVP transpose or cam_right/up derivation.

Run:
  python termin-graphics/python/smoke_tgfx2_phase2_debug.py

Expected:
  top:    a UV gradient quad (red→green)
  middle: a slice of the font atlas showing ASCII glyph shapes
  bottom: "Test" drawn in white

Whichever tier is missing pinpoints the bug.
"""

import ctypes
import sys

import numpy as np
import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend, TcShader
from tgfx.font import get_default_font
from tgfx.text3d import Text3DRenderer
from tgfx._tgfx_native import (
    Tgfx2Context,
    tc_shader_ensure_tgfx2,
    wrap_fbo_color_as_tgfx2,
)


# Shader 1: gradient quad, no MVP, no sampling.
# loc 1 is reinterpreted as (offset_x, offset_y, u, v). We ignore
# offset and just show the UV as color.
_GRADIENT_VS = """#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_offset_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 1.0);
    v_uv = a_offset_uv.zw;
}
"""

_GRADIENT_FS = """#version 330 core
in vec2 v_uv;
out vec4 frag;
void main() {
    frag = vec4(v_uv.x, v_uv.y, 0.0, 1.0);
}
"""


# Shader 2: sample atlas at quad's UV.
_ATLAS_VS = _GRADIENT_VS

_ATLAS_FS = """#version 330 core
uniform sampler2D u_font_atlas;
in vec2 v_uv;
out vec4 frag;
void main() {
    float a = texture(u_font_atlas, v_uv).r;
    // No discard — always visible so we can see where the quad is.
    frag = vec4(a, a, a, 1.0);
}
"""


# Shader 3: IGNORES all uniforms. Adds offset_xy directly to world
# position and uses identity MVP. If tier 3 draws magenta "Test"
# letter shapes, the vertex data and draw_immediate_triangles are
# fine — the bug is in set_uniform_mat4 / set_uniform_vec3 delivery.
# If tier 3 STILL draws nothing, the vertex data itself is wrong.
_TIER3_VS = """#version 330 core
layout(location=0) in vec3 a_world_pos;
layout(location=1) in vec4 a_offset_uv;

out vec2 v_uv;

void main() {
    // Ignore u_mvp / u_cam_right / u_cam_up entirely. Apply offset
    // directly in NDC. a_world_pos.xy is the text anchor in NDC.
    vec2 ndc = a_world_pos.xy + a_offset_uv.xy;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_offset_uv.zw;
}
"""

_TIER3_FS = """#version 330 core
in vec2 v_uv;
out vec4 frag;
void main() {
    frag = vec4(1.0, 0.0, 1.0, 1.0);
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


def make_quad_verts(y_min: float, y_max: float):
    """Two triangles covering x=[-0.5,0.5], y=[y_min,y_max].

    Vertex format (7 floats): [x, y, z, offset_x, offset_y, u, v].
    offset is unused by the debug shaders; u,v cover [0..1] per quad.
    """
    return np.array(
        [
            # tri 1
            -0.5, y_min, 0.0,  0.0, 0.0,  0.0, 1.0,
             0.5, y_min, 0.0,  0.0, 0.0,  1.0, 1.0,
            -0.5, y_max, 0.0,  0.0, 0.0,  0.0, 0.0,
            # tri 2
             0.5, y_min, 0.0,  0.0, 0.0,  1.0, 1.0,
             0.5, y_max, 0.0,  0.0, 0.0,  1.0, 0.0,
            -0.5, y_max, 0.0,  0.0, 0.0,  0.0, 0.0,
        ],
        dtype=np.float32,
    )


class TrivialCamera:
    """Identity MVP with a simple right/up basis."""

    def mvp(self, aspect):  # noqa: ARG002
        return np.eye(4, dtype=np.float32)

    def view_matrix(self):
        # Row 0 = cam_right = +X, row 1 = cam_up = +Y
        return np.eye(4, dtype=np.float32)


def main() -> int:
    window, gl_ctx = create_window(
        "tgfx2 Phase 2 debug — UV / atlas / text3d", 900, 700,
    )

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    holder = Tgfx2Context()
    ctx = holder.context

    font = get_default_font(size=32)
    if font is None:
        print("ERROR: no system font")
        return 1

    # Compile diagnostic shaders via the legacy TcShader bridge.
    grad_sh = TcShader.from_sources(_GRADIENT_VS, _GRADIENT_FS, "", "dbg_grad")
    grad_sh.ensure_ready()
    grad_pair = tc_shader_ensure_tgfx2(ctx, grad_sh)

    atlas_sh = TcShader.from_sources(_ATLAS_VS, _ATLAS_FS, "", "dbg_atlas")
    atlas_sh.ensure_ready()
    atlas_pair = tc_shader_ensure_tgfx2(ctx, atlas_sh)

    tier3_sh = TcShader.from_sources(_TIER3_VS, _TIER3_FS, "", "dbg_tier3")
    tier3_sh.ensure_ready()
    tier3_pair = tc_shader_ensure_tgfx2(ctx, tier3_sh)

    text3d = Text3DRenderer(font=font)
    text_camera = TrivialCamera()

    # Quads for the three tiers. NDC y is [-1..1]; split into three
    # stripes, leaving small gutters.
    top_quad    = make_quad_verts( 0.35,  0.85)  # [-0.5..0.5] x [0.35..0.85]
    middle_quad = make_quad_verts(-0.25,  0.25)
    # The text3d tier has its own geometry from text3d.draw.

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
            r=0.10, g=0.10, b=0.12, a=1.0,
        )
        ctx.set_viewport(0, 0, dw, dh)
        ctx.set_depth_test(False)
        ctx.set_blend(False)

        # --- TIER 1: gradient quad (no MVP, no sampling) ---
        ctx.bind_shader(grad_pair.vs, grad_pair.fs)
        ctx.draw_immediate_triangles(top_quad, 6)

        # --- TIER 2: atlas sample quad ---
        atlas_handle = font.ensure_texture_tgfx2(holder)
        ctx.bind_shader(atlas_pair.vs, atlas_pair.fs)
        ctx.set_uniform_int("u_font_atlas", 0)
        ctx.bind_sampled_texture(0, atlas_handle)
        ctx.draw_immediate_triangles(middle_quad, 6)

        # --- TIER 3: identical quad shape as tier 1/2, tier3 shader ---
        # Use the exact same make_quad_verts helper that produced the
        # visible tier 1/2 quads, but bind the tier3 shader (magenta,
        # ignores uniforms). If this does NOT show magenta in the
        # bottom stripe, the bug is NOT in vertex data — it is in
        # state leak from tier 2 (which bound a sampled texture +
        # atlas shader) persisting into tier 3.
        ctx.set_blend(False)
        ctx.bind_shader(tier3_pair.vs, tier3_pair.fs)
        bottom_quad = make_quad_verts(-0.85, -0.35)
        ctx.draw_immediate_triangles(bottom_quad, 6)

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

    print(f"ran {frame} frames")
    del ctx
    del holder
    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
