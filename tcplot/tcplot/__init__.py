"""tcplot — lightweight plotting library for tcgui."""

from tcplot.plot2d import Plot2D
from tcplot.plot3d import Plot3D
from tcplot.data import PlotData, LineSeries, ScatterSeries, SurfaceSeries
from tcplot.camera3d import OrbitCamera
from tcplot.styles import DEFAULT_COLORS, cycle_color

__all__ = [
    "Plot2D",
    "Plot3D",
    "PlotData",
    "LineSeries",
    "ScatterSeries",
    "SurfaceSeries",
    "OrbitCamera",
    "DEFAULT_COLORS",
    "cycle_color",
]
