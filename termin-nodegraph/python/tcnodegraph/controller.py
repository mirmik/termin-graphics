"""Schema-aware commands over the native graph core."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import logging

from tcnodegraph.model import Graph, Group, Node, Socket, _group_from_dict, _node_from_dict
from tcnodegraph.schema import (
    ConnectionValidator,
    DefaultConnectionValidator,
    NodeSchemaProvider,
)


_log = logging.getLogger(__name__)


@dataclass
class ConnectResult:
    ok: bool
    edge_id: str = ""
    reason: str = ""


class GraphController:
    """The mutation boundary for a native graph."""

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
        self.graph._native.set_validator(self.validator)

    def replace_graph(self, graph: Graph) -> None:
        self.graph = graph
        self.graph._native.set_validator(self.validator)

    def create_node(
        self,
        kind: str,
        *,
        title: str | None = None,
        x: float = 0.0,
        y: float = 0.0,
        node_id: str | None = None,
    ) -> Node:
        descriptor: dict[str, object] = {
            "id": node_id or "",
            "kind": kind,
            "title": title or kind,
            "x": x,
            "y": y,
        }
        template = self.schema.get_template(kind) if self.schema is not None else None
        if template is not None:
            descriptor.update(
                title=title or template.title,
                width=template.width,
                height=template.height,
                params=deepcopy(template.defaults),
                inputs=[
                    {"name": name, "socket_type": socket_type, "multi": False}
                    for name, socket_type in template.inputs
                ],
                outputs=[
                    {"name": name, "socket_type": socket_type, "multi": True}
                    for name, socket_type in template.outputs
                ],
            )
        try:
            return _node_from_dict(self.graph._native.create_node(descriptor))
        except ValueError as error:
            _log.error("NodeGraph: node creation rejected: %s", error)
            raise

    def remove_node(self, node_id: str) -> bool:
        return self.graph._native.remove_node(node_id)

    def move_node(self, node_id: str, x: float, y: float) -> bool:
        return self.graph._native.move_node(node_id, x, y)

    def set_node_param(self, node_id: str, name: str, value: object) -> bool:
        return self.graph._native.set_node_param(node_id, name, deepcopy(value))

    def set_node_data(self, node_id: str, data: dict[str, object]) -> bool:
        return self.graph._native.set_node_data(node_id, deepcopy(data))

    def update_node_data(self, node_id: str, values: dict[str, object]) -> bool:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return False
        node.data.update(deepcopy(values))
        return self.set_node_data(node_id, node.data)

    def update_node(
        self,
        node_id: str,
        *,
        title: str | None = None,
        width: float | None = None,
        height: float | None = None,
        params: dict[str, object] | None = None,
        data: dict[str, object] | None = None,
        inputs: list[Socket] | None = None,
        outputs: list[Socket] | None = None,
    ) -> Node | None:
        if node_id not in self.graph.nodes:
            return None
        changes: dict[str, object] = {}
        if title is not None:
            changes["title"] = title
        if width is not None:
            changes["width"] = width
        if height is not None:
            changes["height"] = height
        if params is not None:
            changes["params"] = deepcopy(params)
        if data is not None:
            changes["data"] = deepcopy(data)
        if inputs is not None:
            changes["inputs"] = [_socket_to_dict(socket) for socket in inputs]
        if outputs is not None:
            changes["outputs"] = [_socket_to_dict(socket) for socket in outputs]
        return _node_from_dict(self.graph._native.update_node(node_id, changes))

    def set_graph_data(self, data: dict[str, object]) -> None:
        self.graph._native.set_data(deepcopy(data))

    def add_input_socket(
        self,
        node_id: str,
        name: str,
        socket_type: str = "any",
        *,
        multi: bool = False,
    ) -> bool:
        result = self.graph._native.add_socket(node_id, name, socket_type, multi, True)
        if not result:
            _log.error("NodeGraph: input socket creation rejected: %s.%s", node_id, name)
        return result

    def add_output_socket(
        self,
        node_id: str,
        name: str,
        socket_type: str = "any",
        *,
        multi: bool = True,
    ) -> bool:
        result = self.graph._native.add_socket(node_id, name, socket_type, multi, False)
        if not result:
            _log.error("NodeGraph: output socket creation rejected: %s.%s", node_id, name)
        return result

    def connect(
        self,
        src_node_id: str,
        src_socket: str,
        dst_node_id: str,
        dst_socket: str,
        *,
        edge_id: str | None = None,
    ) -> ConnectResult:
        result = self.graph._native.connect(
            src_node_id, src_socket, dst_node_id, dst_socket, edge_id or ""
        )
        if not result["ok"]:
            _log.error("NodeGraph: connection rejected: %s", result["message"])
        return ConnectResult(
            ok=result["ok"], edge_id=result["edge_id"], reason=result["reason"]
        )

    def remove_edge(self, edge_id: str) -> bool:
        return self.graph._native.remove_edge(edge_id)

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
        try:
            return _group_from_dict(
                self.graph._native.create_group(
                    {
                        "id": group_id or "",
                        "title": title,
                        "x": x,
                        "y": y,
                        "width": width,
                        "height": height,
                    }
                )
            )
        except ValueError as error:
            _log.error("NodeGraph: group creation rejected: %s", error)
            raise

    def remove_group(self, group_id: str) -> bool:
        return self.graph._native.remove_group(group_id)

    def move_group(self, group_id: str, x: float, y: float) -> bool:
        return self.graph._native.move_group(group_id, x, y)

    def set_group_data(self, group_id: str, data: dict[str, object]) -> bool:
        return self.graph._native.set_group_data(group_id, deepcopy(data))


def _socket_to_dict(socket: Socket) -> dict[str, object]:
    return {
        "name": socket.name,
        "socket_type": socket.socket_type,
        "multi": socket.multi,
    }
