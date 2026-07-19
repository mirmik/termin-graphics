from termin.scene import PythonComponent
from termin.render._render_native import install_drawable_vtable, drawable_capability_id
from termin.render.drawable import RenderItemCollectContext


class DrawableComponent(PythonComponent):
    """Component capable of rendering (has drawable vtable)."""

    component_category = "Rendering"
    is_drawable: bool = True

    def __init__(self, enabled: bool = True, display_name: str = ""):
        super().__init__(enabled=enabled, display_name=display_name)
        install_drawable_vtable(self._tc.c_ptr_int())

    def __init_subclass__(cls, **kwargs):
        cls.component_capabilities = tuple(
            dict.fromkeys((*cls.component_capabilities, drawable_capability_id()))
        )
        super().__init_subclass__(**kwargs)

    @property
    def phase_mask(self) -> int:
        return 0

    def collect_render_items(self, context: RenderItemCollectContext):
        raise NotImplementedError(
            f"{type(self).__name__}.collect_render_items() must return RenderItem objects"
        )


__all__ = ["DrawableComponent"]
