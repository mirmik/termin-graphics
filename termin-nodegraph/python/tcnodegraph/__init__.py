"""termin-nodegraph public API."""

import logging

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
    from tcnodegraph.native_view import NativeNodeGraphView, build_native_node_graph_view
except ImportError as e:
    logging.getLogger(__name__).debug(
        "Optional import tcnodegraph.native_view failed: %s — native GUI features unavailable",
        e,
    )
    NativeNodeGraphView = None
    build_native_node_graph_view = None

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
    "NativeNodeGraphView",
    "build_native_node_graph_view",
]
