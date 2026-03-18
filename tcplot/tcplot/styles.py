"""Color palettes and line styles for plots."""

from __future__ import annotations

# Default color cycle (tab10-like palette, RGBA 0-1)
DEFAULT_COLORS = [
    (0.12, 0.47, 0.71, 1.0),   # blue
    (1.00, 0.50, 0.05, 1.0),   # orange
    (0.17, 0.63, 0.17, 1.0),   # green
    (0.84, 0.15, 0.16, 1.0),   # red
    (0.58, 0.40, 0.74, 1.0),   # purple
    (0.55, 0.34, 0.29, 1.0),   # brown
    (0.89, 0.47, 0.76, 1.0),   # pink
    (0.50, 0.50, 0.50, 1.0),   # gray
    (0.74, 0.74, 0.13, 1.0),   # olive
    (0.09, 0.75, 0.81, 1.0),   # cyan
]

# UI colors
AXIS_COLOR = (0.7, 0.7, 0.7, 1.0)
GRID_COLOR = (0.3, 0.3, 0.3, 0.5)
LABEL_COLOR = (0.8, 0.8, 0.8, 1.0)
BG_COLOR = (0.10, 0.10, 0.12, 1.0)
PLOT_AREA_BG = (0.13, 0.13, 0.15, 1.0)


def cycle_color(index: int) -> tuple[float, float, float, float]:
    """Get color from default cycle by index."""
    return DEFAULT_COLORS[index % len(DEFAULT_COLORS)]
