# termin.render - rendering framework
from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_render")

from termin.render._render_native import (
    GeometryDrawCall,
    PhaseDrawCall,
    RENDER_CATEGORY_ALL,
    RENDER_CATEGORY_COLLIDERS,
    RENDER_CATEGORY_NAVMESH,
    RenderItem,
    RenderTargetConfig,
    RenderSyncMode,
    SCENE_EXT_TYPE_RENDER_MOUNT,
    SCENE_EXT_TYPE_RENDER_STATE,
    SceneRenderMount,
    SceneRenderState,
    TcSceneLighting,
    ViewportConfig,
    drawable_capability_id,
    get_render_sync_mode,
    install_drawable_vtable,
    is_drawable,
    scene_render_mount,
    scene_render_state,
    set_render_sync_mode,
)
from termin.render.drawable import DEFAULT_GEOMETRY_ID, Drawable, RenderItemCollectContext
from termin.render.drawable_component import DrawableComponent
from termin.render.immediate import ImmediateRenderer
from termin.render.render_target_config import (
    deserialize_render_target_config,
    serialize_render_target_config,
)
from termin.render.viewport_config import (
    deserialize_viewport_config,
    serialize_viewport_config,
)

__all__ = [
    "DEFAULT_GEOMETRY_ID",
    "Drawable",
    "GeometryDrawCall",
    "PhaseDrawCall",
    "RENDER_CATEGORY_ALL",
    "RENDER_CATEGORY_COLLIDERS",
    "RENDER_CATEGORY_NAVMESH",
    "ImmediateRenderer",
    "RenderItem",
    "RenderItemCollectContext",
    "RenderTargetConfig",
    "RenderSyncMode",
    "SCENE_EXT_TYPE_RENDER_MOUNT",
    "SCENE_EXT_TYPE_RENDER_STATE",
    "SceneRenderMount",
    "SceneRenderState",
    "TcSceneLighting",
    "ViewportConfig",
    "serialize_render_target_config",
    "deserialize_render_target_config",
    "serialize_viewport_config",
    "deserialize_viewport_config",
    "drawable_capability_id",
    "get_render_sync_mode",
    "install_drawable_vtable",
    "is_drawable",
    "scene_render_mount",
    "scene_render_state",
    "set_render_sync_mode",
    "DrawableComponent",
]
