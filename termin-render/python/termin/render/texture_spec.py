# termin/loaders/texture_spec.py
"""Texture import specification - settings for loading texture files."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from tcbase import log


@dataclass
class TextureSpec:
    """
    Import settings for texture files.

    Stored as .meta file next to the texture (e.g., image.png.meta).
    """

    # Flip texture horizontally (mirror X)
    flip_x: bool = False
    # Flip texture vertically for OpenGL (origin at bottom-left)
    flip_y: bool = True
    # Transpose texture (swap X and Y axes)
    transpose: bool = False

    @classmethod
    def load(cls, spec_path: str | Path) -> "TextureSpec":
        """Load spec from file."""
        path = Path(spec_path)
        if not path.exists():
            return cls()

        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            return cls(
                flip_x=data.get("flip_x", False),
                flip_y=data.get("flip_y", True),
                transpose=data.get("transpose", False),
            )
        except Exception:
            log.warning(f"[TextureSpec] Failed to load spec from {spec_path}", exc_info=True)
            return cls()

    @classmethod
    def for_texture_file(cls, texture_path: str | Path) -> "TextureSpec":
        """Load spec for a texture file (looks for texture_path.meta or .spec)."""
        # Try .meta first (new format)
        meta_path = Path(str(texture_path) + ".meta")
        if meta_path.exists():
            return cls.load(meta_path)
        # Fallback to .spec (old format)
        spec_path = Path(str(texture_path) + ".spec")
        return cls.load(spec_path)

    def save(self, spec_path: str | Path, preserve_existing: bool = False) -> None:
        """Save spec to file.

        Args:
            spec_path: Path to save the spec file.
            preserve_existing: If True, preserve fields from existing file (like uuid).
        """
        path = Path(spec_path)

        # Read existing data to preserve fields like uuid
        existing_data = {}
        if preserve_existing and path.exists():
            try:
                with open(path, "r", encoding="utf-8") as f:
                    existing_data = json.load(f)
            except Exception:
                log.warning(f"[TextureSpec] Failed to read existing spec {path}", exc_info=True)

        # Update with our fields
        data = existing_data.copy()
        data.update({
            "flip_x": self.flip_x,
            "flip_y": self.flip_y,
            "transpose": self.transpose,
        })
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

    def save_for_texture(self, texture_path: str | Path) -> None:
        """Save spec next to texture file (.meta format).

        Preserves existing fields like uuid from the .meta file.
        """
        import os
        meta_path = Path(str(texture_path) + ".meta")
        self.save(meta_path, preserve_existing=True)
        # Remove old .spec if exists (migration)
        old_spec = Path(str(texture_path) + ".spec")
        if old_spec.exists():
            try:
                os.remove(old_spec)
            except Exception:
                log.warning(f"[TextureSpec] Failed to remove old spec {old_spec}", exc_info=True)
