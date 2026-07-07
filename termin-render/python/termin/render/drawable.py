"""
Drawable protocol — legacy Python-facing rendering notes.

Runtime passes collect RenderItems through the native drawable protocol.
The old Python draw_geometry/get_geometry_draws shape is retained only as
documentation for legacy Python components that have not been migrated yet.

Разделение ответственности:
- FramePass отвечает за привязку шейдера/материала
- Drawable/renderer submits RenderItems
- RenderItem encoders own backend-specific draw details
"""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Protocol, Set, runtime_checkable

from termin.render._render_native import GeometryDrawCall

if TYPE_CHECKING:
    from termin.render_framework import RenderContext


# Идентификатор геометрии по умолчанию
DEFAULT_GEOMETRY_ID = 0


@runtime_checkable
class Drawable(Protocol):
    """
    Legacy protocol for components that expose geometry.

    New render paths should submit RenderItems through the native drawable
    protocol instead of driving draw_geometry from passes.

    Атрибуты:
        phase_marks: Множество фаз, в которых участвует этот drawable.
                     Например: {"opaque", "shadow"}, {"editor"}, {"transparent"}
                     "shadow" означает, что объект отбрасывает тень.
                     Используется пассами для фильтрации.

    Методы:
        draw_geometry: Legacy direct geometry draw hook.
        get_geometry_draws: Legacy material/geometry discovery hook.
    """

    phase_marks: Set[str]

    def draw_geometry(self, context: "RenderContext", _geometry_id: int = DEFAULT_GEOMETRY_ID) -> None:
        """
        Рисует геометрию.

        Legacy hook. Runtime passes should not call this through the C drawable
        protocol; renderers should submit RenderItems instead.

        Параметры:
            context: Контекст рендеринга.
                     context.model содержит матрицу модели (для VAO binding).
            geometry_id: Идентификатор геометрии для отрисовки.
                         0 = основная/единственная геометрия.
        """
        ...

    def get_geometry_draws(
        self,
        context: "RenderContext",
        phase_mark: str | None = None,
    ) -> List[GeometryDrawCall]:
        """
        Возвращает GeometryDrawCalls для этого drawable.

        Legacy hook for material/geometry discovery. Runtime passes should
        collect RenderItems instead.

        Параметры:
            context: Контекст рендеринга.
            phase_mark: Фильтр по метке фазы ("opaque", "transparent", "editor", etc.)
                        Если None, возвращает все фазы.

        Возвращает:
            Список GeometryDrawCall, отсортированный по priority.
        """
        ...
