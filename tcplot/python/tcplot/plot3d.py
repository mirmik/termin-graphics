"""CPU-first retained 3D plot widget for tcgui."""

from __future__ import annotations

import numpy as np

from tcgui.widgets.events import MouseEvent, MouseWheelEvent
from tcgui.widgets.widget import Widget

from tcplot._tcplot_native import (
    LineSeries,
    PlotData,
    RetainedChart3D,
    ScatterSeries,
    SurfaceColorMap,
    SurfaceSeries,
    default_colors,
    SrgbColor,
)


class Plot3D(Widget):
    """A retained 3D chart whose CPU scene exists before its GPU renderer.

    Series are copied into a native retained scene immediately. The scene is
    attached to the application's canonical graphics host only during the
    first render, so callers may freely populate a widget before creating a
    window or selecting a graphics backend.
    """

    _LABEL_COLOR = SrgbColor(0.8, 0.8, 0.8, 1.0)
    _SURFACE_GRID_COLOR = SrgbColor(0.05, 0.05, 0.05, 1.0)

    def __init__(self):
        super().__init__()
        self.data = PlotData()
        self._scene = RetainedChart3D()
        self._line_handles = []
        self._scatter_handles = []
        self._surface_handles = []
        self._palette = list(default_colors())
        self._next_color = 0
        self._axis_scale = [1.0, 1.0, 1.0]
        self._show_wireframe = False

        from tcgui.widgets.button import Button
        from tcgui.widgets.units import px

        self._wire_btn = Button()
        self._wire_btn.text = "W"
        self._wire_btn.preferred_width = px(28)
        self._wire_btn.preferred_height = px(28)
        self._wire_btn.on_click = self.toggle_wireframe
        self.add_child(self._wire_btn)

    def _color(self, color):
        if color is not None:
            return color
        result = self._palette[self._next_color % len(self._palette)]
        self._next_color += 1
        return result

    @staticmethod
    def _xyz(x, y, z):
        xa = np.ascontiguousarray(x, dtype=np.float64).reshape(-1)
        ya = np.ascontiguousarray(y, dtype=np.float64).reshape(-1)
        za = np.ascontiguousarray(z, dtype=np.float64).reshape(-1)
        if xa.size != ya.size or xa.size != za.size:
            raise ValueError("x, y and z arrays must have equal size")
        return xa, ya, za

    @property
    def z_scale(self) -> float:
        return self._axis_scale[2]

    @z_scale.setter
    def z_scale(self, value: float) -> None:
        self.set_axis_scale(self._axis_scale[0], self._axis_scale[1], value)

    @property
    def show_wireframe(self) -> bool:
        return self._show_wireframe

    def set_axis_scale(self, x: float, y: float, z: float) -> None:
        self._scene.set_axis_scale(x, y, z)
        self._axis_scale[:] = [float(x), float(y), float(z)]

    def set_axis_labels(self, x: str, y: str, z: str) -> None:
        self.data.x_label = x
        self.data.y_label = y
        self.data.z_label = z
        self._scene.set_axis_labels(x, y, z)

    def set_surface_shading(self, enabled: bool, strength: float = 0.35) -> None:
        self._scene.set_surface_shading(enabled, strength)

    def set_surface_light_dir(self, x: float, y: float, z: float) -> None:
        self._scene.set_light_direction(x, y, z)

    def plot(self, x, y, z, *, color=None, thickness=1.5, label=""):
        xa, ya, za = self._xyz(x, y, z)
        resolved = self._color(color)
        handle = self._scene.add_line(
            xa, ya, za, color=resolved, thickness=thickness
        )
        self._line_handles.append(handle)

        series = LineSeries()
        series.x = xa.tolist()
        series.y = ya.tolist()
        series.z = za.tolist()
        series.thickness = thickness
        series.label = label
        self.data.lines = [*self.data.lines, series]
        return handle

    def scatter(self, x, y, z, *, color=None, size=4.0, label=""):
        xa, ya, za = self._xyz(x, y, z)
        resolved = self._color(color)
        handle = self._scene.add_scatter(
            xa, ya, za, color=resolved, size=size
        )
        self._scatter_handles.append(handle)

        series = ScatterSeries()
        series.x = xa.tolist()
        series.y = ya.tolist()
        series.z = za.tolist()
        series.size = size
        series.label = label
        self.data.scatters = [*self.data.scatters, series]
        return handle

    def surface(
        self,
        X,
        Y,
        Z,
        *,
        color=None,
        colormap=None,
        wireframe=False,
        label="",
    ):
        xa = np.ascontiguousarray(X, dtype=np.float64)
        ya = np.ascontiguousarray(Y, dtype=np.float64)
        za = np.ascontiguousarray(Z, dtype=np.float64)
        if xa.ndim != 2 or ya.shape != xa.shape or za.shape != xa.shape:
            raise ValueError("X, Y and Z must be equally shaped 2D arrays")
        rows, columns = za.shape
        resolved = self._color(color)
        resolved_colormap = (
            SurfaceColorMap.Jet if colormap is None else colormap
        )
        handle = self._scene.add_surface(
            xa.reshape(-1),
            ya.reshape(-1),
            za.reshape(-1),
            rows,
            columns,
            color=resolved,
            colormap=resolved_colormap,
            wireframe=wireframe or self._show_wireframe,
        )
        self._surface_handles.append(handle)

        series = SurfaceSeries()
        series.X = xa.reshape(-1).tolist()
        series.Y = ya.reshape(-1).tolist()
        series.Z = za.reshape(-1).tolist()
        series.rows = rows
        series.cols = columns
        series.wireframe = wireframe
        series.label = label
        self.data.surfaces = [*self.data.surfaces, series]
        return handle

    def clear(self):
        for handle in (
            self._line_handles
            + self._scatter_handles
            + self._surface_handles
        ):
            if not self._scene.destroy_item(handle):
                raise RuntimeError("RetainedChart3D rejected a live item handle")
        self._line_handles.clear()
        self._scatter_handles.clear()
        self._surface_handles.clear()
        self.data.lines = []
        self.data.scatters = []
        self.data.surfaces = []
        self._next_color = 0

    def set_surface_grid(
        self,
        idx,
        visible=True,
        row_step=8,
        col_step=8,
        color=_SURFACE_GRID_COLOR,
        width_px=1.5,
    ):
        if idx < 0 or idx >= len(self._surface_handles):
            return False
        return self._scene.set_surface_grid(
            self._surface_handles[idx],
            visible,
            row_step,
            col_step,
            color,
            width_px,
        )

    def toggle_wireframe(self):
        enabled = not self._show_wireframe
        for handle in self._surface_handles:
            if not self._scene.set_surface_wireframe(handle, enabled):
                raise RuntimeError("RetainedChart3D rejected a surface handle")
        self._show_wireframe = enabled

    def layout(self, x, y, width, height, viewport_w, viewport_h):
        super().layout(x, y, width, height, viewport_w, viewport_h)
        self._wire_btn.layout(
            x + width - 36, y + 8, 28, 28, viewport_w, viewport_h
        )

    def compute_size(self, viewport_w: float, viewport_h: float):
        width = (
            self.preferred_width.to_pixels(viewport_w)
            if self.preferred_width
            else viewport_w
        )
        height = (
            self.preferred_height.to_pixels(viewport_h)
            if self.preferred_height
            else viewport_h
        )
        return width, height

    def render(self, renderer):
        width = max(1, int(round(self.width)))
        height = max(1, int(round(self.height)))
        if self.width <= 0 or self.height <= 0:
            return

        self._scene.set_axis_labels(
            self.data.x_label or "x",
            self.data.y_label or "y",
            self.data.z_label or "z",
        )
        renderer.begin_clip(self.x, self.y, self.width, self.height)
        texture = renderer.render_offscreen(
            lambda graphics_host, font: self._scene.render(
                graphics_host, font, width, height
            )
        )
        renderer.draw_texture(
            self.x,
            self.y,
            self.width,
            self.height,
            texture,
            width,
            height,
            flip_v=not renderer.graphics.texture_origin_top_left,
        )
        renderer.end_clip()

        if self.data.title:
            renderer.draw_text_centered(
                self.x + self.width / 2,
                self.y + 16,
                self.data.title,
                self._LABEL_COLOR,
                14.0,
            )
        for child in self.children:
            if child.visible:
                child.render(renderer)

    def on_mouse_down(self, event: MouseEvent) -> bool:
        return self._scene.pointer_down(
            event.x - self.x, event.y - self.y, int(event.button.value)
        )

    def on_mouse_move(self, event: MouseEvent):
        self._scene.pointer_move(event.x - self.x, event.y - self.y)

    def on_mouse_up(self, event: MouseEvent):
        self._scene.pointer_up(
            event.x - self.x, event.y - self.y, int(event.button.value)
        )

    def on_mouse_wheel(self, event: MouseWheelEvent) -> bool:
        return self._scene.wheel(
            event.x - self.x, event.y - self.y, event.dy
        )
