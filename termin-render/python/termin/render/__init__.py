# termin.render - rendering framework
from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_render")

from termin.render._render_native import (
    GeometryDrawCall,
    PhaseDrawCall,
    RenderTargetConfig,
    ViewportConfig,
    drawable_capability_id,
    install_drawable_vtable,
    is_drawable,
)
from termin.render.drawable import DEFAULT_GEOMETRY_ID, Drawable
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
    "ImmediateRenderer",
    "RenderTargetConfig",
    "ViewportConfig",
    "serialize_render_target_config",
    "deserialize_render_target_config",
    "serialize_viewport_config",
    "deserialize_viewport_config",
    "drawable_capability_id",
    "install_drawable_vtable",
    "is_drawable",
    "DrawableComponent",
]
