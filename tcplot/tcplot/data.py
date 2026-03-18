"""Data models for plot series — shared between 2D and 3D."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence

import numpy as np

from tcplot.styles import cycle_color


@dataclass
class LineSeries:
    """A single 2D line (or 3D polyline)."""
    x: np.ndarray
    y: np.ndarray
    z: np.ndarray | None = None  # for 3D
    color: tuple[float, float, float, float] | None = None
    thickness: float = 1.5
    label: str = ""


@dataclass
class ScatterSeries:
    """Scatter points."""
    x: np.ndarray
    y: np.ndarray
    z: np.ndarray | None = None
    color: tuple[float, float, float, float] | None = None
    size: float = 4.0
    label: str = ""


@dataclass
class PlotData:
    """Collection of series for one plot."""
    lines: list[LineSeries] = field(default_factory=list)
    scatters: list[ScatterSeries] = field(default_factory=list)

    title: str = ""
    x_label: str = ""
    y_label: str = ""

    def add_line(
        self,
        x: Sequence[float],
        y: Sequence[float],
        *,
        color: tuple | None = None,
        thickness: float = 1.5,
        label: str = "",
    ) -> LineSeries:
        xa = np.asarray(x, dtype=np.float64)
        ya = np.asarray(y, dtype=np.float64)
        if color is None:
            color = cycle_color(len(self.lines) + len(self.scatters))
        s = LineSeries(x=xa, y=ya, color=color, thickness=thickness, label=label)
        self.lines.append(s)
        return s

    def add_scatter(
        self,
        x: Sequence[float],
        y: Sequence[float],
        *,
        color: tuple | None = None,
        size: float = 4.0,
        label: str = "",
    ) -> ScatterSeries:
        xa = np.asarray(x, dtype=np.float64)
        ya = np.asarray(y, dtype=np.float64)
        if color is None:
            color = cycle_color(len(self.lines) + len(self.scatters))
        s = ScatterSeries(x=xa, y=ya, color=color, size=size, label=label)
        self.scatters.append(s)
        return s

    def data_bounds(self) -> tuple[float, float, float, float]:
        """Return (x_min, x_max, y_min, y_max) across all series."""
        xs, ys = [], []
        for s in self.lines:
            if len(s.x) > 0:
                xs.append(s.x.min())
                xs.append(s.x.max())
                ys.append(s.y.min())
                ys.append(s.y.max())
        for s in self.scatters:
            if len(s.x) > 0:
                xs.append(s.x.min())
                xs.append(s.x.max())
                ys.append(s.y.min())
                ys.append(s.y.max())
        if not xs:
            return (0.0, 1.0, 0.0, 1.0)
        return (min(xs), max(xs), min(ys), max(ys))
