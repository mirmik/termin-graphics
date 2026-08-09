from pathlib import Path
import inspect
import runpy


def test_examples_construct_with_explicit_srgb_colors() -> None:
    examples = Path(__file__).resolve().parents[2] / "examples"

    for name in ("demo_3d_helix.py", "demo_3d_surface.py", "demo_scatter.py"):
        namespace = runpy.run_path(str(examples / name), run_name="tcplot_example_contract")
        widget = namespace["make_plot"]()
        assert widget is not None

    from _host import run_demo
    from termin.geombase import SrgbColor

    assert isinstance(inspect.signature(run_demo).parameters["bg"].default, SrgbColor)


def test_multi_example_allocates_space_to_both_stretch_plots() -> None:
    examples = Path(__file__).resolve().parents[2] / "examples"
    namespace = runpy.run_path(str(examples / "demo_multi.py"), run_name="tcplot_example_contract")
    root = namespace["make_row"]()

    from tcgui.widgets.ui import UI

    ui = UI.__new__(UI)
    ui._root = root
    ui._viewport_w = 0
    ui._viewport_h = 0
    ui.layout(1200, 500)

    assert root.width == 1200
    assert root.height == 500
    assert [child.width for child in root.children] == [595, 595]
