"""tcplot — lightweight plotting library for tcgui."""

from tcplot.plot2d import Plot2D
from tcplot.data import PlotData, LineSeries, ScatterSeries
from tcplot.styles import DEFAULT_COLORS, cycle_color

__all__ = [
    "Plot2D",
    "PlotData",
    "LineSeries",
    "ScatterSeries",
    "DEFAULT_COLORS",
    "cycle_color",
]
