"""
Drawable protocol.

Runtime passes collect RenderItems through the native drawable protocol.

Разделение ответственности:
- FramePass отвечает за привязку шейдера/материала
- Drawable/renderer submits RenderItems
- RenderItem encoders own backend-specific draw details
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol, Set, runtime_checkable

from termin.render._render_native import (
    RENDER_ITEM_COLLECT_ALLOW_MISSING_MATERIAL_PHASE,
    RenderItem,
)


# Идентификатор геометрии по умолчанию
DEFAULT_GEOMETRY_ID = 0


@dataclass(frozen=True)
class RenderItemCollectContext:
    phase_mark: str = ""
    flags: int = 0
    layer_mask: int = (1 << 64) - 1
    render_category_mask: int = (1 << 64) - 1
    debug_pass_name: str = ""

    @property
    def allow_missing_material_phase(self) -> bool:
        return (self.flags & RENDER_ITEM_COLLECT_ALLOW_MISSING_MATERIAL_PHASE) != 0


@runtime_checkable
class Drawable(Protocol):
    """
    Protocol for components that submit renderable work through RenderItems.

    Атрибуты:
        phase_marks: Множество фаз, в которых участвует этот drawable.
                     Например: {"opaque", "shadow"}, {"editor"}, {"transparent"}
                     "shadow" означает, что объект отбрасывает тень.
                     Используется пассами для фильтрации.

    Методы:
        collect_render_items: Возвращает RenderItems для указанного pass context.
                              Пустой phase_mark означает pass-neutral snapshot
                              и требует вернуть все phase-варианты за один вызов.
    """

    phase_marks: Set[str]

    def collect_render_items(self, context: RenderItemCollectContext) -> list[RenderItem]:
        """
        Возвращает RenderItems для текущей фазы pass-а или все варианты для
        snapshot collection при пустом phase_mark.
        """
        ...


__all__ = [
    "DEFAULT_GEOMETRY_ID",
    "Drawable",
    "RenderItem",
    "RenderItemCollectContext",
]
