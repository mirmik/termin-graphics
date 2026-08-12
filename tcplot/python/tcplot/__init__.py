"""Toolkit-neutral plotting engines and retained chart primitives.

All rendering, camera math and mesh building lives in the native ``tcplot``
library bound through :mod:`tcplot._tcplot_native`. Ready-made Termin UI
widgets are provided by the optional :mod:`tcplot_gui_native` package; the
core package deliberately does not import a UI toolkit.
"""
from termin_nanobind.runtime import preload_sdk_libs

# Bring our native dependencies into the Windows DLL search path
# before importing the extension module. tcplot depends transitively
# on termin_graphics2, termin_mesh and termin_base via tcplot.dll.
preload_sdk_libs("tcplot", "termin_graphics2", "termin_mesh", "termin_base")

from tcplot._tcplot_native import (
    SrgbColor,
    cycle_color,
    jet,
    default_colors,
    LineSeries,
    ScatterSeries,
    SurfaceSeries,
    PlotData,
    OrbitCamera,
    PickResult3D,
    PlotAnnotationHandle,
    PlotAnnotationAction2D,
    PlotDataMarker2D,
    PlotDataMarkerSnapshot2D,
    PlotEngine2D,
    PlotEngine3D,
    RetainedChart3D,
    PlotItem3DHandle,
    SurfaceColorMap,
    MouseButton,
)

# DEFAULT_COLORS retained for legacy callers; materialise once from the
# native palette so callers that iterate the list don't trigger a C
# call per index.
DEFAULT_COLORS = list(default_colors())

__all__ = [
    "PlotEngine2D",
    "PlotEngine3D",
    "RetainedChart3D",
    "PlotItem3DHandle",
    "PlotData",
    "LineSeries",
    "ScatterSeries",
    "SurfaceSeries",
    "OrbitCamera",
    "PickResult3D",
    "PlotAnnotationHandle",
    "PlotAnnotationAction2D",
    "PlotDataMarker2D",
    "PlotDataMarkerSnapshot2D",
    "SrgbColor",
    "SurfaceColorMap",
    "MouseButton",
    "DEFAULT_COLORS",
    "cycle_color",
    "jet",
    "default_colors",
]
