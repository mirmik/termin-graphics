"""termin-nodegraph public API."""

from tcnodegraph.controller import ConnectResult, GraphController
from tcnodegraph.io import graph_from_dict, graph_to_dict, load_graph_json, save_graph_json
from tcnodegraph.model import Edge, Graph, Group, Node, Socket
from tcnodegraph.schema import (
    ConnectionValidator,
    DictSchemaProvider,
    DefaultConnectionValidator,
    NodeSchemaProvider,
    NodeTemplate,
)

try:
    from tcnodegraph.view import NodeGraphSceneAdapter, NodeGraphView
except ModuleNotFoundError:
    NodeGraphSceneAdapter = None
    NodeGraphView = None

__all__ = [
    "Socket",
    "Node",
    "Edge",
    "Group",
    "Graph",
    "NodeTemplate",
    "NodeSchemaProvider",
    "DictSchemaProvider",
    "ConnectionValidator",
    "DefaultConnectionValidator",
    "ConnectResult",
    "GraphController",
    "graph_to_dict",
    "graph_from_dict",
    "save_graph_json",
    "load_graph_json",
    "NodeGraphSceneAdapter",
    "NodeGraphView",
]
