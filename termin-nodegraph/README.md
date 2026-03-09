# termin-nodegraph

Abstract node graph library for Termin ecosystem.

## Layers

1. Core model: `tcnodegraph.model`
2. Editing operations: `tcnodegraph.controller`
3. Serialization: `tcnodegraph.io`
4. Optional tcgui adapter: `tcnodegraph.view`

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

## Demo

Interactive tcgui demo:

```bash
PYTHONPATH=python:../termin-gui/python python3 examples/sdl_nodegraph_demo.py
```

Controls:

1. Left drag on node body: move node
2. Left drag from output socket to input socket: create edge
3. Middle drag: pan
4. Mouse wheel: zoom
5. Delete: remove selected item
