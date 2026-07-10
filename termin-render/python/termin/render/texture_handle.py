"""Compatibility helpers for default TcTexture instances.

The historical ``TextureHandle`` wrapper stored Python TextureAsset objects.
Runtime texture APIs now use ``tgfx.TcTexture`` directly, backed by the C
``tc_texture`` pool.
"""

from __future__ import annotations

import numpy as np
from tgfx import TcTexture

# Singleton for white texture handle
_white_texture_handle: TcTexture | None = None

# Singleton for normal texture handle
_normal_texture_handle: TcTexture | None = None


def get_white_texture_handle() -> TcTexture:
    """Return the white 1x1 texture singleton."""
    global _white_texture_handle
    if _white_texture_handle is None or not _white_texture_handle.is_valid:
        _white_texture_handle = TcTexture.white_1x1()
    return _white_texture_handle


def get_normal_texture_handle() -> TcTexture:
    """Return the flat normal 1x1 texture singleton."""
    global _normal_texture_handle
    if _normal_texture_handle is None or not _normal_texture_handle.is_valid:
        data = np.array([[[128, 128, 255, 255]]], dtype=np.uint8)
        _normal_texture_handle = TcTexture.from_data(
            data=data,
            width=1,
            height=1,
            channels=4,
            flip_x=False,
            flip_y=False,
            transpose=False,
            name="__normal_1x1__",
            source_path="",
            uuid="__normal_1x1__",
        )
    return _normal_texture_handle


__all__ = ["get_white_texture_handle", "get_normal_texture_handle"]
