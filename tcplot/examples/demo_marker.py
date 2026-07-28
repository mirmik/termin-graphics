"""Interactive retained data-marker example.

Run: ./run-python.sh tcplot/examples/demo_marker.py

Drag the orange anchor to snap it to the nearest curve sample. Click the
callout's close button to destroy the semantic annotation.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import numpy as np

from tcplot import Plot2D, PlotDataMarker2D


def make_plot():
    plot = Plot2D()
    x = np.linspace(0.0, 4.0 * np.pi, 800)
    y = np.sin(x)
    plot.plot(x, y, thickness=2.0, label="sin(x)")
    plot.set_view(0.0, 4.0 * np.pi, -1.25, 1.25)
    plot.data.title = "Retained data marker"
    plot.data.x_label = "x"
    plot.data.y_label = "sin(x)"

    initial_index = int(np.searchsorted(x, 2.0 * np.pi))
    marker = PlotDataMarker2D()
    marker.x = float(x[initial_index])
    marker.y = float(y[initial_index])
    marker.text = "Drag the anchor • close with ×"
    handle = plot.create_data_marker(marker)
    if not handle:
        raise RuntimeError("tcplot rejected the example marker")

    def snap_to_curve(candidate_x, _candidate_y):
        index = int(np.abs(x - candidate_x).argmin())
        return float(x[index]), float(y[index])

    def report_action(_annotation, action):
        print(f"marker action: {action}")

    if not plot.set_marker_snap_handler(handle, snap_to_curve):
        raise RuntimeError("failed to install marker snap handler")
    if not plot.set_marker_action_handler(handle, report_action):
        raise RuntimeError("failed to install marker action handler")
    return plot


if __name__ == "__main__":
    from _host import run_demo

    run_demo("tcplot — Retained Marker Demo", make_plot)
