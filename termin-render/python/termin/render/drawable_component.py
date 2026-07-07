from termin.scene import PythonComponent
from termin.render._render_native import install_drawable_vtable, drawable_capability_id
from termin.scene import ComponentRegistry


class DrawableComponent(PythonComponent):
    """Component capable of rendering (has drawable vtable)."""

    is_drawable: bool = True

    def __init__(self, enabled: bool = True, display_name: str = ""):
        super().__init__(enabled=enabled, display_name=display_name)
        install_drawable_vtable(self._tc.c_ptr_int())

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        ComponentRegistry.set_capability(cls.__name__, drawable_capability_id(), True)

    def phase_marks(self):
        return []


__all__ = ["DrawableComponent"]
