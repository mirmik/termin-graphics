# termin-nodegraph

Abstract node graph library for Termin ecosystem.

## Layers

1. Native graph/controller and serialization: `termin_nodegraph_core`
2. Stable language boundary: `termin/nodegraph/c_api.h`
3. Transitional Python model and UI projection: `tcnodegraph`

The C++ core depends only on `termin-base` and does not require a UI runtime.
The Python implementation remains temporarily while its binding and consumers
move to the native core.

The C ABI uses generation-checked graph/entity handles, descriptor `struct_size`
fields, copied `tc_value` snapshots and size-query/copy strings. Inputs are
deep-copied; callers own returned `tc_value` trees and release them with
`tc_value_free`. `tc_nodegraph_replace` and `tc_nodegraph_replace_json` validate
into a staging graph and never partially modify the destination.

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

## Interactive native example

After building the SDK, launch the current native projection directly:

```bash
./sdk/bin/termin_python termin-nodegraph/examples/native_nodegraph_demo.py
```

With an SDL-enabled full or `graphics` SDK this opens an interactive window
through the lightweight `termin-window` host. An explicit offscreen path works
with either profile and does not need a window backend:

```bash
./sdk/bin/termin_python termin-nodegraph/examples/native_nodegraph_demo.py \
    --offscreen --output nodegraph-example.png
```

The example contains a typed render-style graph, a visual group, existing
connections and bool/enum/int/float/text parameter editors. Drag nodes and the
group with the left mouse button, drag between compatible sockets to reconnect,
select an item and press Delete to remove it, and close the window to exit.

For an automated window smoke, bound its lifetime by frames or seconds:

```bash
./sdk/bin/termin_python termin-nodegraph/examples/native_nodegraph_demo.py --frames 3
./sdk/bin/termin_python termin-nodegraph/examples/native_nodegraph_demo.py --seconds 5
```

The example uses `GuiWindowAdapter` and `NativeNodeGraphView`; it does not import
the editor application or the retired tcgui frontend.

## Core invariants and serialization

Connections always run from an existing output socket to an existing input
socket on a different node. Socket types must match unless either type is
`"any"`. A socket with `multi=False` has at most one connection; making a new
connection through `GraphController` atomically replaces the previous edge at
each affected single-connection endpoint. A serialized graph that already
violates either endpoint's cardinality is rejected instead of being repaired.

`graph_to_dict()` and `graph_from_dict()` preserve graph, node, and group
metadata, including `Graph.data`. Mutable parameter and metadata containers are
deep-copied at both boundaries, so mutating a serialized dictionary or a loaded
graph does not mutate its source. JSON values are preserved by `save_graph_json()`
and `load_graph_json()`. Invalid loads and rejected controller connections raise
or return diagnostics and emit an error log without partially changing a graph.
