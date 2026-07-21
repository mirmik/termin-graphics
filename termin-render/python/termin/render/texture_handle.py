"""Compatibility helpers for default TcTexture instances.

The historical ``TextureHandle`` wrapper stored Python TextureAsset objects.
Runtime texture APIs now use ``tgfx.TcTexture`` directly, backed by the C
``tc_texture`` pool.
"""

from __future__ import annotations

from tgfx import TcTexture


def get_white_texture_handle() -> TcTexture:
    """Return the canonical white 1x1 texture from the C registry."""
    return TcTexture.white_1x1()


def get_normal_texture_handle() -> TcTexture:
    """Return the canonical flat-normal 1x1 texture from the C registry."""
    return TcTexture.normal_1x1()


__all__ = ["get_white_texture_handle", "get_normal_texture_handle"]
