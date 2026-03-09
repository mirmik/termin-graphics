"""Graph editing operations."""

from __future__ import annotations

from dataclasses import dataclass

from tcnodegraph.model import Edge, Graph, Group, Node, Socket
from tcnodegraph.schema import (
    ConnectionValidator,
    DefaultConnectionValidator,
    NodeSchemaProvider,
)


@dataclass
class ConnectResult:
    """Connection operation result."""

    ok: bool
    edge_id: str = ""
    reason: str = ""


class GraphController:
    """Mutable graph operations with schema-aware validation."""

    def __init__(
        self,
        graph: Graph | None = None,
        *,
        schema: NodeSchemaProvider | None = None,
        validator: ConnectionValidator | None = None,
    ) -> None:
        self.graph = graph if graph is not None else Graph()
        self.schema = schema
        self.validator = validator if validator is not None else DefaultConnectionValidator()
        self._id_counters: dict[str, int] = {}

    def _next_id(self, prefix: str) -> str:
        n = self._id_counters.get(prefix, 0) + 1
        while True:
            candidate = f"{prefix}_{n}"
            if prefix == "node" and candidate in self.graph.nodes:
                n += 1
                continue
            if prefix == "edge" and candidate in self.graph.edges:
                n += 1
                continue
            if prefix == "group" and candidate in self.graph.groups:
                n += 1
                continue
            self._id_counters[prefix] = n
            return candidate

    def create_node(
        self,
        kind: str,
        *,
        title: str | None = None,
        x: float = 0.0,
        y: float = 0.0,
        node_id: str | None = None,
    ) -> Node:
        node = Node(
            id=node_id or self._next_id("node"),
            kind=kind,
            title=title or kind,
            x=x,
            y=y,
        )

        template = self.schema.get_template(kind) if self.schema is not None else None
        if template is not None:
            node.title = title or template.title
            node.width = template.width
            node.height = template.height
            node.params.update(template.defaults)
            node.inputs = [Socket(n, t, is_input=True) for n, t in template.inputs]
            node.outputs = [Socket(n, t, is_input=False) for n, t in template.outputs]

        self.graph.nodes[node.id] = node
        return node

    def remove_node(self, node_id: str) -> bool:
        if node_id not in self.graph.nodes:
            return False
        del self.graph.nodes[node_id]

        to_delete = [
            edge_id
            for edge_id, edge in self.graph.edges.items()
            if edge.src_node_id == node_id or edge.dst_node_id == node_id
        ]
        for edge_id in to_delete:
            del self.graph.edges[edge_id]
        return True

    def move_node(self, node_id: str, x: float, y: float) -> bool:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return False
        node.x = x
        node.y = y
        return True

    def set_node_param(self, node_id: str, name: str, value: object) -> bool:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return False
        node.params[name] = value
        return True

    def add_input_socket(
        self,
        node_id: str,
        name: str,
        socket_type: str = "any",
        *,
        multi: bool = False,
    ) -> bool:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return False
        if any(s.name == name for s in node.inputs):
            return False
        node.inputs.append(Socket(name, socket_type, is_input=True, multi=multi))
        return True

    def add_output_socket(
        self,
        node_id: str,
        name: str,
        socket_type: str = "any",
        *,
        multi: bool = True,
    ) -> bool:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return False
        if any(s.name == name for s in node.outputs):
            return False
        node.outputs.append(Socket(name, socket_type, is_input=False, multi=multi))
        return True

    def connect(
        self,
        src_node_id: str,
        src_socket: str,
        dst_node_id: str,
        dst_socket: str,
        *,
        edge_id: str | None = None,
    ) -> ConnectResult:
        src_node = self.graph.nodes.get(src_node_id)
        dst_node = self.graph.nodes.get(dst_node_id)
        if src_node is None or dst_node is None:
            return ConnectResult(False, reason="node not found")

        src = next((s for s in src_node.outputs if s.name == src_socket), None)
        dst = next((s for s in dst_node.inputs if s.name == dst_socket), None)
        if src is None or dst is None:
            return ConnectResult(False, reason="socket not found")

        if not self.validator.validate(
            src.socket_type,
            dst.socket_type,
            src_node_id=src_node_id,
            src_socket=src_socket,
            dst_node_id=dst_node_id,
            dst_socket=dst_socket,
        ):
            return ConnectResult(False, reason="type mismatch")

        if not dst.multi:
            to_delete = [
                eid
                for eid, edge in self.graph.edges.items()
                if edge.dst_node_id == dst_node_id and edge.dst_socket == dst_socket
            ]
            for eid in to_delete:
                del self.graph.edges[eid]

        e = Edge(
            id=edge_id or self._next_id("edge"),
            src_node_id=src_node_id,
            src_socket=src_socket,
            dst_node_id=dst_node_id,
            dst_socket=dst_socket,
        )
        self.graph.edges[e.id] = e
        return ConnectResult(True, edge_id=e.id)

    def remove_edge(self, edge_id: str) -> bool:
        if edge_id not in self.graph.edges:
            return False
        del self.graph.edges[edge_id]
        return True

    def add_group(
        self,
        title: str,
        x: float,
        y: float,
        width: float,
        height: float,
        *,
        group_id: str | None = None,
    ) -> Group:
        g = Group(
            id=group_id or self._next_id("group"),
            title=title,
            x=x,
            y=y,
            width=width,
            height=height,
        )
        self.graph.groups[g.id] = g
        return g

    def remove_group(self, group_id: str) -> bool:
        if group_id not in self.graph.groups:
            return False
        del self.graph.groups[group_id]
        return True
