# termin.render - rendering framework
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
