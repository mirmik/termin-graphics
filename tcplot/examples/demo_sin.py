"""Basic sine/cosine plot demo.

Run: python3 examples/demo_sin.py          (OpenGL, default)
     TERMIN_BACKEND=vulkan python3 ...     (Vulkan)
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np

from tcplot import Plot2D
from _host import run_demo


def make_plot():
    plot = Plot2D()
    x = np.linspace(0, 4 * np.pi, 500)
    plot.plot(x, np.sin(x), label="sin(x)")
    plot.plot(x, np.cos(x), label="cos(x)")
    plot.plot(x, np.sin(x) * np.exp(-x / 10),
              label="sin(x) * e^(-x/10)", thickness=2.0)
    plot.data.title = "Trigonometric Functions"
    plot.data.x_label = "x"
    plot.data.y_label = "y"
    return plot


if __name__ == "__main__":
    run_demo("tcplot — Sine Demo", make_plot)
