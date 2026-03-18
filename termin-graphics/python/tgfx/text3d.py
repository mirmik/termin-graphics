"""3D billboard text rendering.

Font atlas is injected — no dependency on tcgui.
Designed to live in tgfx once font atlas moves there too.
"""

from __future__ import annotations

import numpy as np

from tgfx import OpenGLGraphicsBackend, TcShader

# Billboard text shader:
# - Vertex positions computed as: world_pos + right*offset.x + up*offset.y
# - right/up vectors come from inverse view matrix (camera-facing)
# - Font atlas sampled in fragment shader (R channel → alpha)

# Billboard text shader.
# Uses same VAO layout as draw_immediate: location 0 = vec3, location 1 = vec4.
# We pack: location 0 = world_pos(3), location 1 = (offset_x, offset_y, u, v).
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
    """Renders billboard text labels in 3D space.

    Usage:
        t3d = Text3DRenderer()
        t3d.begin(camera, aspect)
        t3d.draw("hello", position=[1,2,3], color=(1,1,1,1), size=0.1)
        t3d.draw("world", position=[4,5,6])
        t3d.end()
    """

    def __init__(self, font=None):
        """Create renderer. Pass font atlas or it will be resolved on first use."""
        self._shader: TcShader | None = None
        self._font = font
        self._graphics = None

    def _ensure_init(self):
        if self._shader is None:
            self._shader = TcShader.from_sources(_TEXT_VERT, _TEXT_FRAG, "", "tgfx_text3d")
            self._shader.ensure_ready()
        if self._graphics is None:
            self._graphics = OpenGLGraphicsBackend.get_instance()

    def begin(self, camera, aspect: float, font=None, mvp_override=None):
        """Setup for a batch of text draws. Call once per frame.

        Args:
            camera: object with mvp(aspect) and view_matrix() methods
            aspect: viewport width / height
            font: FontTextureAtlas (optional, overrides constructor font)
            mvp_override: if given, use this MVP instead of camera.mvp()
                (for z_scale etc. — billboard vectors still come from camera)
        """
        self._ensure_init()
        if font is not None:
            self._font = font

        self._mvp = mvp_override if mvp_override is not None else camera.mvp(aspect)

        # Extract camera right and up vectors from view matrix
        view = camera.view_matrix()
        # View matrix rows 0,1 = right, up in world space (transposed)
        self._cam_right = np.array([view[0, 0], view[0, 1], view[0, 2]], dtype=np.float32)
        self._cam_up = np.array([view[1, 0], view[1, 1], view[1, 2]], dtype=np.float32)

        self._shader.use()
        self._shader.set_uniform_mat4("u_mvp", self._mvp.astype(np.float32), True)
        self._shader.set_uniform_vec3("u_cam_right", *self._cam_right.tolist())
        self._shader.set_uniform_vec3("u_cam_up", *self._cam_up.tolist())
        self._shader.set_uniform_int("u_font_atlas", 0)

        # Bind font atlas texture
        self._font.ensure_glyphs("0123456789.-+eE ", self._graphics)
        tex = self._font.ensure_texture(self._graphics)
        tex.bind(0)

    def draw(self, text: str, position, *,
             color=(1.0, 1.0, 1.0, 1.0),
             size: float = 0.05,
             anchor: str = "center"):
        """Draw billboard text at a 3D world position.

        Args:
            text: string to render
            position: (x, y, z) in world coordinates
            color: RGBA tuple
            size: height of text in world units
            anchor: "left", "center", or "right"
        """
        if not text or self._font is None:
            return

        self._font.ensure_glyphs(text, self._graphics)
        pos = np.asarray(position, dtype=np.float32)

        scale = size / self._font.size
        ascent = self._font.ascent * scale

        # Measure total width
        total_w = 0.0
        for ch in text:
            if ch in self._font.glyphs:
                gw, _ = self._font.glyphs[ch]["size"]
                total_w += gw * scale

        # Anchor offset
        if anchor == "center":
            start_x = -total_w / 2
        elif anchor == "right":
            start_x = -total_w
        else:
            start_x = 0.0

        self._shader.set_uniform_vec4("u_color", *[float(c) for c in color])

        # Build all glyphs into one vertex array
        verts = []
        cursor_x = start_x
        px, py, pz = float(pos[0]), float(pos[1]), float(pos[2])

        for ch in text:
            if ch not in self._font.glyphs:
                continue

            glyph = self._font.glyphs[ch]
            gw, gh = glyph["size"]
            u0, v0, u1, v1 = glyph["uv"]

            char_w = gw * scale
            char_h = gh * scale

            left = cursor_x
            right = cursor_x + char_w
            top = ascent
            bottom = ascent - char_h

            # 6 vertices (2 triangles), each 7 floats: [wx,wy,wz, off_x,off_y, u,v]
            verts.extend([
                px, py, pz, left, top, u0, v0,
                px, py, pz, right, top, u1, v0,
                px, py, pz, left, bottom, u0, v1,
                px, py, pz, right, top, u1, v0,
                px, py, pz, right, bottom, u1, v1,
                px, py, pz, left, bottom, u0, v1,
            ])

            cursor_x += char_w

        if verts:
            self._graphics.draw_immediate_triangles(np.array(verts, dtype=np.float32))

    def end(self):
        """Finish text batch."""
        pass
