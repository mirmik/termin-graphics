"""3D billboard text rendering — tgfx2.

Usage:
    t3d = Text3DRenderer()
    t3d.begin(holder, camera, aspect)
    t3d.draw("hello", position=[1, 2, 3], color=(1, 1, 1, 1), size=0.1)
    t3d.draw("world", position=[4, 5, 6])
    t3d.end()

``holder`` is a ``Tgfx2Context`` — the same object the caller uses for
its own rendering. It gives Text3DRenderer access to both the device
(for atlas upload) and the render context (for draws).

Shader vertex layout matches ``RenderContext2::draw_immediate_triangles``:
  location 0 = vec3 world_pos
  location 1 = vec4 (offset_x, offset_y, u, v)

The shader reinterprets loc 1 as billboard offset + texture UV — the
byte layout (3 floats + 4 floats = 28 bytes stride) is the same as the
"pos + color" layout tgfx2 expects.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

from tgfx import TcShader
from tgfx._tgfx_native import tc_shader_ensure_tgfx2

if TYPE_CHECKING:
    from tgfx._tgfx_native import Tgfx2Context, Tgfx2ShaderHandle
    from tgfx.font import FontTextureAtlas


# Billboard text shader. Uses the fixed vertex layout of
# draw_immediate_triangles (loc 0 = vec3, loc 1 = vec4), with loc 1
# carrying (offset_x, offset_y, u, v).
_TEXT_VERT = """#version 330 core
layout(location=0) in vec3 a_world_pos;
layout(location=1) in vec4 a_offset_uv;  // (offset.x, offset.y, u, v)

uniform mat4 u_mvp;
uniform vec3 u_cam_right;
uniform vec3 u_cam_up;

out vec2 v_uv;

void main() {
    vec3 pos = a_world_pos
             + u_cam_right * a_offset_uv.x
             + u_cam_up * a_offset_uv.y;
    gl_Position = u_mvp * vec4(pos, 1.0);
    v_uv = a_offset_uv.zw;
}
"""

_TEXT_FRAG = """#version 330 core
uniform sampler2D u_font_atlas;
uniform vec4 u_color;

in vec2 v_uv;
out vec4 frag_color;

void main() {
    float a = texture(u_font_atlas, v_uv).r * u_color.a;
    if (a < 0.01) discard;
    frag_color = vec4(u_color.rgb, a);
}
"""


class Text3DRenderer:
    """Renders billboard text labels in 3D space via tgfx2."""

    def __init__(self, font: "FontTextureAtlas | None" = None):
        """Create renderer. Pass a font atlas or resolve one on first begin()."""
        self._font = font

        # Lazy tgfx2 resources — populated on first begin().
        self._tc_shader: TcShader | None = None
        self._vs: "Tgfx2ShaderHandle | None" = None
        self._fs: "Tgfx2ShaderHandle | None" = None
        self._compiled_for_holder: "Tgfx2Context | None" = None

        # Active frame state (valid between begin() and end()).
        self._holder: "Tgfx2Context | None" = None
        self._ctx = None  # Tgfx2RenderContext
        self._mvp_flat: list[float] | None = None
        self._cam_right: tuple[float, float, float] = (1.0, 0.0, 0.0)
        self._cam_up: tuple[float, float, float] = (0.0, 1.0, 0.0)

    # ------------------------------------------------------------------
    # Lazy resource setup
    # ------------------------------------------------------------------

    def _ensure_shader(self, holder: "Tgfx2Context") -> None:
        """Compile TcShader + bridge to tgfx2 handles. Cached per-holder."""
        if self._tc_shader is None:
            self._tc_shader = TcShader.from_sources(
                _TEXT_VERT, _TEXT_FRAG, "", "tgfx_text3d"
            )
            self._tc_shader.ensure_ready()

        # Re-compile if holder changed (e.g. GL context recreated).
        if self._compiled_for_holder is not holder or self._vs is None:
            pair = tc_shader_ensure_tgfx2(holder.context, self._tc_shader)
            if not pair.vs or not pair.fs:
                raise RuntimeError(
                    "Text3DRenderer: tc_shader_ensure_tgfx2 returned null handles"
                )
            self._vs = pair.vs
            self._fs = pair.fs
            self._compiled_for_holder = holder

    # ------------------------------------------------------------------
    # Frame API
    # ------------------------------------------------------------------

    def begin(
        self,
        holder: "Tgfx2Context",
        camera,
        aspect: float,
        font: "FontTextureAtlas | None" = None,
        mvp_override=None,
    ) -> None:
        """Set up for a batch of text draws.

        ``holder`` is the caller's Tgfx2Context — Text3DRenderer draws
        through its ``.context`` and uploads glyphs through the holder's
        device.

        ``camera`` must expose ``mvp(aspect)`` and ``view_matrix()``.

        ``mvp_override`` replaces the computed MVP (used when the caller
        wants to pre-multiply a z-scale, for example). Billboard right
        and up vectors still come from the camera view matrix.
        """
        if font is not None:
            self._font = font
        if self._font is None:
            raise RuntimeError("Text3DRenderer.begin: no font atlas provided")

        self._ensure_shader(holder)

        self._holder = holder
        self._ctx = holder.context

        # Cache per-frame values so draw() can rebind them every call.
        # draw() must rebind shader + atlas + uniforms every time
        # because callers may interleave Text3D draws with other draws
        # that change the currently bound shader.
        mvp = mvp_override if mvp_override is not None else camera.mvp(aspect)
        mvp_np = np.ascontiguousarray(mvp, dtype=np.float32)
        self._mvp_flat = mvp_np.flatten().tolist()

        view = camera.view_matrix()
        self._cam_right = (
            float(view[0, 0]), float(view[0, 1]), float(view[0, 2]),
        )
        self._cam_up = (
            float(view[1, 0]), float(view[1, 1]), float(view[1, 2]),
        )

    def draw(
        self,
        text: str,
        position,
        *,
        color=(1.0, 1.0, 1.0, 1.0),
        size: float = 0.05,
        anchor: str = "center",
    ) -> None:
        """Draw a billboard text string at a 3D world position."""
        if not text or self._font is None or self._ctx is None:
            return

        # Rasterise any new glyphs and push to the atlas if needed.
        self._font.ensure_glyphs(text, ctx=self._ctx)

        pos = np.asarray(position, dtype=np.float32)

        scale = size / self._font.size
        ascent = self._font.ascent * scale

        # Measure total text width for anchoring.
        total_w, _ = self._font.measure_text(text, size)

        if anchor == "center":
            start_x = -total_w / 2
        elif anchor == "right":
            start_x = -total_w
        else:
            start_x = 0.0

        # Rebind shader + atlas + per-frame uniforms on every draw so
        # we survive state changes made by interleaved callers.
        ctx = self._ctx
        ctx.bind_shader(self._vs, self._fs)
        ctx.set_uniform_mat4("u_mvp", self._mvp_flat, True)
        ctx.set_uniform_vec3("u_cam_right", *self._cam_right)
        ctx.set_uniform_vec3("u_cam_up", *self._cam_up)
        ctx.set_uniform_int("u_font_atlas", 0)
        atlas_handle = self._font.ensure_texture(self._ctx)
        ctx.bind_sampled_texture(0, atlas_handle)
        ctx.set_uniform_vec4(
            "u_color",
            float(color[0]), float(color[1]),
            float(color[2]), float(color[3]),
        )

        # Build a single flat vertex array for the whole string.
        verts: list[float] = []
        cursor_x = start_x
        px, py, pz = float(pos[0]), float(pos[1]), float(pos[2])

        for ch in text:
            glyph = self._font.get_glyph(ord(ch))
            if glyph is None:
                continue
            u0, v0, u1, v1, gw, gh = glyph

            char_w = gw * scale
            char_h = gh * scale

            left = cursor_x
            right = cursor_x + char_w
            top = ascent
            bottom = ascent - char_h

            # 6 vertices (2 triangles) per glyph. Per vertex:
            # [world_x, world_y, world_z, offset_x, offset_y, u, v]
            #
            # Wind order is CCW in NDC (y+ up) so we survive the
            # default CullMode::Back in tgfx2. tgfx1 had no culling
            # by default, which is why the pre-migration version got
            # away with CW here.
            verts.extend([
                # Triangle 1: BL, BR, TL
                px, py, pz, left,  bottom, u0, v1,
                px, py, pz, right, bottom, u1, v1,
                px, py, pz, left,  top,    u0, v0,
                # Triangle 2: BR, TR, TL
                px, py, pz, right, bottom, u1, v1,
                px, py, pz, right, top,    u1, v0,
                px, py, pz, left,  top,    u0, v0,
            ])

            cursor_x += char_w

        if not verts:
            return

        verts_np = np.asarray(verts, dtype=np.float32)
        vertex_count = len(verts) // 7
        self._ctx.draw_immediate_triangles(verts_np, vertex_count)

    def end(self) -> None:
        """Finish text batch. Currently a no-op — shader/state stays
        bound on ctx until the caller rebinds or ends its pass."""
        self._holder = None
        self._ctx = None
