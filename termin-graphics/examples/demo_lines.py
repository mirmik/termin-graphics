"""3D thick line mesh demo for tgfx2.

Run:
    TERMIN_BACKEND=opengl python3 examples/demo_lines.py
    TERMIN_BACKEND=vulkan python3 examples/demo_lines.py
"""

from __future__ import annotations

import ctypes
import math

import numpy as np
import sdl2

from termin.display import SDLBackendWindow
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


def _dispatch(event: sdl2.SDL_Event, window: SDLBackendWindow,
              camera: OrbitCamera, drag: dict[str, float | str]) -> None:
    etype = event.type
    if etype == sdl2.SDL_QUIT:
        window.set_should_close(True)
    elif etype == sdl2.SDL_KEYDOWN:
        if event.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE:
            window.set_should_close(True)
    elif etype == sdl2.SDL_WINDOWEVENT:
        if event.window.event == sdl2.SDL_WINDOWEVENT_CLOSE:
            window.set_should_close(True)
    elif etype == sdl2.SDL_MOUSEBUTTONDOWN:
        button = int(event.button.button)
        if button == 2:
            drag["mode"] = "orbit"
        elif button == 3:
            drag["mode"] = "pan"
        drag["x"] = float(event.button.x)
        drag["y"] = float(event.button.y)
    elif etype == sdl2.SDL_MOUSEBUTTONUP:
        drag["mode"] = ""
    elif etype == sdl2.SDL_MOUSEMOTION:
        mode = str(drag["mode"])
        if mode:
            x = float(event.motion.x)
            y = float(event.motion.y)
            dx = x - float(drag["x"])
            dy = y - float(drag["y"])
            if mode == "orbit":
                camera.orbit(-dx * 0.006, dy * 0.006)
            elif mode == "pan":
                camera.pan(-dx, dy)
            drag["x"] = x
            drag["y"] = y
    elif etype == sdl2.SDL_MOUSEWHEEL:
        factor = max(0.15, 1.0 - float(event.wheel.y) * 0.10)
        camera.zoom(factor)


def main() -> None:
    window = SDLBackendWindow("tgfx2 3D line mesh demo", 1100, 760)
    ctx = Tgfx2Context.from_window(window.device_ptr(), window.context_ptr())
    target = RenderTarget(ctx, 1100, 760)
    vs = ctx.device.create_shader(Tgfx2ShaderStage.Vertex, _VERT_SRC)
    fs = ctx.device.create_shader(Tgfx2ShaderStage.Fragment, _FRAG_SRC)

    camera = OrbitCamera()
    camera.distance = 5.2
    camera.fitted_radius = 2.2
    scene = _make_scene()
    event = sdl2.SDL_Event()
    drag: dict[str, float | str] = {"mode": "", "x": 0.0, "y": 0.0}

    try:
        while not window.should_close():
            while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
                _dispatch(event, window, camera, drag)

            width, height = window.framebuffer_size()
            if width <= 0 or height <= 0:
                sdl2.SDL_Delay(16)
                continue

            target.ensure_size(width, height)
            aspect = max(float(width) / max(float(height), 1.0), 0.001)

            ctx.context.begin_frame()
            ctx.context.begin_pass(
                target.color,
                target.depth,
                clear_color_enabled=True,
                r=0.035,
                g=0.04,
                b=0.05,
                a=1.0,
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
            sdl2.SDL_Delay(16)
    finally:
        target.destroy()
        window.close()
        sdl2.SDL_Quit()


if __name__ == "__main__":
    main()
