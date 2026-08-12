"""Reusable builders for the restored tcplot example gallery."""

from __future__ import annotations

import numpy as np

from tcplot import SrgbColor, SurfaceColorMap
from tcplot_gui_native import Plot2D, Plot3D
from termin.gui_native import Size


def _add(parent, child, *, size: tuple[float, float] | None = None) -> None:
    if size is not None:
        child.widget.preferred_size = Size(*size)
    if not parent.widget.append_child(child.widget):
        raise RuntimeError("failed to append tcplot gallery widget")


def _page(document, heading: str, plot, legend: tuple[str, ...]):
    root = document.create_vstack("tcplot-gallery-page")
    root.widget.preferred_size = Size(1120.0, 680.0)
    _add(root, document.create_label(heading))
    _add(root, plot, size=(1120.0, 600.0 if not legend else 560.0))
    if legend:
        _add(root, document.create_label("  •  ".join(legend)))
    return root


def sine_plot(document):
    plot = Plot2D(document)
    x = np.linspace(0, 4 * np.pi, 500)
    plot.plot(x, np.sin(x))
    plot.plot(x, np.cos(x))
    plot.plot(x, np.sin(x) * np.exp(-x / 10), thickness=2.0)
    plot.set_title("Trigonometric Functions")
    plot.set_axis_labels("x", "y")
    return _page(
        document,
        "Sine, cosine and damped sine",
        plot,
        ("sin(x)", "cos(x)", "sin(x) · exp(-x/10)"),
    )


def scatter_plot(document):
    plot = Plot2D(document)
    rng = np.random.default_rng(42)
    for cx, cy in ((2, 3), (5, 1), (8, 4)):
        plot.scatter(rng.normal(cx, 0.8, 100), rng.normal(cy, 0.6, 100), size=5.0)
    x_trend = np.linspace(0, 10, 100)
    plot.plot(
        x_trend,
        0.3 * x_trend + 1.5,
        color=SrgbColor(1.0, 1.0, 1.0, 0.4),
        thickness=1.0,
    )
    plot.set_title("Scatter Plot with Clusters")
    plot.set_axis_labels("Feature A", "Feature B")
    return _page(
        document,
        "Three deterministic clusters and their trend",
        plot,
        ("Cluster 1", "Cluster 2", "Cluster 3", "Trend"),
    )


def multi_plot(document):
    row = document.create_hstack("tcplot-multi-row")
    row.widget.preferred_size = Size(1200.0, 660.0)

    left = document.create_vstack("tcplot-polynomials")
    p1 = Plot2D(document)
    x = np.linspace(-2, 2, 200)
    p1.plot(x, x**2)
    p1.plot(x, x**3)
    p1.plot(x, x**4 - 2 * x**2)
    p1.set_title("Polynomials")
    _add(left, p1, size=(590.0, 580.0))
    _add(left, document.create_label("x²  •  x³  •  x⁴ − 2x²"))

    right = document.create_vstack("tcplot-damped")
    p2 = Plot2D(document)
    t = np.linspace(0, 10, 500)
    for zeta in (0.1, 0.3, 0.7, 1.0):
        p2.plot(t, np.exp(-zeta * t) * np.cos(t * np.sqrt(max(1 - zeta**2, 0))))
    p2.set_title("Damped Oscillations")
    p2.set_axis_labels("t", "response")
    _add(right, p2, size=(590.0, 580.0))
    _add(right, document.create_label("ζ = 0.1  •  0.3  •  0.7  •  1.0"))

    _add(row, left, size=(600.0, 650.0))
    _add(row, right, size=(600.0, 650.0))
    return row


def marker_plot(document):
    plot = Plot2D(document)
    x = np.linspace(0.0, 4.0 * np.pi, 800)
    y = np.sin(x)
    line = plot.plot(x, y, thickness=2.0)
    plot.set_view(0.0, 4.0 * np.pi, -1.25, 1.25)
    plot.set_title("Retained data marker")
    plot.set_axis_labels("x", "sin(x)")
    initial = int(np.searchsorted(x, 2.0 * np.pi))
    plot.create_data_marker(
        float(x[initial]),
        float(y[initial]),
        "Drag the anchor · close with ×",
        snap_line_index=line,
    )
    return _page(
        document,
        "Interactive retained annotation",
        plot,
        ("drag anchor", "nearest-sample snap", "semantic close action"),
    )


def helix_plot(document):
    plot = Plot3D(document)
    t = np.linspace(0, 6 * np.pi, 500)
    z = t / (2 * np.pi)
    plot.plot(
        np.cos(t), np.sin(t), z,
        color=SrgbColor(0.12, 0.47, 0.71, 1.0),
        thickness=2.5,
    )
    plot.plot(
        np.cos(t + np.pi), np.sin(t + np.pi), z,
        color=SrgbColor(1.0, 0.50, 0.05, 1.0),
        thickness=2.5,
    )
    rng = np.random.default_rng(42)
    radius = rng.uniform(0, 0.3, 200)
    theta = rng.uniform(0, 2 * np.pi, 200)
    plot.scatter(
        radius * np.cos(theta),
        radius * np.sin(theta),
        rng.uniform(0, 3, 200),
        color=SrgbColor(0.17, 0.63, 0.17, 1.0),
        size=5.0,
    )
    plot.set_axis_labels("x", "y", "z")
    plot.fit_camera()
    return _page(document, "Double Helix", plot, ("Helix 1", "Helix 2", "Points"))


def surface_plot(document):
    plot = Plot3D(document)
    axis = np.linspace(-10, 10, 80)
    x, y = np.meshgrid(axis, axis)
    radius = np.sqrt(x**2 + y**2) + 1e-6
    z = np.sin(radius) / radius
    plot.set_axis_scale(1.0, 1.0, 5.0)
    surface = plot.surface(x, y, z, colormap=SurfaceColorMap.Viridis)
    plot.surface(
        x,
        y,
        z,
        color=SrgbColor(0.0, 0.0, 0.0, 1.0),
        wireframe=True,
    )
    plot.set_axis_labels("x", "y", "z")
    plot.show_colorbar(surface, "z")
    plot.set_surface_shading(True, 0.45)
    plot.fit_camera()
    return _page(
        document,
        "z = sin(r) / r",
        plot,
        ("Viridis height map", "wireframe overlay", "z scale ×5"),
    )


GALLERY = (
    ("tcplot_sine", "Sine", sine_plot),
    ("tcplot_scatter", "Scatter", scatter_plot),
    ("tcplot_multi", "Multi Plot", multi_plot),
    ("tcplot_marker", "Marker", marker_plot),
    ("tcplot_helix", "3D Helix", helix_plot),
    ("tcplot_surface", "3D Surface", surface_plot),
)
