"""Optional termin-window adapter for native GUI documents."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs(
    "termin_window",
    "termin_gui_native_window_adapter",
)

from termin.gui_native._gui_native_window import (  # noqa: E402
    GuiWindowAdapter,
    dynamic_texture_lease,
)

__all__ = ["GuiWindowAdapter", "dynamic_texture_lease"]
