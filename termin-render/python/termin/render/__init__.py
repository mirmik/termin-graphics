# termin.render - rendering framework
from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_render")

from termin.render._render_native import (
    drawable_capability_id,
    install_drawable_vtable,
    is_drawable,
)
from termin.render.drawable_component import DrawableComponent

__all__ = [
    "drawable_capability_id",
    "install_drawable_vtable",
    "is_drawable",
    "DrawableComponent",
]
