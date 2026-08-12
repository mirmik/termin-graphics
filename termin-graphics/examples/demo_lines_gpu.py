"""GPU-expanded screen-space 3D line demo for tgfx2."""

from __future__ import annotations

import math
import os
import time

from tcbase._geom_native import LinearColor
from termin.window import WindowedGraphicsSession, quit_sdl
from termin.geombase import OrbitCamera, Vec3
from tgfx import (
    CULL_NONE,
    LineCapStyle,
    LinePoint3,
    ScreenSpaceLineParams,
    ScreenSpaceLineRenderer,
    ScreenSpaceLineStyle,
    Tgfx2Context,
    Tgfx2PixelFormat,
    configure_default_shader_runtime,
)


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


class GpuLine:
    def __init__(self,
                 points: list[tuple[float, float, float]],
                 width_px: float,
                 color: tuple[float, float, float, float],
                 cap: LineCapStyle = LineCapStyle.Round) -> None:
        self.points = [LinePoint3(x, y, z) for x, y, z in points]
        self.style = ScreenSpaceLineStyle()
        self.style.width_px = width_px
        self.style.color = LinearColor(*color)
        self.style.cap = cap
        self.style.round_segments = 16


def _make_scene() -> list[GpuLine]:
    helix = []
    for i in range(96):
        t = i / 95.0 * math.tau * 2.75
        helix.append((math.cos(t), math.sin(t), -1.2 + i / 95.0 * 2.4))

    zigzag = [
        (-1.8, -1.0, -0.4),
        (-1.2, -0.2, 0.55),
        (-0.45, -1.0, -0.1),
        (0.25, -0.15, 0.65),
        (0.95, -0.95, 0.1),
        (1.75, -0.1, 0.85),
    ]

    ring = []
    for i in range(37):
        t = i / 36.0 * math.tau
        ring.append((math.cos(t) * 1.45, math.sin(t) * 1.45, -1.35))

    angle_lines = []
    angles_deg = (25.0, 45.0, 70.0, 100.0, 135.0, 165.0)
    colors = (
        (1.0, 0.35, 0.35, 1.0),
        (1.0, 0.58, 0.25, 1.0),
        (1.0, 0.84, 0.25, 1.0),
        (0.55, 0.9, 0.35, 1.0),
        (0.35, 0.75, 1.0, 1.0),
        (0.82, 0.55, 1.0, 1.0),
    )
    for i, angle_deg in enumerate(angles_deg):
        row = i // 3
        col = i % 3
        origin_x = -1.35 + col * 1.35
        origin_y = -0.85 + row * 1.05
        z = 1.15
        angle = math.radians(angle_deg)
        p0 = (origin_x - 0.55, origin_y, z)
        p1 = (origin_x, origin_y, z)
        p2 = (
            origin_x + math.cos(angle) * 0.55,
            origin_y + math.sin(angle) * 0.55,
            z,
        )
        angle_lines.append(
            GpuLine([p0, p1, p2], 18.0, colors[i], LineCapStyle.Round))

    center_marker = GpuLine(
        [(-0.65, 1.15, 1.35), (0.0, 1.75, 1.35), (0.65, 1.15, 1.35)],
        26.0,
        (1.0, 0.2, 0.95, 1.0),
        LineCapStyle.Round,
    )

    return [
        GpuLine([(-2.2, 0.0, 0.0), (2.2, 0.0, 0.0)], 4.0, (0.95, 0.25, 0.25, 1.0), LineCapStyle.Square),
        GpuLine([(0.0, -2.2, 0.0), (0.0, 2.2, 0.0)], 4.0, (0.2, 0.85, 0.3, 1.0), LineCapStyle.Square),
        GpuLine([(0.0, 0.0, -1.8), (0.0, 0.0, 1.8)], 4.0, (0.25, 0.55, 1.0, 1.0), LineCapStyle.Square),
        GpuLine(helix, 9.0, (0.55, 0.72, 1.0, 1.0)),
        GpuLine(zigzag, 14.0, (1.0, 0.72, 0.2, 1.0)),
        GpuLine(ring, 7.0, (0.65, 0.95, 0.75, 1.0)),
    ] + angle_lines + [center_marker]


def main() -> None:
    configure_default_shader_runtime("examples")
    runtime = WindowedGraphicsSession.create_native()
    window = runtime.create_window(
        "tgfx2 GPU screen-space line demo - angle gallery v2", 1100, 760)
    ctx = Tgfx2Context.from_runtime(runtime.graphics)
    target = RenderTarget(ctx, 1100, 760)
    renderer = ScreenSpaceLineRenderer()

    camera = OrbitCamera()
    camera.target = Vec3(0.0, 0.0, 0.35)
    camera.distance = 6.0
    camera.fitted_radius = 2.7
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

            params = ScreenSpaceLineParams()
            params.view_projection = camera.mvp(aspect)
            params.viewport_width = float(width)
            params.viewport_height = float(height)

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

            for item in scene:
                renderer.draw_polyline(ctx.context, item.points, item.style, params)

            ctx.context.end_pass()
            ctx.context.end_frame()
            window.present(target.color)
            if max_seconds > 0.0 and time.monotonic() - started >= max_seconds:
                break
            time.sleep(0.016)
    finally:
        renderer.release(ctx.context)
        target.destroy()
        window.close()
        runtime.close()
        quit_sdl()


if __name__ == "__main__":
    main()
