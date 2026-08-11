import importlib.util
from pathlib import Path


_EXAMPLE_PATH = (
    Path(__file__).resolve().parents[1] / "examples" / "native_nodegraph_demo.py"
)
_EXAMPLE_SPEC = importlib.util.spec_from_file_location(
    "tcnodegraph_native_nodegraph_demo",
    _EXAMPLE_PATH,
)
assert _EXAMPLE_SPEC is not None
assert _EXAMPLE_SPEC.loader is not None
_EXAMPLE_MODULE = importlib.util.module_from_spec(_EXAMPLE_SPEC)
_EXAMPLE_SPEC.loader.exec_module(_EXAMPLE_MODULE)
make_demo_graph = _EXAMPLE_MODULE.make_demo_graph


def test_demo_graph_exercises_native_projection_features():
    graph = make_demo_graph()

    assert graph.data == {"example": "native-nodegraph"}
    assert len(graph.nodes) == 4
    assert len(graph.edges) == 3
    assert len(graph.groups) == 1
    kinds = {
        spec["kind"]
        for node in graph.nodes.values()
        for spec in node.data.get("param_specs", {}).values()
    }
    assert kinds == {"bool", "enum", "float", "int", "string"}
