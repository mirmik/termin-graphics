import numpy as np

from tcplot import Plot3D


def test_plot3d_accepts_series_before_gpu_attachment():
    plot = Plot3D()

    line = plot.plot(
        np.array([0.0, 1.0, 2.0]),
        np.array([0.0, 1.0, 0.0]),
        np.array([0.0, 0.5, 1.0]),
    )
    scatter = plot.scatter(
        np.array([0.0, 1.0]),
        np.array([1.0, 0.0]),
        np.array([0.25, 0.75]),
    )
    surface = plot.surface(
        np.array([[0.0, 1.0], [0.0, 1.0]]),
        np.array([[0.0, 0.0], [1.0, 1.0]]),
        np.array([[0.0, 0.5], [1.0, 0.25]]),
    )

    assert line.scene_id == scatter.scene_id == surface.scene_id
    assert len(plot.data.lines) == 1
    assert len(plot.data.scatters) == 1
    assert len(plot.data.surfaces) == 1
    assert plot.set_surface_grid(0, visible=True)
    plot.show_colorbar(label="height")
    plot.hide_colorbar()

    plot.clear()
    assert not plot.data.lines
    assert not plot.data.scatters
    assert not plot.data.surfaces
