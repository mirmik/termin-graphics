"""Handle-safe retained 2D visual scenes."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs(
    "termin_visual_scene",
    "termin_graphics2",
    "termin_base",
)

from termin.visual_scene._visual_scene_native import (
    GraphicItemHandle,
    GraphicItemRef2D,
    PolylineItemRef2D,
    TcVisualScene,
    tc_visual_scene_create,
    tc_visual_scene_destroy,
)

__all__ = [
    "GraphicItemHandle",
    "GraphicItemRef2D",
    "PolylineItemRef2D",
    "TcVisualScene",
    "tc_visual_scene_create",
    "tc_visual_scene_destroy",
]
