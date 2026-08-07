# termin-nodegraph

Abstract node graph library for Termin ecosystem.

## Layers

1. Core model: `tcnodegraph.model`
2. Editing operations: `tcnodegraph.controller`
3. Serialization: `tcnodegraph.io`
4. Native UI projection: `tcnodegraph.native_view`

Core modules do not require UI runtime.

## Quick start

```python
from tcnodegraph import Graph, GraphController

graph = Graph()
ctrl = GraphController(graph)

a = ctrl.create_node("ColorPass", x=10, y=20)
b = ctrl.create_node("BloomPass", x=240, y=40)
ctrl.add_output_socket(a.id, "output_res", "fbo")
ctrl.add_input_socket(b.id, "input_res", "fbo")
ctrl.connect(a.id, "output_res", b.id, "input_res")
```

Save/load JSON:

```python
from tcnodegraph import save_graph_json, load_graph_json

save_graph_json(graph, "graph.json")
graph2 = load_graph_json("graph.json")
```
