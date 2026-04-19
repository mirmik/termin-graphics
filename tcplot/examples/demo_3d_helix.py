"""3D helix plot demo.

Run: python3 examples/demo_3d_helix.py
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
    plot.data.title = "Double Helix"

    t = np.linspace(0, 6 * np.pi, 500)
    x1 = np.cos(t)
    y1 = np.sin(t)
    z1 = t / (2 * np.pi)
    plot.plot(x1, y1, z1, color=(0.12, 0.47, 0.71, 1.0), label="Helix 1")

    x2 = np.cos(t + np.pi)
    y2 = np.sin(t + np.pi)
    plot.plot(x2, y2, z1, color=(1.0, 0.50, 0.05, 1.0), label="Helix 2")

    rng = np.random.default_rng(42)
    n = 200
    r = rng.uniform(0, 0.3, n)
    theta = rng.uniform(0, 2 * np.pi, n)
    sx = r * np.cos(theta)
    sy = r * np.sin(theta)
    sz = rng.uniform(0, 3, n)
    plot.scatter(sx, sy, sz, color=(0.17, 0.63, 0.17, 1.0), label="Points")
    return plot


if __name__ == "__main__":
    run_demo("tcplot — 3D Helix", make_plot, size=(900, 700))
