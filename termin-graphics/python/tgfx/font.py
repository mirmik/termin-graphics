"""Font atlas - thin re-export of the C++ tgfx2::FontAtlas.

The atlas class itself lives in C++ (termin-graphics/src/tgfx2/font_atlas.cpp,
bound as ``FontTextureAtlas`` in ``_tgfx_native``). This module keeps the
public name, plus Python-side convenience helpers for locating a
system TTF file and producing a lazily-initialised default atlas.

Migration note: pre-migration ``tgfx/font.py`` owned a PIL-based atlas
with a ``glyphs`` dict and separate tgfx1 / tgfx2 upload paths. The
C++ port only speaks tgfx2; callers pass a ``RenderContext2`` (i.e.
``holder.context`` in Python idiom), not a ``Tgfx2Context`` holder.
"""
from __future__ import annotations

import os
import sys

from tgfx._tgfx_native import FontTextureAtlas

__all__ = ["FontTextureAtlas", "find_system_font", "get_default_font"]


# ---------------------------------------------------------------------------
# Font discovery
# ---------------------------------------------------------------------------

def find_system_font() -> str | None:
    """Return the path to a usable system TTF font, or None."""
    if sys.platform == "win32":
        fonts_dir = os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts")
        candidates = [
            os.path.join(fonts_dir, "segoeui.ttf"),
            os.path.join(fonts_dir, "arial.ttf"),
            os.path.join(fonts_dir, "tahoma.ttf"),
        ]
    elif sys.platform == "darwin":
        candidates = [
            "/System/Library/Fonts/SFNSText.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/Library/Fonts/Arial.ttf",
        ]
    else:
        candidates = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
        ]
    return next((p for p in candidates if os.path.exists(p)), None)


_default_font_atlas: FontTextureAtlas | None = None


def get_default_font(size: int = 14) -> FontTextureAtlas | None:
    """Return (or lazily create) the global default font atlas."""
    global _default_font_atlas
    if _default_font_atlas is None:
        path = find_system_font()
        if path:
            try:
                _default_font_atlas = FontTextureAtlas(path, size)
            except Exception as e:
                from tgfx import log
                log.warn(f"[Font] Failed to load system font '{path}': {e}")
    return _default_font_atlas
