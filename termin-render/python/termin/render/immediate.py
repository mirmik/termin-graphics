"""Immediate debug rendering helpers."""

from __future__ import annotations

from tgfx import ImmediateRenderer as _ImmediateRenderer


class ImmediateRenderer(_ImmediateRenderer):
    """Shared immediate-mode renderer used by debug and gizmo passes."""

    _instance: "ImmediateRenderer | None" = None

    def __init__(self):
        super().__init__()
        ImmediateRenderer._instance = self

    @classmethod
    def instance(cls) -> "ImmediateRenderer":
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance


__all__ = ["ImmediateRenderer"]
