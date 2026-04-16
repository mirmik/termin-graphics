"""tcplot - lightweight plotting library for tcgui (native C++ core).

Public API surface kept stable across the Python-to-C++ port. All heavy
lifting (engines, camera math, mesh building) lives in the C++
``tcplot`` library bound via ``_tcplot_native``.  This module re-exports
those symbols plus the widget adapters ``Plot2D`` / ``Plot3D`` that
host the engines inside a tcgui Widget.
"""
from termin_nanobind.runtime import preload_sdk_libs

# Bring our native dependencies into the Windows DLL search path
# before importing the extension module. tcplot depends transitively
# on termin_graphics2, termin_mesh and termin_base via tcplot.dll.
preload_sdk_libs("tcplot", "termin_graphics2", "termin_mesh", "termin_base")

from tcplot._tcplot_native import (
    Color4,
    cycle_color,
    jet,
    default_colors,
    LineSeries,
    ScatterSeries,
    SurfaceSeries,
    PlotData,
    OrbitCamera,
    PickResult3D,
    PlotEngine2D,
    PlotEngine3D,
    MouseButton,
)

# DEFAULT_COLORS retained for legacy callers; materialise once from the
# native palette so callers that iterate the list don't trigger a C
# call per index.
DEFAULT_COLORS = list(default_colors())

# Widget adapters — tcgui.Widget subclasses, Python-only.
from tcplot.plot2d import Plot2D
from tcplot.plot3d import Plot3D

__all__ = [
    "Plot2D",
    "Plot3D",
    "PlotEngine2D",
    "PlotEngine3D",
    "PlotData",
    "LineSeries",
    "ScatterSeries",
    "SurfaceSeries",
    "OrbitCamera",
    "PickResult3D",
    "Color4",
    "MouseButton",
    "DEFAULT_COLORS",
    "cycle_color",
    "jet",
    "default_colors",
]
