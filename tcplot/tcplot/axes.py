"""Axis ticks and labels — shared logic for 2D and 3D."""

from __future__ import annotations

import math


def nice_ticks(lo: float, hi: float, max_ticks: int = 10) -> list[float]:
    """Generate human-friendly tick positions for range [lo, hi].

    Returns a list of tick values that are "nice" numbers (1, 2, 5 multiples).
    Works for any scale — suitable for both 2D axes and 3D grids.
    """
    if hi <= lo:
        return [lo]

    span = hi - lo
    rough_step = span / max(max_ticks - 1, 1)

    # Round step to a nice number: 1, 2, 5 * 10^n
    exp = math.floor(math.log10(rough_step))
    base = 10 ** exp
    normalized = rough_step / base
    if normalized <= 1.0:
        nice_step = base
    elif normalized <= 2.0:
        nice_step = 2 * base
    elif normalized <= 5.0:
        nice_step = 5 * base
    else:
        nice_step = 10 * base

    start = math.floor(lo / nice_step) * nice_step
    ticks = []
    v = start
    while v <= hi + nice_step * 0.001:
        if v >= lo - nice_step * 0.001:
            ticks.append(round(v, 12))  # avoid floating point dust
        v += nice_step

    return ticks


def format_tick(value: float) -> str:
    """Format tick value for display."""
    if value == 0:
        return "0"
    abs_val = abs(value)
    if abs_val >= 1e6 or abs_val < 1e-3:
        return f"{value:.2e}"
    # Remove trailing zeros
    s = f"{value:.6g}"
    return s
