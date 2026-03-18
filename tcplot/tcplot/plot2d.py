"""2D Plot widget for tcgui."""

from __future__ import annotations

import math

import numpy as np

from tcgui.widgets.widget import Widget
from tcgui.widgets.events import MouseEvent, MouseWheelEvent
from tcbase import MouseButton

from tcplot.data import PlotData
from tcplot.axes import nice_ticks, format_tick
from tcplot import styles


class Plot2D(Widget):
    """Interactive 2D plot widget.

    Supports:
    - Line plots, scatter plots
    - Pan (middle mouse drag) and zoom (scroll wheel)
    - Auto-fit to data
    - Axis ticks, grid, labels, title
    """

    def __init__(self):
        super().__init__()
        self.data = PlotData()

        # Margins in pixels (left, right, top, bottom)
        self.margin_left = 60
        self.margin_right = 15
        self.margin_top = 30
        self.margin_bottom = 35

        # View range in data coordinates (None = auto-fit)
        self._view_x_min: float | None = None
        self._view_x_max: float | None = None
        self._view_y_min: float | None = None
        self._view_y_max: float | None = None

        # Pan state
        self._panning = False
        self._pan_start_mx = 0.0
        self._pan_start_my = 0.0
        self._pan_start_view: tuple[float, float, float, float] = (0, 1, 0, 1)

        # Style
        self.show_grid = True
        self.grid_color = styles.GRID_COLOR
        self.axis_color = styles.AXIS_COLOR
        self.label_color = styles.LABEL_COLOR
        self.bg_color = styles.BG_COLOR
        self.plot_bg_color = styles.PLOT_AREA_BG
        self.font_size = 11.0
        self.title_font_size = 14.0

    # -- Public API --

    def plot(self, x, y, *, color=None, thickness=1.5, label=""):
        """Add a line series."""
        self.data.add_line(x, y, color=color, thickness=thickness, label=label)
        if self._view_x_min is None:
            self.fit()

    def scatter(self, x, y, *, color=None, size=4.0, label=""):
        """Add a scatter series."""
        self.data.add_scatter(x, y, color=color, size=size, label=label)
        if self._view_x_min is None:
            self.fit()

    def clear(self):
        """Remove all series."""
        self.data = PlotData()
        self._view_x_min = None

    def fit(self):
        """Auto-fit view to data bounds (with padding)."""
        x0, x1, y0, y1 = self.data.data_bounds()
        dx = x1 - x0 if x1 > x0 else 1.0
        dy = y1 - y0 if y1 > y0 else 1.0
        pad = 0.05
        self._view_x_min = x0 - dx * pad
        self._view_x_max = x1 + dx * pad
        self._view_y_min = y0 - dy * pad
        self._view_y_max = y1 + dy * pad

    def set_view(self, x_min: float, x_max: float, y_min: float, y_max: float):
        """Set view range explicitly."""
        self._view_x_min = x_min
        self._view_x_max = x_max
        self._view_y_min = y_min
        self._view_y_max = y_max

    # -- Coordinate transforms --

    def _plot_area(self) -> tuple[float, float, float, float]:
        """Return (px, py, pw, ph) of the plot area in widget pixels."""
        px = self.x + self.margin_left
        py = self.y + self.margin_top
        pw = self.width - self.margin_left - self.margin_right
        ph = self.height - self.margin_top - self.margin_bottom
        return (px, py, max(pw, 1), max(ph, 1))

    def _view(self) -> tuple[float, float, float, float]:
        """Current view in data coords."""
        if self._view_x_min is None:
            self.fit()
        return (self._view_x_min, self._view_x_max,
                self._view_y_min, self._view_y_max)

    def _data_to_pixel(self, dx: float, dy: float) -> tuple[float, float]:
        """Convert data coordinates to widget pixel coordinates."""
        px, py, pw, ph = self._plot_area()
        vx0, vx1, vy0, vy1 = self._view()
        sx = (dx - vx0) / (vx1 - vx0) if vx1 != vx0 else 0.5
        sy = (dy - vy0) / (vy1 - vy0) if vy1 != vy0 else 0.5
        return (px + sx * pw, py + (1.0 - sy) * ph)  # Y flipped

    def _pixel_to_data(self, wx: float, wy: float) -> tuple[float, float]:
        """Convert widget pixel to data coordinates."""
        px, py, pw, ph = self._plot_area()
        vx0, vx1, vy0, vy1 = self._view()
        sx = (wx - px) / pw
        sy = 1.0 - (wy - py) / ph
        return (vx0 + sx * (vx1 - vx0), vy0 + sy * (vy1 - vy0))

    # -- Rendering --

    def render(self, renderer):
        if self.width <= 0 or self.height <= 0:
            print(f"[Plot2D] SKIP render: x={self.x} y={self.y} w={self.width} h={self.height}")
            return

        # Background
        renderer.draw_rect(self.x, self.y, self.width, self.height, self.bg_color)

        px, py, pw, ph = self._plot_area()

        # Plot area background
        renderer.draw_rect(px, py, pw, ph, self.plot_bg_color)

        vx0, vx1, vy0, vy1 = self._view()

        # Clip to plot area
        renderer.begin_clip(px, py, pw, ph)

        # Grid
        if self.show_grid:
            self._draw_grid(renderer, px, py, pw, ph, vx0, vx1, vy0, vy1)

        # Series
        for series in self.data.lines:
            self._draw_line_series(renderer, series)
        for series in self.data.scatters:
            self._draw_scatter_series(renderer, series)

        renderer.end_clip()

        # Axes border
        renderer.draw_rect_outline(px, py, pw, ph, self.axis_color, 1.0)

        # Tick labels
        self._draw_tick_labels(renderer, px, py, pw, ph, vx0, vx1, vy0, vy1)

        # Title
        if self.data.title:
            renderer.draw_text_centered(
                self.x + self.width / 2,
                self.y + self.margin_top / 2,
                self.data.title,
                self.label_color,
                self.title_font_size,
            )

        # Axis labels
        if self.data.x_label:
            renderer.draw_text_centered(
                px + pw / 2,
                py + ph + self.margin_bottom - 4,
                self.data.x_label,
                self.label_color,
                self.font_size,
            )
        if self.data.y_label:
            # Vertical label — draw horizontally at left margin for now
            renderer.draw_text_centered(
                self.x + self.margin_left / 2,
                py + ph / 2,
                self.data.y_label,
                self.label_color,
                self.font_size,
            )

    def _draw_grid(self, renderer, px, py, pw, ph, vx0, vx1, vy0, vy1):
        max_x_ticks = max(int(pw / 80), 3)
        max_y_ticks = max(int(ph / 50), 3)
        x_ticks = nice_ticks(vx0, vx1, max_x_ticks)
        y_ticks = nice_ticks(vy0, vy1, max_y_ticks)

        for tx in x_ticks:
            sx, _ = self._data_to_pixel(tx, 0)
            renderer.draw_line(sx, py, sx, py + ph, self.grid_color, 1.0)

        for ty in y_ticks:
            _, sy = self._data_to_pixel(0, ty)
            renderer.draw_line(px, sy, px + pw, sy, self.grid_color, 1.0)

    def _draw_tick_labels(self, renderer, px, py, pw, ph, vx0, vx1, vy0, vy1):
        max_x_ticks = max(int(pw / 80), 3)
        max_y_ticks = max(int(ph / 50), 3)
        x_ticks = nice_ticks(vx0, vx1, max_x_ticks)
        y_ticks = nice_ticks(vy0, vy1, max_y_ticks)

        for tx in x_ticks:
            sx, _ = self._data_to_pixel(tx, 0)
            label = format_tick(tx)
            renderer.draw_text_centered(
                sx, py + ph + 14, label, self.label_color, self.font_size - 1)

        for ty in y_ticks:
            _, sy = self._data_to_pixel(0, ty)
            label = format_tick(ty)
            tw, _ = renderer.measure_text(label, self.font_size - 1)
            renderer.draw_text(
                px - tw - 6, sy + 4, label, self.label_color, self.font_size - 1)

    def _draw_line_series(self, renderer, series):
        if len(series.x) < 2:
            return
        color = series.color or styles.AXIS_COLOR
        for i in range(len(series.x) - 1):
            x1, y1 = self._data_to_pixel(series.x[i], series.y[i])
            x2, y2 = self._data_to_pixel(series.x[i + 1], series.y[i + 1])
            renderer.draw_line(x1, y1, x2, y2, color, series.thickness)

    def _draw_scatter_series(self, renderer, series):
        color = series.color or styles.AXIS_COLOR
        half = series.size / 2
        for i in range(len(series.x)):
            sx, sy = self._data_to_pixel(series.x[i], series.y[i])
            renderer.draw_rect(sx - half, sy - half, series.size, series.size, color)

    # -- Interaction --

    def on_mouse_down(self, event: MouseEvent) -> bool:
        if event.button == MouseButton.MIDDLE:
            self._panning = True
            self._pan_start_mx = event.x
            self._pan_start_my = event.y
            self._pan_start_view = self._view()
            return True
        return False

    def on_mouse_move(self, event: MouseEvent):
        if self._panning:
            px, py, pw, ph = self._plot_area()
            vx0, vx1, vy0, vy1 = self._pan_start_view
            dx_px = event.x - self._pan_start_mx
            dy_px = event.y - self._pan_start_my
            dx_data = -dx_px / pw * (vx1 - vx0)
            dy_data = dy_px / ph * (vy1 - vy0)  # Y flipped
            self._view_x_min = vx0 + dx_data
            self._view_x_max = vx1 + dx_data
            self._view_y_min = vy0 + dy_data
            self._view_y_max = vy1 + dy_data

    def on_mouse_up(self, event: MouseEvent):
        self._panning = False

    def on_mouse_wheel(self, event: MouseWheelEvent) -> bool:
        px, py, pw, ph = self._plot_area()
        # Only zoom if cursor is in plot area
        if not (px <= event.x <= px + pw and py <= event.y <= py + ph):
            return False

        factor = 0.85 if event.dy > 0 else 1.0 / 0.85

        # Zoom around cursor position
        cx, cy = self._pixel_to_data(event.x, event.y)
        vx0, vx1, vy0, vy1 = self._view()

        self._view_x_min = cx + (vx0 - cx) * factor
        self._view_x_max = cx + (vx1 - cx) * factor
        self._view_y_min = cy + (vy0 - cy) * factor
        self._view_y_max = cy + (vy1 - cy) * factor
        return True
