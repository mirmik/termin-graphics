from tcnodegraph.example import make_demo_graph


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
