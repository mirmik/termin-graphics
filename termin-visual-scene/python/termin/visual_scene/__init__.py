"""Handle-safe retained 2D visual scenes."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs(
    "termin_visual_scene",
    "termin_graphics2",
    "termin_base",
)

from termin.visual_scene._visual_scene_native import (
    GraphicItemRef2D,
    VisualScene2D,
)

__all__ = [
    "GraphicItemRef2D",
    "VisualScene2D",
]
