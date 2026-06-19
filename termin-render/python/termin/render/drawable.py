"""
Drawable protocol — унифицированный интерфейс для рендеринга.

Позволяет ColorPass, ShadowPass, IdPass и другим пассам единообразно
работать с разными типами рендереров (MeshRenderer, LineRenderer, IconRenderer и т.д.).

Разделение ответственности:
- FramePass отвечает за привязку шейдера/материала
- Drawable отвечает за отрисовку геометрии

Использование:
    # ColorPass спрашивает фазы у Drawable, применяет их и рисует
    for drawable in drawables:
        for draw_call in drawable.get_geometry_draws(phase_mark):
            graphics.apply_render_state(draw_call.phase.state)
            material.apply(model, view, projection)
            drawable.draw_geometry(context, draw_call.geometry_id)

    # ShadowPass использует свой шейдер
    for drawable in drawables:
        if "shadow" in drawable.phase_marks:
            shadow_shader.use()
            # ... setup uniforms ...
            drawable.draw_geometry(context)

    # IdPass использует свой шейдер с pick_id
    for drawable in drawables:
        id_shader.use()
        id_shader.set_uniform("u_pick_color", encode_pick_id(entity.pick_id))
        drawable.draw_geometry(context)
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, List, Protocol, Set, runtime_checkable

if TYPE_CHECKING:
    from termin.materials import TcMaterialPhase
    from termin.render_framework import RenderContext


# Идентификатор геометрии по умолчанию
DEFAULT_GEOMETRY_ID = 0


@dataclass
class GeometryDrawCall:
    """
    Описание одного запроса на отрисовку геометрии.

    Связывает TcMaterialPhase с идентификатором геометрии.
    Позволяет Drawable рисовать разную геометрию с разными материалами
    в одной фазе рендеринга.

    Атрибуты:
        phase: Фаза материала (шейдер, render state, uniforms).
        geometry_id: Идентификатор геометрии для draw_geometry.
                     0 = основная геометрия.
    """
    phase: "TcMaterialPhase"
    geometry_id: int = DEFAULT_GEOMETRY_ID


@runtime_checkable
class Drawable(Protocol):
    """
    Протокол для компонентов, которые умеют рисовать геометрию.

    Атрибуты:
        phase_marks: Множество фаз, в которых участвует этот drawable.
                     Например: {"opaque", "shadow"}, {"editor"}, {"transparent"}
                     "shadow" означает, что объект отбрасывает тень.
                     Используется пассами для фильтрации.

    Методы:
        draw_geometry: Рисует геометрию (шейдер уже привязан пассом).
        get_geometry_draws: Возвращает GeometryDrawCalls для ColorPass.
    """

    phase_marks: Set[str]

    def draw_geometry(self, context: "RenderContext", geometry_id: int = DEFAULT_GEOMETRY_ID) -> None:
        """
        Рисует геометрию.

        Шейдер и материал уже привязаны пассом перед вызовом.
        Drawable должен просто отрисовать свою геометрию (mesh, lines, etc.)

        Параметры:
            context: Контекст рендеринга.
                     context.model содержит матрицу модели (для VAO binding).
            geometry_id: Идентификатор геометрии для отрисовки.
                         0 = основная/единственная геометрия.
        """
        ...

    def get_geometry_draws(self, phase_mark: str | None = None) -> List[GeometryDrawCall]:
        """
        Возвращает GeometryDrawCalls для этого drawable.

        Используется ColorPass для получения материалов и геометрий.
        ShadowPass и IdPass игнорируют этот метод и используют свои шейдеры.

        Параметры:
            phase_mark: Фильтр по метке фазы ("opaque", "transparent", "editor", etc.)
                        Если None, возвращает все фазы.

        Возвращает:
            Список GeometryDrawCall, отсортированный по priority.
        """
        ...
