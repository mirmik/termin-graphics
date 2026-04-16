"""World-space 3D billboard text rendering - re-export of the C++
Text3DRenderer.

The renderer lives in C++ (termin-graphics/src/tgfx2/text3d_renderer.cpp,
bound in _tgfx_native). This shim stays so existing ``from tgfx.text3d
import Text3DRenderer`` imports keep working, and to paper over the
slightly different ``begin`` signature: the C++ class takes flat
``mvp`` / ``cam_right`` / ``cam_up`` arrays directly, while legacy
Python callers pass a camera object that exposes ``mvp(aspect)`` and
``view_matrix()``. ``_CameraAdapter`` does the translation.

New code should import ``Text3DRenderer`` from ``tgfx._tgfx_native``
and pass the numpy arrays directly.
"""
from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

from tgfx._tgfx_native import Text3DRenderer as _NativeText3DRenderer

if TYPE_CHECKING:
    from tgfx.font import FontTextureAtlas
    from tgfx._tgfx_native import Tgfx2Context


__all__ = ["Text3DRenderer"]


class Text3DRenderer(_NativeText3DRenderer):
    """Thin subclass adding legacy ``begin(holder, camera, aspect)`` support.

    The C++ base class's ``begin`` signature is
    ``begin(ctx, mvp, cam_right, cam_up, font=None)`` — caller must
    supply the camera basis directly. This subclass adds a Python
    overload that accepts the pre-migration ``camera`` object (which
    exposes ``mvp(aspect)`` and ``view_matrix()``), extracts the three
    arrays, and forwards.
    """

    def begin(  # type: ignore[override]
        self,
        holder,
        camera_or_mvp=None,
        aspect_or_cam_right=None,
        cam_up=None,
        *,
        font: "FontTextureAtlas | None" = None,
        mvp_override=None,
    ) -> None:
        # Shape-1: native form — (ctx, mvp, cam_right, cam_up).
        if cam_up is not None:
            ctx = holder.context if hasattr(holder, "context") else holder
            mvp = np.ascontiguousarray(camera_or_mvp, dtype=np.float32).reshape(-1)
            cr = np.ascontiguousarray(aspect_or_cam_right, dtype=np.float32).reshape(-1)
            cu = np.ascontiguousarray(cam_up, dtype=np.float32).reshape(-1)
            _NativeText3DRenderer.begin(self, ctx, mvp, cr, cu, font)
            return

        # Shape-2: legacy Python form — (holder, camera, aspect).
        camera = camera_or_mvp
        aspect = aspect_or_cam_right
        if camera is None or aspect is None:
            raise TypeError(
                "Text3DRenderer.begin: need either (ctx, mvp, cam_right, cam_up) "
                "or (holder, camera, aspect)"
            )

        ctx = holder.context if hasattr(holder, "context") else holder

        mvp = mvp_override if mvp_override is not None else camera.mvp(aspect)
        mvp = np.ascontiguousarray(mvp, dtype=np.float32).reshape(-1)

        view = camera.view_matrix()
        cam_right = np.ascontiguousarray(
            [float(view[0, 0]), float(view[0, 1]), float(view[0, 2])],
            dtype=np.float32,
        )
        cam_up_vec = np.ascontiguousarray(
            [float(view[1, 0]), float(view[1, 1]), float(view[1, 2])],
            dtype=np.float32,
        )

        _NativeText3DRenderer.begin(self, ctx, mvp, cam_right, cam_up_vec, font)

    def draw(  # type: ignore[override]
        self,
        text: str,
        position,
        *,
        color=(1.0, 1.0, 1.0, 1.0),
        size: float = 0.05,
        anchor: str = "center",
    ) -> None:
        # Accept list/tuple/ndarray for position; C++ bind takes 3-tuple.
        p = (float(position[0]), float(position[1]), float(position[2]))
        c = (float(color[0]), float(color[1]), float(color[2]), float(color[3]))
        _NativeText3DRenderer.draw(
            self, text, p, color=c, size=size, anchor=anchor,
        )
