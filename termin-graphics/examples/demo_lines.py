"""3D thick line mesh demo for tgfx2.

Run:
    TERMIN_BACKEND=opengl sdk/bin/termin_python termin-graphics/examples/demo_lines.py
    TERMIN_BACKEND=vulkan sdk/bin/termin_python termin-graphics/examples/demo_lines.py
"""

from __future__ import annotations

import math
import os
import time

import numpy as np

from tcbase._geom_native import LinearColor
from termin.window import WindowedGraphicsSession, quit_sdl
from termin.geombase import OrbitCamera
from tgfx import (
    CULL_NONE,
    LineCapStyle,
    LineJoinStyle,
    LinePoint3,
    LineStyle,
    Tgfx2Context,
    Tgfx2PixelFormat,
    Tgfx2ShaderStage,
    build_line_mesh,
    configure_default_shader_runtime,
)


_VERT_SRC = """#version 450 core
#ifdef VULKAN
layout(push_constant) uniform PCBlock {
    mat4 u_mvp;
    vec4 u_color;
} pc;
#define U_MVP pc.u_mvp
#else
uniform mat4 u_mvp;
#define U_MVP u_mvp
#endif
layout(location=0) in vec3 a_position;
void main() {
    gl_Position = U_MVP * vec4(a_position, 1.0);
}
"""

_FRAG_SRC = """#version 450 core
#ifdef VULKAN
layout(push_constant) uniform PCBlock {
    mat4 u_mvp;
    vec4 u_color;
} pc;
#define U_COLOR pc.u_color
#else
uniform vec4 u_color;
#define U_COLOR u_color
#endif
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = U_COLOR;
}
"""


def _example_seconds() -> float:
    try:
        return max(float(os.environ.get("TERMIN_GRAPHICS_EXAMPLE_SECONDS", "0")), 0.0)
    except ValueError:
        return 0.0


class RenderTarget:
    def __init__(self, ctx: Tgfx2Context, width: int, height: int) -> None:
        self.ctx = ctx
        self.width = width
        self.height = height
        self.color = ctx.create_color_attachment(
            width, height, Tgfx2PixelFormat.RGBA8_UNorm)
        self.depth = ctx.create_depth_attachment(
            width, height, Tgfx2PixelFormat.D32F)

    def ensure_size(self, width: int, height: int) -> None:
        if width == self.width and height == self.height:
            return
        self.destroy()
        self.width = width
        self.height = height
        self.color = self.ctx.create_color_attachment(
            width, height, Tgfx2PixelFormat.RGBA8_UNorm)
        self.depth = self.ctx.create_depth_attachment(
            width, height, Tgfx2PixelFormat.D32F)

    def destroy(self) -> None:
        if self.color is not None:
            self.ctx.destroy_texture(self.color)
            self.color = None
        if self.depth is not None:
            self.ctx.destroy_texture(self.depth)
            self.depth = None


class LineDraw:
    def __init__(self, points: list[tuple[float, float, float]],
                 width: float,
                 color: tuple[float, float, float, float],
                 *,
                 closed: bool = False) -> None:
        style = LineStyle()
        style.width = width
        style.up_hint = LinePoint3(0.0, 0.0, 1.0)
        style.cap = LineCapStyle.Round
        style.join = LineJoinStyle.Round
        style.round_segments = 12
        style.closed = closed

        mesh = build_line_mesh([LinePoint3(x, y, z) for x, y, z in points], style)
        verts = np.asarray(mesh.triangle_vertices, dtype=np.float32)
        if verts.size == 0:
            self.vertices = np.zeros((0, 7), dtype=np.float32)
        else:
            self.vertices = np.zeros((verts.shape[0], 7), dtype=np.float32)
            self.vertices[:, 0:3] = verts
        self.color = np.asarray(color, dtype=np.float32)


def _make_scene() -> list[LineDraw]:
    helix = []
    for i in range(80):
        t = i / 79.0 * math.tau * 2.5
        helix.append((math.cos(t), math.sin(t), -1.2 + i / 79.0 * 2.4))

    zigzag = [
        (-1.8, -1.0, -0.4),
        (-1.2, -0.2, 0.55),
        (-0.45, -1.0, -0.1),
        (0.25, -0.15, 0.65),
        (0.95, -0.95, 0.1),
        (1.75, -0.1, 0.85),
    ]

    ring = []
    for i in range(36):
        t = i / 36.0 * math.tau
        ring.append((math.cos(t) * 1.45, math.sin(t) * 1.45, -1.35))

    return [
        LineDraw([(-2.2, 0.0, 0.0), (2.2, 0.0, 0.0)], 0.035, (0.95, 0.25, 0.25, 1.0)),
        LineDraw([(0.0, -2.2, 0.0), (0.0, 2.2, 0.0)], 0.035, (0.2, 0.85, 0.3, 1.0)),
        LineDraw([(0.0, 0.0, -1.8), (0.0, 0.0, 1.8)], 0.035, (0.25, 0.55, 1.0, 1.0)),
        LineDraw(helix, 0.08, (0.55, 0.72, 1.0, 1.0)),
        LineDraw(zigzag, 0.13, (1.0, 0.72, 0.2, 1.0)),
        LineDraw(ring, 0.055, (0.65, 0.95, 0.75, 1.0), closed=True),
    ]


def _push_state(ctx, camera: OrbitCamera, aspect: float,
                color: np.ndarray) -> None:
    mvp = np.asarray(camera.mvp(aspect), dtype=np.float32)
    pc = np.concatenate((mvp, color)).view(np.uint8)
    ctx.set_push_constants(np.ascontiguousarray(pc, dtype=np.uint8))


def main() -> None:
    configure_default_shader_runtime("examples")
    runtime = WindowedGraphicsSession.create_native()
    window = runtime.create_window("tgfx2 3D line mesh demo", 1100, 760)
    ctx = Tgfx2Context.from_runtime(runtime.graphics)
    target = RenderTarget(ctx, 1100, 760)
    vs = ctx.device.create_shader(Tgfx2ShaderStage.Vertex, _VERT_SRC)
    fs = ctx.device.create_shader(Tgfx2ShaderStage.Fragment, _FRAG_SRC)

    camera = OrbitCamera()
    camera.distance = 5.2
    camera.fitted_radius = 2.2
    scene = _make_scene()
    max_seconds = _example_seconds()
    started = time.monotonic()
    try:
        while not window.should_close():
            window.poll_events()

            width, height = window.framebuffer_size()
            if width <= 0 or height <= 0:
                time.sleep(0.016)
                continue

            target.ensure_size(width, height)
            aspect = max(float(width) / max(float(height), 1.0), 0.001)

            ctx.context.begin_frame()
            ctx.context.begin_pass(
                target.color,
                target.depth,
                clear_linear_color=LinearColor(0.035, 0.04, 0.05, 1.0),
                clear_depth_enabled=True,
                clear_depth=1.0,
            )
            ctx.context.set_viewport(0, 0, width, height)
            ctx.context.set_depth_test(True)
            ctx.context.set_depth_write(True)
            ctx.context.set_cull(CULL_NONE)
            ctx.context.bind_shader(vs, fs)

            for item in scene:
                if item.vertices.shape[0] == 0:
                    continue
                _push_state(ctx.context, camera, aspect, item.color)
                ctx.context.draw_immediate_triangles(
                    np.ascontiguousarray(item.vertices, dtype=np.float32),
                    int(item.vertices.shape[0]),
                )

            ctx.context.end_pass()
            ctx.context.end_frame()
            window.present(target.color)
            if max_seconds > 0.0 and time.monotonic() - started >= max_seconds:
                break
            time.sleep(0.016)
    finally:
        target.destroy()
        window.close()
        runtime.close()
        quit_sdl()


if __name__ == "__main__":
    main()
