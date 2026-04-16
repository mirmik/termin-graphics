"""Screen-space 2D text rendering - re-export of the C++ Text2DRenderer.

The renderer itself lives in C++ (termin-graphics/src/tgfx2/
text2d_renderer.cpp, bound in _tgfx_native). This module is kept only
so existing ``from tgfx.text2d import Text2DRenderer`` imports keep
working; new code may import directly from ``tgfx._tgfx_native``.
"""
from __future__ import annotations

from tgfx._tgfx_native import Text2DRenderer

__all__ = ["Text2DRenderer"]
