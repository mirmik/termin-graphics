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
