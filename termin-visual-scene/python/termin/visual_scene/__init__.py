"""Handle-safe retained 2D and 3D visual scenes."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs(
    "termin_visual_scene",
    "termin_mesh",
    "termin_graphics",
    "termin_graphics2",
    "termin_base",
)

from termin.visual_scene._visual_scene_native import (
    GraphicItemHandle,
    GraphicItemRef2D,
    HitResult3D,
    ActionEvent3D,
    TargetPointerEvent3D,
    TargetPointerEventKind3D,
    PointCloudItemRef3D,
    PointerDispatch3D,
    PointerEvent3D,
    PointerEventKind3D,
    PolylineItemRef2D,
    PrimitiveItemRef3D,
    SceneInteraction3D,
    StaticMeshItemRef3D,
    TcVisualScene,
    TcVisualScene3D,
    VisualItem3DHandle,
    VisualItemRef3D,
    tc_visual_scene_create,
    tc_visual_scene3d_create,
    tc_visual_scene3d_destroy,
    tc_visual_scene_destroy,
)

__all__ = [
    "GraphicItemHandle",
    "GraphicItemRef2D",
    "HitResult3D",
    "ActionEvent3D",
    "TargetPointerEvent3D",
    "TargetPointerEventKind3D",
    "PointCloudItemRef3D",
    "PointerDispatch3D",
    "PointerEvent3D",
    "PointerEventKind3D",
    "PolylineItemRef2D",
    "PrimitiveItemRef3D",
    "SceneInteraction3D",
    "StaticMeshItemRef3D",
    "TcVisualScene",
    "TcVisualScene3D",
    "VisualItem3DHandle",
    "VisualItemRef3D",
    "tc_visual_scene_create",
    "tc_visual_scene3d_create",
    "tc_visual_scene3d_destroy",
    "tc_visual_scene_destroy",
]
