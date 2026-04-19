"""Multi-plot demo showing polynomials and damped oscillations side-by-side.

Run: python3 examples/demo_multi.py
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np

from tcgui.widgets.hstack import HStack

from tcplot import Plot2D
from _host import run_demo


def make_row():
    row = HStack()
    row.spacing = 10

    p1 = Plot2D()
    p1.stretch = True
    x = np.linspace(-2, 2, 200)
    p1.plot(x, x**2, label="x^2")
    p1.plot(x, x**3, label="x^3")
    p1.plot(x, x**4 - 2*x**2, label="x^4 - 2x^2")
    p1.data.title = "Polynomials"

    p2 = Plot2D()
    p2.stretch = True
    t = np.linspace(0, 10, 500)
    for zeta in [0.1, 0.3, 0.7, 1.0]:
        y = np.exp(-zeta * t) * np.cos(t * np.sqrt(max(1 - zeta**2, 0)))
        p2.plot(t, y, label=f"zeta={zeta}")
    p2.data.title = "Damped Oscillations"
    p2.data.x_label = "t"

    row.add_child(p1)
    row.add_child(p2)
    return row


if __name__ == "__main__":
    run_demo("tcplot — Multi Plot", make_row, size=(1200, 500))
