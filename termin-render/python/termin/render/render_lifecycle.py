from termin.scene import PythonComponent
from termin.render._render_native import (
    install_render_lifecycle,
    render_lifecycle_priority,
    render_lifecycle_capability_id,
    set_render_lifecycle_priority,
)


class RenderLifecycleComponent(PythonComponent):
    """Python component participating in render attach/prepare/detach."""

    def __init__(self, enabled: bool = True, display_name: str = ""):
        super().__init__(enabled=enabled, display_name=display_name)
        install_render_lifecycle(self._tc.c_ptr_int())

    def __init_subclass__(cls, **kwargs):
        cls.component_capabilities = tuple(
            dict.fromkeys(
                (*cls.component_capabilities, render_lifecycle_capability_id())
            )
        )
        super().__init_subclass__(**kwargs)

    def on_render_attach(self, context) -> None:
        pass

    @property
    def render_prepare_priority(self) -> int:
        return render_lifecycle_priority(self._tc.c_ptr_int())

    @render_prepare_priority.setter
    def render_prepare_priority(self, value: int) -> None:
        set_render_lifecycle_priority(self._tc.c_ptr_int(), value)

    def prepare_render(self, context) -> None:
        pass

    def on_render_detach(self, context) -> None:
        pass


__all__ = ["RenderLifecycleComponent"]
