"""Scatter plot demo with random data.

Run: python3 examples/demo_scatter.py
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

    rng = np.random.default_rng(42)
    for i, (cx, cy) in enumerate([(2, 3), (5, 1), (8, 4)]):
        n = 100
        x = rng.normal(cx, 0.8, n)
        y = rng.normal(cy, 0.6, n)
        plot.scatter(x, y, label=f"Cluster {i+1}", size=5.0)

    x_trend = np.linspace(0, 10, 100)
    y_trend = 0.3 * x_trend + 1.5
    plot.plot(x_trend, y_trend,
              color=(1.0, 1.0, 1.0, 0.4),
              thickness=1.0, label="Trend")

    plot.data.title = "Scatter Plot with Clusters"
    plot.data.x_label = "Feature A"
    plot.data.y_label = "Feature B"
    return plot


if __name__ == "__main__":
    run_demo("tcplot — Scatter Demo", make_plot)
