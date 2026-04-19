"""3D surface plot demo.

Run: python3 examples/demo_3d_surface.py
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np

from tcplot import Plot3D
from _host import run_demo


def make_plot():
    plot = Plot3D()
    plot.data.title = "z = sin(r) / r"

    N = 80
    u = np.linspace(-10, 10, N)
    v = np.linspace(-10, 10, N)
    X, Y = np.meshgrid(u, v)

    R = np.sqrt(X**2 + Y**2) + 1e-6
    Z = np.sin(R) / R

    plot.z_scale = 5.0
    plot.surface(X, Y, Z, color=(0.12, 0.56, 0.85, 1.0))
    plot.surface(X, Y, Z, color=(0.0, 0.0, 0.0, 1.0), wireframe=True)
    return plot


if __name__ == "__main__":
    run_demo("tcplot — 3D Surface", make_plot, size=(900, 700))
