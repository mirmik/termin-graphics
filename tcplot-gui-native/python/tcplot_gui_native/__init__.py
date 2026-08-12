"""Native UI widget adapters for :mod:`tcplot`."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from tcplot import SrgbColor, SurfaceColorMap, default_colors
from tcplot_gui_native._tcplot_gui_native import Plot2DAccess, Plot3DAccess


class Plot2D:
    """Retained line chart widget for ``termin-gui-native`` documents."""

    def __init__(self, document):
        self.widget = document.create_registered_widget("termin.gui.Plot2D")
        handle = self.widget.handle
        self._native = Plot2DAccess(document, handle.index, handle.generation)
        self._palette = list(default_colors())
        self._next_color = 0

    @property
    def handle(self):
        return self.widget.handle

    @property
    def line_count(self) -> int:
        return self._native.line_count

    @property
    def scatter_count(self) -> int:
        return self._native.scatter_count

    def _color(self, color: SrgbColor | None) -> SrgbColor:
        if color is not None:
            return color
        result = self._palette[self._next_color % len(self._palette)]
        self._next_color += 1
        return result

    @staticmethod
    def _xy(x, y):
        xa = np.ascontiguousarray(x, dtype=np.float64).reshape(-1)
        ya = np.ascontiguousarray(y, dtype=np.float64).reshape(-1)
        if xa.size != ya.size:
            raise ValueError("x and y arrays must have equal size")
        return xa, ya

    def plot(self, x, y, *, color: SrgbColor | None = None, thickness: float = 1.5) -> int:
        xa, ya = self._xy(x, y)
        resolved = self._color(color)
        index = self._native.add_line(
            resolved.r, resolved.g, resolved.b, resolved.a, thickness
        )
        if not self._native.set_line_data(index, xa, ya):
            raise RuntimeError("tcplot Plot2D rejected line data")
        return index

    def set_line_data(self, index: int, x, y) -> None:
        xa, ya = self._xy(x, y)
        if not self._native.set_line_data(index, xa, ya):
            raise RuntimeError("tcplot Plot2D rejected line data")

    def append_line_data(self, index: int, x, y) -> None:
        xa, ya = self._xy(x, y)
        if not self._native.append_line_data(index, xa, ya):
            raise RuntimeError("tcplot Plot2D rejected appended line data")

    def scatter(
        self,
        x,
        y,
        *,
        color: SrgbColor | None = None,
        size: float = 5.0,
    ) -> int:
        xa, ya = self._xy(x, y)
        resolved = self._color(color)
        index = self._native.add_scatter(
            resolved.r, resolved.g, resolved.b, resolved.a, size
        )
        if not self._native.set_scatter_data(index, xa, ya):
            raise RuntimeError("tcplot Plot2D rejected scatter data")
        return index

    def create_data_marker(
        self,
        x: float,
        y: float,
        text: str,
        *,
        snap_line_index: int = 0,
    ) -> tuple[int, int, int]:
        handle = tuple(
            int(value)
            for value in self._native.create_data_marker(
                x, y, text, snap_line_index
            )
        )
        if handle[0] == 0:
            raise RuntimeError("tcplot Plot2D rejected data marker")
        return handle

    def clear(self) -> None:
        self._native.clear_lines()
        self._native.clear_scatters()
        self._next_color = 0

    def set_title(self, title: str) -> None:
        self._native.set_title(title)

    def set_axis_labels(self, x: str, y: str) -> None:
        self._native.set_axis_labels(x, y)

    def set_auto_fit(self, enabled: bool) -> None:
        self._native.set_auto_fit(enabled)

    def set_view(self, x_min: float, x_max: float, y_min: float, y_max: float) -> None:
        self._native.set_view(x_min, x_max, y_min, y_max)


@dataclass(frozen=True)
class Plot3DItem:
    scene_id: int
    index: int
    generation: int


class Plot3D:
    """Interactive ``termin-gui-native`` widget backed by ``RetainedChart3D``."""

    def __init__(self, document):
        self.widget = document.create_registered_widget("termin.gui.Plot3D")
        handle = self.widget.handle
        self._native = Plot3DAccess(document, handle.index, handle.generation)
        self._palette = list(default_colors())
        self._next_color = 0

    @property
    def handle(self):
        return self.widget.handle

    @property
    def scene_id(self) -> int:
        return self._native.scene_id

    @property
    def item_count(self) -> int:
        return self._native.item_count

    @property
    def texture_id(self) -> int:
        return self._native.texture_id

    def _color(self, color: SrgbColor | None) -> SrgbColor:
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

    @staticmethod
    def _item(value) -> Plot3DItem:
        return Plot3DItem(int(value[0]), int(value[1]), int(value[2]))

    def plot(self, x, y, z, *, color: SrgbColor | None = None, thickness: float = 1.5):
        xa, ya, za = self._xyz(x, y, z)
        resolved = self._color(color)
        return self._item(
            self._native.add_line(
                xa, ya, za, resolved.r, resolved.g, resolved.b, resolved.a, thickness
            )
        )

    def scatter(self, x, y, z, *, color: SrgbColor | None = None, size: float = 4.0):
        xa, ya, za = self._xyz(x, y, z)
        resolved = self._color(color)
        return self._item(
            self._native.add_scatter(
                xa, ya, za, resolved.r, resolved.g, resolved.b, resolved.a, size
            )
        )

    def surface(
        self,
        x,
        y,
        z,
        *,
        color: SrgbColor | None = None,
        colormap: SurfaceColorMap = SurfaceColorMap.Jet,
        wireframe: bool = False,
    ):
        xa = np.ascontiguousarray(x, dtype=np.float64)
        ya = np.ascontiguousarray(y, dtype=np.float64)
        za = np.ascontiguousarray(z, dtype=np.float64)
        if xa.ndim != 2 or ya.shape != xa.shape or za.shape != xa.shape:
            raise ValueError("x, y and z must be equally shaped 2D arrays")
        resolved = self._color(color)
        rows, columns = xa.shape
        return self._item(
            self._native.add_surface(
                xa.reshape(-1),
                ya.reshape(-1),
                za.reshape(-1),
                rows,
                columns,
                resolved.r,
                resolved.g,
                resolved.b,
                resolved.a,
                int(colormap.value),
                wireframe,
            )
        )

    def destroy_item(self, item: Plot3DItem) -> bool:
        return self._native.destroy_item(item.scene_id, item.index, item.generation)

    def clear(self) -> None:
        self._native.clear()
        self._next_color = 0

    def set_axis_labels(self, x: str, y: str, z: str) -> None:
        self._native.set_axis_labels(x, y, z)

    def set_axis_scale(self, x: float, y: float, z: float) -> None:
        self._native.set_axis_scale(x, y, z)

    def set_surface_shading(self, enabled: bool, strength: float = 0.35) -> None:
        self._native.set_surface_shading(enabled, strength)

    def set_light_direction(self, x: float, y: float, z: float) -> None:
        self._native.set_light_direction(x, y, z)

    def show_colorbar(self, surface: Plot3DItem, label: str = "") -> None:
        self._native.set_colorbar(
            surface.scene_id,
            surface.index,
            surface.generation,
            label,
        )

    def clear_colorbar(self) -> None:
        self._native.clear_colorbar()

    def fit_camera(self) -> None:
        self._native.fit_camera()

    def reset_camera(self) -> None:
        self._native.reset_camera()


__all__ = ["Plot2D", "Plot3D", "Plot3DItem"]
