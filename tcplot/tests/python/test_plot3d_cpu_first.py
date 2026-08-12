import numpy as np

from tcplot import RetainedChart3D, SrgbColor


def test_retained_chart3d_accepts_series_before_gpu_attachment():
    plot = RetainedChart3D()

    line = plot.add_line(
        np.array([0.0, 1.0, 2.0]),
        np.array([0.0, 1.0, 0.0]),
        np.array([0.0, 0.5, 1.0]),
    )
    scatter = plot.add_scatter(
        np.array([0.0, 1.0]),
        np.array([1.0, 0.0]),
        np.array([0.25, 0.75]),
    )
    surface = plot.add_surface(
        np.array([0.0, 1.0, 0.0, 1.0]),
        np.array([0.0, 0.0, 1.0, 1.0]),
        np.array([0.0, 0.5, 1.0, 0.25]),
        2,
        2,
    )

    assert line.scene_id == scatter.scene_id == surface.scene_id
    assert plot.set_surface_grid(
        surface,
        True,
        8,
        8,
        SrgbColor(0.05, 0.05, 0.05, 1.0),
    )
    plot.set_colorbar(surface, "height")
    plot.clear_colorbar()
    assert plot.destroy_item(line)
    assert plot.destroy_item(scatter)
    assert plot.destroy_item(surface)
