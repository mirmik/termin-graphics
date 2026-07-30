"""Thin Python facade over the native UiScript v2 implementation."""

from termin.gui_native._gui_native import (
    LoadedUiScript,
    MaterializedWidget,
    UiScriptDescription,
    UiScriptError,
    UiScriptLoader,
    UiScriptNode,
    UiScriptParser,
)

UISCRIPT_VERSION = 2

__all__ = [
    "LoadedUiScript",
    "MaterializedWidget",
    "UISCRIPT_VERSION",
    "UiScriptDescription",
    "UiScriptError",
    "UiScriptLoader",
    "UiScriptNode",
    "UiScriptParser",
]
