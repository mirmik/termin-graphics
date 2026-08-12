import tcplot


def test_core_package_does_not_export_ui_widgets() -> None:
    assert "Plot2D" not in tcplot.__all__
    assert "Plot3D" not in tcplot.__all__
    assert not hasattr(tcplot, "Plot2D")
    assert not hasattr(tcplot, "Plot3D")
