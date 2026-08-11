"""Framework-neutral native window ownership."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_window")

from termin.window._window_native import (  # noqa: E402
    BackendWindow,
    BackendWindowSystem,
    PresentationMode,
    SDLBackendWindow,
    WindowHandle,
    WindowManager,
    WindowedGraphicsSession,
    quit_sdl,
)

__all__ = [
    "BackendWindow",
    "BackendWindowSystem",
    "PresentationMode",
    "SDLBackendWindow",
    "WindowHandle",
    "WindowManager",
    "WindowedGraphicsSession",
    "quit_sdl",
]
