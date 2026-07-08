"""Experimental native Termin UI document bindings."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("nanobind", "termin_base", "termin_graphics2", "termin_gui_native")

from termin.gui_native._gui_native import (  # noqa: E402
    Color,
    Document,
    DrawCommand,
    DrawCommandType,
    DrawList,
    DrawListRenderer,
    PaintContext,
    Point,
    Rect,
    Size,
    WidgetHandle,
    invalid_widget_handle,
)


class Widget:
    """Base class for Python widgets adopted by ``Document``."""

    debug_name: str | None = None

    def paint(self, context: PaintContext) -> None:
        pass


__all__ = [
    "Color",
    "Document",
    "DrawCommand",
    "DrawCommandType",
    "DrawList",
    "DrawListRenderer",
    "PaintContext",
    "Point",
    "Rect",
    "Size",
    "Widget",
    "WidgetHandle",
    "invalid_widget_handle",
]
