"""Graph schema extension points."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from tcnodegraph.model import Graph


@dataclass
class NodeTemplate:
    """Node declaration provided by schema."""

    kind: str
    title: str
    inputs: list[tuple[str, str]] = field(default_factory=list)
    outputs: list[tuple[str, str]] = field(default_factory=list)
    defaults: dict[str, object] = field(default_factory=dict)
    width: float = 190.0
    height: float = 120.0


class NodeSchemaProvider(Protocol):
    """Provides node templates by kind."""

    def get_template(self, kind: str) -> NodeTemplate | None:
        raise NotImplementedError


class ConnectionValidator(Protocol):
    """Validates whether two sockets can be connected."""

    def validate(
        self,
        src_type: str,
        dst_type: str,
        *,
        src_node_id: str,
        src_socket: str,
        dst_node_id: str,
        dst_socket: str,
    ) -> bool:
        raise NotImplementedError


class DefaultConnectionValidator:
    """Default compatibility rules."""

    def validate(
        self,
        src_type: str,
        dst_type: str,
        *,
        src_node_id: str,
        src_socket: str,
        dst_node_id: str,
        dst_socket: str,
    ) -> bool:
        if src_node_id == dst_node_id:
            return False
        if src_type == "any" or dst_type == "any":
            return True
        return src_type == dst_type


def connection_rejection_reason(
    graph: Graph,
    src_node_id: str,
    src_socket_name: str,
    dst_node_id: str,
    dst_socket_name: str,
    *,
    validator: ConnectionValidator,
    ignored_edge_ids: set[str] | None = None,
) -> str | None:
    """Return why a connection would violate the graph's core invariants."""
    src_node = graph.nodes.get(src_node_id)
    dst_node = graph.nodes.get(dst_node_id)
    if src_node is None or dst_node is None:
        return "node not found"

    src_socket = next(
        (socket for socket in src_node.outputs if socket.name == src_socket_name),
        None,
    )
    dst_socket = next(
        (socket for socket in dst_node.inputs if socket.name == dst_socket_name),
        None,
    )
    if src_socket is None or dst_socket is None:
        return "socket not found"
    if src_node_id == dst_node_id:
        return "self-link"
    if not validator.validate(
        src_socket.socket_type,
        dst_socket.socket_type,
        src_node_id=src_node_id,
        src_socket=src_socket_name,
        dst_node_id=dst_node_id,
        dst_socket=dst_socket_name,
    ):
        return "type mismatch"

    ignored = ignored_edge_ids or set()
    if not src_socket.multi and any(
        edge.id not in ignored
        and edge.src_node_id == src_node_id
        and edge.src_socket == src_socket_name
        for edge in graph.edges.values()
    ):
        return "output socket does not allow multiple connections"
    if not dst_socket.multi and any(
        edge.id not in ignored
        and edge.dst_node_id == dst_node_id
        and edge.dst_socket == dst_socket_name
        for edge in graph.edges.values()
    ):
        return "input socket does not allow multiple connections"
    return None


class DictSchemaProvider:
    """Schema provider backed by an in-memory dict."""

    def __init__(self, templates: dict[str, NodeTemplate] | None = None) -> None:
        self.templates = templates if templates is not None else {}

    def get_template(self, kind: str) -> NodeTemplate | None:
        return self.templates.get(kind)

    def register(self, template: NodeTemplate) -> None:
        self.templates[template.kind] = template
