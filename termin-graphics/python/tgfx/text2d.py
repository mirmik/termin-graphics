"""Screen-space 2D text rendering — tgfx2.

Unlike ``Text3DRenderer`` (billboarded, world-space), this renderer
works in pixel coordinates with an orthographic projection. Used by
``UIRenderer`` for widget text (labels, buttons, tick labels, etc.).

Coordinate convention:
    x, y are in viewport pixels.
    y grows downward (standard UI). Top-left of the viewport is (0, 0).

Usage:
    t2d = Text2DRenderer(font=font)
    t2d.begin(holder, viewport_w, viewport_h)
    t2d.draw("hello", x=20, y=40, color=(1, 1, 1, 1), size=14)
    t2d.draw("world", x=200, y=40, anchor="center")
    t2d.end()

The ortho matrix flips Y (negative y-scale). That reflection inverts
triangle orientation — so vertices are emitted CCW in visual (y+down)
pixel space, which becomes CCW in NDC y+up and survives the default
``CullMode::Back``.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

from tgfx import TcShader
from tgfx._tgfx_native import tc_shader_ensure_tgfx2

if TYPE_CHECKING:
    from tgfx._tgfx_native import Tgfx2Context, Tgfx2ShaderHandle
    from tgfx.font import FontTextureAtlas


_TEXT2D_VERT = """#version 330 core
layout(location=0) in vec3 a_pos;    // (x_pixel, y_pixel, 0)
layout(location=1) in vec4 a_uv_pad; // (u, v, _, _)

uniform mat4 u_projection;

out vec2 v_uv;

void main() {
    gl_Position = u_projection * vec4(a_pos.xy, 0.0, 1.0);
    v_uv = a_uv_pad.xy;
}
"""

_TEXT2D_FRAG = """#version 330 core
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


def _build_ortho_pixel_to_ndc(w: float, h: float) -> np.ndarray:
    """Ortho projection: pixel coords (y+ down) → NDC (y+ up).

    (0, 0) pixel → (-1, +1) NDC (top-left)
    (w, h) pixel → (+1, -1) NDC (bottom-right)
    """
    if w <= 0 or h <= 0:
        return np.eye(4, dtype=np.float32)
    m = np.array(
        [
            [2.0 / w,  0.0,     0.0, -1.0],
            [0.0,    -2.0 / h,  0.0,  1.0],
            [0.0,     0.0,     -1.0,  0.0],
            [0.0,     0.0,      0.0,  1.0],
        ],
        dtype=np.float32,
    )
    return m


class Text2DRenderer:
    """Renders UI text labels through tgfx2 in screen-pixel space."""

    def __init__(self, font: "FontTextureAtlas | None" = None):
        self._font = font

        self._tc_shader: TcShader | None = None
        self._vs: "Tgfx2ShaderHandle | None" = None
        self._fs: "Tgfx2ShaderHandle | None" = None
        self._compiled_for_holder: "Tgfx2Context | None" = None

        self._holder: "Tgfx2Context | None" = None
        self._ctx = None
        self._proj_flat: list[float] | None = None

    # ------------------------------------------------------------------
    # Lazy resources
    # ------------------------------------------------------------------

    def _ensure_shader(self, holder: "Tgfx2Context") -> None:
        if self._tc_shader is None:
            self._tc_shader = TcShader.from_sources(
                _TEXT2D_VERT, _TEXT2D_FRAG, "", "tgfx_text2d"
            )
        if self._compiled_for_holder is not holder or self._vs is None:
            pair = tc_shader_ensure_tgfx2(holder.context, self._tc_shader)
            if not pair.vs or not pair.fs:
                raise RuntimeError(
                    "Text2DRenderer: tc_shader_ensure_tgfx2 returned null handles"
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
        viewport_w: int,
        viewport_h: int,
        font: "FontTextureAtlas | None" = None,
    ) -> None:
        if font is not None:
            self._font = font
        if self._font is None:
            raise RuntimeError("Text2DRenderer.begin: no font atlas provided")

        self._ensure_shader(holder)

        self._holder = holder
        self._ctx = holder.context

        # Cache the projection matrix so draw() can rebind it every
        # call without re-computing. draw() must rebind shader +
        # atlas + projection on every call because callers are
        # allowed to interleave Text2D draws with other draws that
        # change the bound shader (e.g. UIRenderer.draw_rect /
        # draw_image between two draw_text calls).
        self._proj_flat = _build_ortho_pixel_to_ndc(
            float(viewport_w), float(viewport_h),
        ).flatten().tolist()

    def measure(self, text: str, size: float = 14.0) -> tuple[float, float]:
        """Measure pixel (width, height) of ``text`` rendered at ``size``.

        Height is ``size`` — a conservative upper bound (ascent+descent).
        Width is the sum of rasterised glyph widths scaled to ``size``.
        """
        if not text or self._font is None:
            return (0.0, 0.0)
        scale = size / self._font.size
        total_w = 0.0
        for ch in text:
            g = self._font.glyphs.get(ch)
            if g is not None:
                total_w += g["size"][0] * scale
        return (total_w, float(size))

    def draw(
        self,
        text: str,
        x: float,
        y: float,
        *,
        color=(1.0, 1.0, 1.0, 1.0),
        size: float = 14.0,
        anchor: str = "left",
    ) -> None:
        """Draw ``text`` anchored at pixel (x, y).

        ``anchor``:
          - ``"left"`` — (x, y) is the top-left of the text box.
          - ``"center"`` — (x, y) is the center of the text box.
          - ``"right"`` — (x, y) is the top-right of the text box.
        """
        if not text or self._font is None or self._ctx is None:
            return

        # Make sure the atlas has every char and re-uploads on new glyphs.
        self._font.ensure_glyphs(text, tgfx2_ctx=self._holder)

        scale = size / self._font.size
        total_w, _ = self.measure(text, size)

        if anchor == "center":
            start_x = x - total_w / 2
            start_y = y - size / 2
        elif anchor == "right":
            start_x = x - total_w
            start_y = y
        else:
            start_x = x
            start_y = y

        # Rebind shader + projection + atlas on every draw — a caller
        # (e.g. UIRenderer) may have bound a different shader between
        # our own begin() and this draw. Cheap in practice: one
        # bind_shader + three uniform sets + one texture bind.
        ctx = self._ctx
        ctx.bind_shader(self._vs, self._fs)
        ctx.set_uniform_mat4("u_projection", self._proj_flat, True)
        ctx.set_uniform_int("u_font_atlas", 0)
        atlas_handle = self._font.ensure_texture_tgfx2(self._holder)
        ctx.bind_sampled_texture(0, atlas_handle)
        ctx.set_uniform_vec4(
            "u_color",
            float(color[0]), float(color[1]),
            float(color[2]), float(color[3]),
        )

        verts: list[float] = []
        cursor_x = start_x
        for ch in text:
            g = self._font.glyphs.get(ch)
            if g is None:
                continue
            gw, gh = g["size"]
            u0, v0, u1, v1 = g["uv"]

            char_w = gw * scale
            char_h = gh * scale

            px0 = cursor_x
            px1 = cursor_x + char_w
            py0 = start_y              # top edge (smaller y in y+down)
            py1 = start_y + char_h     # bottom edge (larger y)

            # 6 vertices (2 triangles). CCW in pixel y+down visual →
            # after ortho y-flip → CCW in NDC y+up → front-facing.
            # Triangle 1: TL, BL, BR
            # Triangle 2: TL, BR, TR
            verts.extend([
                px0, py0, 0.0, u0, v0, 0.0, 0.0,  # TL
                px0, py1, 0.0, u0, v1, 0.0, 0.0,  # BL
                px1, py1, 0.0, u1, v1, 0.0, 0.0,  # BR
                px0, py0, 0.0, u0, v0, 0.0, 0.0,  # TL
                px1, py1, 0.0, u1, v1, 0.0, 0.0,  # BR
                px1, py0, 0.0, u1, v0, 0.0, 0.0,  # TR
            ])

            cursor_x += char_w

        if not verts:
            return

        verts_np = np.asarray(verts, dtype=np.float32)
        vertex_count = len(verts) // 7
        self._ctx.draw_immediate_triangles(verts_np, vertex_count)

    def end(self) -> None:
        self._holder = None
        self._ctx = None
