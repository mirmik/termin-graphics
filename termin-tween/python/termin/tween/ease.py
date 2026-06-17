"""Функции сглаживания (easing) для твининга.

Функции сглаживания определяют, как значение изменяется от начального к конечному.
Все функции принимают t в диапазоне [0, 1] и возвращают значение в том же диапазоне
(за исключением Back и Elastic, которые могут выходить за пределы).

Терминология:
- IN (вход): медленное начало, ускорение к концу
- OUT (выход): быстрое начало, замедление к концу
- IN_OUT: медленное начало и конец, быстрая середина
"""

from __future__ import annotations

import math
from enum import Enum, auto
from typing import Callable


class Ease(Enum):
    """
    Типы функций сглаживания.

    Степенные функции (чем выше степень, тем резче переход):
    - QUAD: квадратичная (t²) — мягкое сглаживание
    - CUBIC: кубическая (t³) — умеренное сглаживание
    - QUART: четвёртая степень (t⁴) — заметное сглаживание
    - QUINT: пятая степень (t⁵) — сильное сглаживание

    Тригонометрические:
    - SINE: синусоидальная — самое мягкое, естественное сглаживание

    Экспоненциальные:
    - EXPO: экспоненциальная — очень резкий переход
    - CIRC: круговая (по дуге окружности) — резкий, но плавный

    Специальные эффекты:
    - BACK: отскок назад перед/после движения (как оттягивание рогатки)
    - ELASTIC: пружинные колебания (как резинка)
    - BOUNCE: отскоки (как мячик)
    """

    # Линейная интерполяция (без сглаживания)
    LINEAR = auto()

    # Квадратичная (t²) — мягкое сглаживание
    IN_QUAD = auto()      # Медленный старт
    OUT_QUAD = auto()     # Медленный финиш
    IN_OUT_QUAD = auto()  # Медленный старт и финиш

    # Кубическая (t³) — умеренное сглаживание
    IN_CUBIC = auto()
    OUT_CUBIC = auto()
    IN_OUT_CUBIC = auto()

    # Четвёртая степень (t⁴) — заметное сглаживание
    IN_QUART = auto()
    OUT_QUART = auto()
    IN_OUT_QUART = auto()

    # Пятая степень (t⁵) — сильное сглаживание
    IN_QUINT = auto()
    OUT_QUINT = auto()
    IN_OUT_QUINT = auto()

    # Синусоидальная — самое мягкое, естественное
    IN_SINE = auto()
    OUT_SINE = auto()
    IN_OUT_SINE = auto()

    # Экспоненциальная — очень резкий переход
    IN_EXPO = auto()
    OUT_EXPO = auto()
    IN_OUT_EXPO = auto()

    # Круговая — резкий, но плавный (по дуге)
    IN_CIRC = auto()
    OUT_CIRC = auto()
    IN_OUT_CIRC = auto()

    # Отскок назад — выходит за пределы [0,1] перед/после
    IN_BACK = auto()      # Отходит назад перед движением
    OUT_BACK = auto()     # Проскакивает цель и возвращается
    IN_OUT_BACK = auto()  # Оба эффекта

    # Пружина — колебания вокруг цели
    IN_ELASTIC = auto()      # Колебания в начале
    OUT_ELASTIC = auto()     # Колебания в конце (пружинный эффект)
    IN_OUT_ELASTIC = auto()  # Колебания с обоих сторон

    # Отскоки — как падающий мячик
    IN_BOUNCE = auto()      # Отскоки в начале
    OUT_BOUNCE = auto()     # Отскоки в конце (мячик)
    IN_OUT_BOUNCE = auto()  # Отскоки с обоих сторон


# ============================================================================
# Реализации функций сглаживания
# ============================================================================


def linear(t: float) -> float:
    """Линейная: равномерное движение без ускорения."""
    return t


# --- Квадратичные (Quad) ---

def in_quad(t: float) -> float:
    """Квадратичный вход: медленный старт, t²."""
    return t * t


def out_quad(t: float) -> float:
    """Квадратичный выход: медленный финиш."""
    return 1 - (1 - t) * (1 - t)


def in_out_quad(t: float) -> float:
    """Квадратичный вход-выход: медленные старт и финиш."""
    return 2 * t * t if t < 0.5 else 1 - (-2 * t + 2) ** 2 / 2


# --- Кубические (Cubic) ---

def in_cubic(t: float) -> float:
    """Кубический вход: t³."""
    return t * t * t


def out_cubic(t: float) -> float:
    """Кубический выход."""
    return 1 - (1 - t) ** 3


def in_out_cubic(t: float) -> float:
    """Кубический вход-выход."""
    return 4 * t * t * t if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2


# --- Четвёртая степень (Quart) ---

def in_quart(t: float) -> float:
    """Четвёртая степень вход: t⁴."""
    return t ** 4


def out_quart(t: float) -> float:
    """Четвёртая степень выход."""
    return 1 - (1 - t) ** 4


def in_out_quart(t: float) -> float:
    """Четвёртая степень вход-выход."""
    return 8 * t ** 4 if t < 0.5 else 1 - (-2 * t + 2) ** 4 / 2


# --- Пятая степень (Quint) ---

def in_quint(t: float) -> float:
    """Пятая степень вход: t⁵."""
    return t ** 5


def out_quint(t: float) -> float:
    """Пятая степень выход."""
    return 1 - (1 - t) ** 5


def in_out_quint(t: float) -> float:
    """Пятая степень вход-выход."""
    return 16 * t ** 5 if t < 0.5 else 1 - (-2 * t + 2) ** 5 / 2


# --- Синусоидальные (Sine) ---

def in_sine(t: float) -> float:
    """Синусоидальный вход: самое мягкое сглаживание."""
    return 1 - math.cos(t * math.pi / 2)


def out_sine(t: float) -> float:
    """Синусоидальный выход."""
    return math.sin(t * math.pi / 2)


def in_out_sine(t: float) -> float:
    """Синусоидальный вход-выход."""
    return -(math.cos(math.pi * t) - 1) / 2


# --- Экспоненциальные (Expo) ---

def in_expo(t: float) -> float:
    """Экспоненциальный вход: очень медленный старт, резкое ускорение."""
    return 0 if t == 0 else 2 ** (10 * t - 10)


def out_expo(t: float) -> float:
    """Экспоненциальный выход: резкий старт, очень медленный финиш."""
    return 1 if t == 1 else 1 - 2 ** (-10 * t)


def in_out_expo(t: float) -> float:
    """Экспоненциальный вход-выход."""
    if t == 0:
        return 0
    if t == 1:
        return 1
    if t < 0.5:
        return 2 ** (20 * t - 10) / 2
    return (2 - 2 ** (-20 * t + 10)) / 2


# --- Круговые (Circ) ---

def in_circ(t: float) -> float:
    """Круговой вход: движение по дуге окружности."""
    return 1 - math.sqrt(1 - t * t)


def out_circ(t: float) -> float:
    """Круговой выход."""
    return math.sqrt(1 - (t - 1) ** 2)


def in_out_circ(t: float) -> float:
    """Круговой вход-выход."""
    if t < 0.5:
        return (1 - math.sqrt(1 - (2 * t) ** 2)) / 2
    return (math.sqrt(1 - (-2 * t + 2) ** 2) + 1) / 2


# --- Отскок назад (Back) ---

def in_back(t: float) -> float:
    """Отскок назад на входе: отходит назад перед движением вперёд."""
    c1 = 1.70158
    c3 = c1 + 1
    return c3 * t * t * t - c1 * t * t


def out_back(t: float) -> float:
    """Отскок назад на выходе: проскакивает цель и возвращается."""
    c1 = 1.70158
    c3 = c1 + 1
    return 1 + c3 * (t - 1) ** 3 + c1 * (t - 1) ** 2


def in_out_back(t: float) -> float:
    """Отскок назад на входе и выходе."""
    c1 = 1.70158
    c2 = c1 * 1.525
    if t < 0.5:
        return ((2 * t) ** 2 * ((c2 + 1) * 2 * t - c2)) / 2
    return ((2 * t - 2) ** 2 * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2


# --- Пружина (Elastic) ---

def in_elastic(t: float) -> float:
    """Пружина на входе: колебания в начале движения."""
    if t == 0:
        return 0
    if t == 1:
        return 1
    c4 = (2 * math.pi) / 3
    return -(2 ** (10 * t - 10)) * math.sin((t * 10 - 10.75) * c4)


def out_elastic(t: float) -> float:
    """Пружина на выходе: колебания вокруг конечной точки."""
    if t == 0:
        return 0
    if t == 1:
        return 1
    c4 = (2 * math.pi) / 3
    return 2 ** (-10 * t) * math.sin((t * 10 - 0.75) * c4) + 1


def in_out_elastic(t: float) -> float:
    """Пружина на входе и выходе."""
    if t == 0:
        return 0
    if t == 1:
        return 1
    c5 = (2 * math.pi) / 4.5
    if t < 0.5:
        return -(2 ** (20 * t - 10) * math.sin((20 * t - 11.125) * c5)) / 2
    return (2 ** (-20 * t + 10) * math.sin((20 * t - 11.125) * c5)) / 2 + 1


# --- Отскоки (Bounce) ---

def out_bounce(t: float) -> float:
    """Отскоки на выходе: как падающий мячик."""
    n1 = 7.5625
    d1 = 2.75
    if t < 1 / d1:
        return n1 * t * t
    elif t < 2 / d1:
        t -= 1.5 / d1
        return n1 * t * t + 0.75
    elif t < 2.5 / d1:
        t -= 2.25 / d1
        return n1 * t * t + 0.9375
    else:
        t -= 2.625 / d1
        return n1 * t * t + 0.984375


def in_bounce(t: float) -> float:
    """Отскоки на входе."""
    return 1 - out_bounce(1 - t)


def in_out_bounce(t: float) -> float:
    """Отскоки на входе и выходе."""
    if t < 0.5:
        return (1 - out_bounce(1 - 2 * t)) / 2
    return (1 + out_bounce(2 * t - 1)) / 2


# ============================================================================
# Маппинг Ease enum -> функция
# ============================================================================

_EASE_FUNCTIONS: dict[Ease, Callable[[float], float]] = {
    Ease.LINEAR: linear,
    Ease.IN_QUAD: in_quad,
    Ease.OUT_QUAD: out_quad,
    Ease.IN_OUT_QUAD: in_out_quad,
    Ease.IN_CUBIC: in_cubic,
    Ease.OUT_CUBIC: out_cubic,
    Ease.IN_OUT_CUBIC: in_out_cubic,
    Ease.IN_QUART: in_quart,
    Ease.OUT_QUART: out_quart,
    Ease.IN_OUT_QUART: in_out_quart,
    Ease.IN_QUINT: in_quint,
    Ease.OUT_QUINT: out_quint,
    Ease.IN_OUT_QUINT: in_out_quint,
    Ease.IN_SINE: in_sine,
    Ease.OUT_SINE: out_sine,
    Ease.IN_OUT_SINE: in_out_sine,
    Ease.IN_EXPO: in_expo,
    Ease.OUT_EXPO: out_expo,
    Ease.IN_OUT_EXPO: in_out_expo,
    Ease.IN_CIRC: in_circ,
    Ease.OUT_CIRC: out_circ,
    Ease.IN_OUT_CIRC: in_out_circ,
    Ease.IN_BACK: in_back,
    Ease.OUT_BACK: out_back,
    Ease.IN_OUT_BACK: in_out_back,
    Ease.IN_ELASTIC: in_elastic,
    Ease.OUT_ELASTIC: out_elastic,
    Ease.IN_OUT_ELASTIC: in_out_elastic,
    Ease.IN_BOUNCE: in_bounce,
    Ease.OUT_BOUNCE: out_bounce,
    Ease.IN_OUT_BOUNCE: in_out_bounce,
}


def evaluate(ease: Ease, t: float) -> float:
    """Вычислить значение функции сглаживания в момент времени t (0..1)."""
    return _EASE_FUNCTIONS[ease](t)
