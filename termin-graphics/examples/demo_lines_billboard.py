"""GPU-expanded billboard 3D line demo for tgfx2."""

from __future__ import annotations

import ctypes
import math

import sdl2

from tcbase import MouseButton
from termin.display import WindowedGraphicsSession, quit_sdl
from termin.geombase import OrbitCamera
from tgfx import (
    CULL_NONE,
    LineCapStyle,
    LinePoint3,
    Tgfx2Context,
    Tgfx2PixelFormat,
    WorldSpaceLineParams,
    WorldSpaceLineRenderer,
    WorldSpaceLineStyle,
    configure_default_shader_runtime,
)

_SDL_BUTTON_MAP = {
    1: MouseButton.LEFT,
    2: MouseButton.MIDDLE,
    3: MouseButton.RIGHT,
}


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


class WorldLine:
    def __init__(self,
                 points: list[tuple[float, float, float]],
                 width: float,
                 color: tuple[float, float, float, float],
                 cap: LineCapStyle = LineCapStyle.Round) -> None:
        self.points = [LinePoint3(x, y, z) for x, y, z in points]
        self.style = WorldSpaceLineStyle()
        self.style.width = width
        self.style.color = color
        self.style.cap = cap
        self.style.round_segments = 18


def _make_scene() -> list[WorldLine]:
    lines: list[WorldLine] = []

    lines.append(WorldLine(
        [(-2.4, 0.0, 0.0), (2.4, 0.0, 0.0)],
        0.035,
        (0.95, 0.25, 0.25, 1.0),
        LineCapStyle.Square,
    ))
    lines.append(WorldLine(
        [(0.0, -2.4, 0.0), (0.0, 2.4, 0.0)],
        0.035,
        (0.25, 0.9, 0.3, 1.0),
        LineCapStyle.Square,
    ))
    lines.append(WorldLine(
        [(0.0, 0.0, -1.6), (0.0, 0.0, 1.8)],
        0.035,
        (0.25, 0.55, 1.0, 1.0),
        LineCapStyle.Square,
    ))

    for row, y in enumerate((-1.4, -0.35, 0.7)):
        z = -0.9 + row * 0.85
        color = (
            1.0 - row * 0.22,
            0.52 + row * 0.18,
            0.28 + row * 0.23,
            1.0,
        )
        lines.append(WorldLine(
            [(-1.9, y, z), (-0.55, y + 0.55, z), (0.7, y - 0.25, z),
             (1.85, y + 0.35, z)],
            0.13,
            color,
        ))

    helix = []
    for i in range(120):
        t = i / 119.0 * math.tau * 2.4
        helix.append((
            math.cos(t) * 0.85,
            math.sin(t) * 0.85,
            -1.15 + i / 119.0 * 2.3,
        ))
    lines.append(WorldLine(helix, 0.07, (0.55, 0.72, 1.0, 1.0)))

    ring = []
    for i in range(49):
        t = i / 48.0 * math.tau
        ring.append((math.cos(t) * 1.35, math.sin(t) * 1.35, -1.35))
    lines.append(WorldLine(ring, 0.055, (0.65, 0.95, 0.75, 1.0)))

    angles = (35.0, 70.0, 110.0, 150.0)
    colors = (
        (1.0, 0.3, 0.35, 1.0),
        (1.0, 0.78, 0.25, 1.0),
        (0.35, 0.8, 1.0, 1.0),
        (0.84, 0.55, 1.0, 1.0),
    )
    for i, angle_deg in enumerate(angles):
        angle = math.radians(angle_deg)
        x = -1.55 + i * 1.05
        y = 1.65
        z = 0.85
        p0 = (x - 0.45, y, z)
        p1 = (x, y, z)
        p2 = (x + math.cos(angle) * 0.55, y + math.sin(angle) * 0.55, z)
        lines.append(WorldLine([p0, p1, p2], 0.16, colors[i]))

    return lines


def _dispatch(event: sdl2.SDL_Event, window,
              camera: OrbitCamera, drag: dict[str, float | str]) -> None:
    etype = event.type
    if etype == sdl2.SDL_QUIT:
        window.set_should_close(True)
    elif etype == sdl2.SDL_WINDOWEVENT:
        if event.window.event == sdl2.SDL_WINDOWEVENT_CLOSE:
            window.set_should_close(True)
    elif etype == sdl2.SDL_MOUSEBUTTONDOWN:
        button = _SDL_BUTTON_MAP.get(int(event.button.button), MouseButton.OTHER)
        if button == MouseButton.MIDDLE:
            drag["mode"] = "orbit"
        elif button == MouseButton.RIGHT:
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
    configure_default_shader_runtime("examples")
    runtime = WindowedGraphicsSession.create_native()
    window = runtime.create_window(
        "tgfx2 GPU billboard world-width line demo", 1100, 760)
    ctx = Tgfx2Context.from_runtime(runtime.graphics)
    target = RenderTarget(ctx, 1100, 760)
    renderer = WorldSpaceLineRenderer()

    camera = OrbitCamera()
    camera.target = (0.0, 0.0, 0.15)
    camera.distance = 6.4
    camera.fitted_radius = 2.8
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

            params = WorldSpaceLineParams()
            params.view_projection = camera.mvp(aspect)
            params.camera_position = camera.eye

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

            for item in scene:
                renderer.draw_polyline(ctx.context, item.points, item.style, params)

            ctx.context.end_pass()
            ctx.context.end_frame()
            window.present(target.color)
            sdl2.SDL_Delay(16)
    finally:
        renderer.release(ctx.context)
        target.destroy()
        window.close()
        runtime.close()
        quit_sdl()


if __name__ == "__main__":
    main()
