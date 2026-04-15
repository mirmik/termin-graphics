"""Dynamic font texture atlas.

Glyphs are rasterized on demand and added to a pre-allocated atlas.
Any Unicode character supported by the loaded TTF is automatically
available — no pre-enumeration required.

GPU upload strategy
-------------------
The atlas is a grayscale (1-channel) 2048×2048 image pre-allocated on
the CPU.  On first use ``ensure_texture`` uploads it via
``create_texture``.  Whenever new glyphs are added the dirty flag is
set; the next ``ensure_texture`` call issues ``update_texture`` to push
only the changed data (full texture re-upload, but at 2 MB it is fast).
"""
from __future__ import annotations

import os
import sys
from typing import TYPE_CHECKING

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from tgfx import log

if TYPE_CHECKING:
    from tgfx import GPUTextureHandle

# ---------------------------------------------------------------------------
# Atlas constants
# ---------------------------------------------------------------------------
_ATLAS_W = 2048
_ATLAS_H = 2048
_PADDING = 2  # pixels between glyphs

# Characters to warm up at init so the first render frame is atlas-clean.
_PRELOAD = (
    [chr(i) for i in range(32, 127)]  # ASCII printable
    + list(
        "\u25B6\u25BC\u25B2\u25C0"  # ▶▼▲◀ triangles
        "\u25A0\u25A1\u25A3\u25AB"  # ■□▣▫ squares
        "\u25CB\u25CF\u25C8\u25C6"  # ○●◈◆ circles/diamonds
        "\u2022\u2023\u25E6"        # •‣◦ bullets
        "\u2713\u2717\u2715"        # ✓✗✕ check/cross
        "\u25B7\u25BD\u25C1\u25B3"  # ▷▽◁△ hollow triangles
        "\u2190\u2192\u2191\u2193"  # ←→↑↓ arrows
        "\u2302"                    # ⌂ home
    )
)


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
            except Exception:
                log.warning("[Font] Failed to load system font", exc_info=True)
    return _default_font_atlas


# ---------------------------------------------------------------------------
# Dynamic atlas
# ---------------------------------------------------------------------------

class FontTextureAtlas:
    """Rasterizes TTF glyphs on demand into a shared 2048×2048 GPU texture.

    Any character supported by the underlying font is rendered automatically
    on first access — Cyrillic, Greek, Arabic, CJK, emoji, etc.

    Thread safety: single-threaded (UI render loop); no locking.
    """

    def __init__(self, path: str, size: int = 32):
        if not os.path.exists(path):
            raise FileNotFoundError(f"Font file not found: {path}")

        self._pil_font = ImageFont.truetype(path, size)
        self.size = size

        ascent, descent = self._pil_font.getmetrics()
        self.ascent: int = ascent
        self._line_height: int = ascent + descent

        # Glyph registry: char → {"uv": (u0,v0,u1,v1), "size": (w,h)}
        self.glyphs: dict[str, dict] = {}

        # CPU atlas
        self._atlas = Image.new("L", (_ATLAS_W, _ATLAS_H), 0)
        self._atlas_np: np.ndarray | None = None  # lazy numpy view
        self._dirty = False

        # Shelf packer state
        self._shelf_x = 0
        self._shelf_y = 0
        self._shelf_h = 0

        # GPU handle (uploaded lazily)
        self._handle: GPUTextureHandle | None = None

        # Warm up with common chars so first frame has no re-uploads
        for ch in _PRELOAD:
            self._rasterize(ch)
        # Mark dirty so ensure_texture will upload on first call
        self._dirty = True

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def ensure_glyphs(self, text: str, graphics=None) -> None:
        """Ensure every character in *text* is rasterized.

        If *graphics* is given and any new glyph was added, the GPU
        texture is refreshed immediately.
        """
        added = False
        for ch in text:
            if self._rasterize(ch):
                added = True
        if added and graphics is not None:
            self._sync_gpu(graphics)

    def ensure_texture(self, graphics) -> GPUTextureHandle:
        """Return the GPU texture handle, uploading / syncing if needed."""
        if self._handle is not None and not self._handle.is_valid():
            log.warning(
                f"[Font] Texture {self._handle.get_id()} became invalid; recreating"
            )
            self._handle = None

        if self._handle is None:
            data = self._atlas_array()
            self._handle = graphics.create_texture(
                data, _ATLAS_W, _ATLAS_H, channels=1, mipmap=False, clamp=True
            )
            self._dirty = False
        elif self._dirty:
            self._sync_gpu(graphics)

        return self._handle

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _rasterize(self, ch: str) -> bool:
        """Add *ch* to the atlas if not already present. Returns True if added."""
        if ch in self.glyphs:
            return False

        try:
            bbox = self._pil_font.getbbox(ch)
        except Exception:
            return False

        if bbox is None:
            return False

        gw = bbox[2] - bbox[0]
        gh = self._line_height

        if gw <= 0:
            # Zero-advance glyph (e.g. combining chars) — register but don't allocate space
            self.glyphs[ch] = {"uv": (0.0, 0.0, 0.0, 0.0), "size": (0, gh)}
            return True

        cell_w = gw + _PADDING
        cell_h = gh + _PADDING

        # Advance shelf if needed
        if self._shelf_x + cell_w > _ATLAS_W:
            self._shelf_y += self._shelf_h
            self._shelf_x = 0
            self._shelf_h = 0

        if self._shelf_y + cell_h > _ATLAS_H:
            log.warning(f"[Font] Atlas full — cannot rasterize U+{ord(ch):04X} '{ch}'")
            return False

        # Render glyph into a temporary image
        glyph_img = Image.new("L", (gw, gh), 0)
        ImageDraw.Draw(glyph_img).text((-bbox[0], 0), ch, fill=255, font=self._pil_font)
        self._atlas.paste(glyph_img, (self._shelf_x, self._shelf_y))
        self._atlas_np = None  # invalidate cached numpy view

        u0 = self._shelf_x / _ATLAS_W
        v0 = self._shelf_y / _ATLAS_H
        u1 = (self._shelf_x + gw) / _ATLAS_W
        v1 = (self._shelf_y + gh) / _ATLAS_H

        self.glyphs[ch] = {"uv": (u0, v0, u1, v1), "size": (gw, gh)}

        self._shelf_x += cell_w
        self._shelf_h = max(self._shelf_h, cell_h)
        self._dirty = True
        return True

    def _atlas_array(self) -> np.ndarray:
        """Return a contiguous uint8 numpy view of the CPU atlas."""
        if self._atlas_np is None:
            self._atlas_np = np.array(self._atlas, dtype=np.uint8)
        return self._atlas_np

    def _sync_gpu(self, graphics) -> None:
        """Push CPU atlas to GPU (full update)."""
        if self._handle is None:
            return
        data = self._atlas_array()
        graphics.update_texture(self._handle, data, _ATLAS_W, _ATLAS_H, 1)
        self._dirty = False
