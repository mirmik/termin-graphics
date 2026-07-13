"""JSON serialization helpers."""

from __future__ import annotations

import json
import logging
from pathlib import Path

from tcnodegraph.model import Edge, Graph, Group, Node, Socket


_log = logging.getLogger(__name__)


def graph_to_dict(graph: Graph) -> dict:
    return {
        "nodes": [
            {
                "id": n.id,
                "kind": n.kind,
                "title": n.title,
                "x": n.x,
                "y": n.y,
                "width": n.width,
                "height": n.height,
                "params": n.params,
                "data": n.data,
                "inputs": [
                    {
                        "name": s.name,
                        "socket_type": s.socket_type,
                        "is_input": s.is_input,
                        "multi": s.multi,
                    }
                    for s in n.inputs
                ],
                "outputs": [
                    {
                        "name": s.name,
                        "socket_type": s.socket_type,
                        "is_input": s.is_input,
                        "multi": s.multi,
                    }
                    for s in n.outputs
                ],
            }
            for n in graph.nodes.values()
        ],
        "edges": [
            {
                "id": e.id,
                "src_node_id": e.src_node_id,
                "src_socket": e.src_socket,
                "dst_node_id": e.dst_node_id,
                "dst_socket": e.dst_socket,
            }
            for e in graph.edges.values()
        ],
        "groups": [
            {
                "id": g.id,
                "title": g.title,
                "x": g.x,
                "y": g.y,
                "width": g.width,
                "height": g.height,
                "data": g.data,
            }
            for g in graph.groups.values()
        ],
    }


def graph_from_dict(data: dict) -> Graph:
    g = Graph()
    node_ids: set[str] = set()
    for raw in data.get("nodes", []):
        node_id = raw["id"]
        if node_id in node_ids:
            raise _invalid_graph(f"duplicate node id: {node_id}")
        node_ids.add(node_id)
        inputs = _parse_sockets(raw.get("inputs", []), is_input=True, node_id=node_id)
        outputs = _parse_sockets(raw.get("outputs", []), is_input=False, node_id=node_id)
        node = Node(
            id=node_id,
            kind=raw.get("kind", ""),
            title=raw.get("title", raw.get("kind", "")),
            x=float(raw.get("x", 0.0)),
            y=float(raw.get("y", 0.0)),
            width=float(raw.get("width", 190.0)),
            height=float(raw.get("height", 120.0)),
            params=dict(raw.get("params", {})),
            data=dict(raw.get("data", {})),
            inputs=inputs,
            outputs=outputs,
        )
        g.nodes[node.id] = node

    edge_ids: set[str] = set()
    for raw in data.get("edges", []):
        edge_id = raw["id"]
        if edge_id in edge_ids:
            raise _invalid_graph(f"duplicate edge id: {edge_id}")
        edge_ids.add(edge_id)
        src_node_id = raw["src_node_id"]
        dst_node_id = raw["dst_node_id"]
        src_node = g.nodes.get(src_node_id)
        dst_node = g.nodes.get(dst_node_id)
        if src_node is None or dst_node is None:
            raise _invalid_graph(f"edge {edge_id} references a missing node")
        if not any(socket.name == raw["src_socket"] for socket in src_node.outputs):
            raise _invalid_graph(f"edge {edge_id} references a missing output socket")
        if not any(socket.name == raw["dst_socket"] for socket in dst_node.inputs):
            raise _invalid_graph(f"edge {edge_id} references a missing input socket")
        edge = Edge(
            id=edge_id,
            src_node_id=src_node_id,
            src_socket=raw["src_socket"],
            dst_node_id=dst_node_id,
            dst_socket=raw["dst_socket"],
        )
        g.edges[edge.id] = edge

    group_ids: set[str] = set()
    for raw in data.get("groups", []):
        group_id = raw["id"]
        if group_id in group_ids:
            raise _invalid_graph(f"duplicate group id: {group_id}")
        group_ids.add(group_id)
        group = Group(
            id=group_id,
            title=raw.get("title", ""),
            x=float(raw.get("x", 0.0)),
            y=float(raw.get("y", 0.0)),
            width=float(raw.get("width", 0.0)),
            height=float(raw.get("height", 0.0)),
            data=dict(raw.get("data", {})),
        )
        g.groups[group.id] = group
    return g


def _parse_sockets(raw_sockets: object, *, is_input: bool, node_id: str) -> list[Socket]:
    if not isinstance(raw_sockets, list):
        raise _invalid_graph(f"node {node_id} sockets must be a list")
    sockets: list[Socket] = []
    names: set[str] = set()
    for raw_socket in raw_sockets:
        if not isinstance(raw_socket, dict):
            raise _invalid_graph(f"node {node_id} socket must be an object")
        name = raw_socket.get("name", "")
        if not isinstance(name, str) or not name or name in names:
            raise _invalid_graph(f"node {node_id} has a duplicate or empty socket id")
        names.add(name)
        sockets.append(
            Socket(
                name=name,
                socket_type=raw_socket.get("socket_type", "any"),
                is_input=is_input,
                multi=bool(raw_socket.get("multi", not is_input)),
            )
        )
    return sockets


def _invalid_graph(message: str) -> ValueError:
    _log.error("NodeGraph: rejected invalid graph: %s", message)
    return ValueError(message)


def save_graph_json(graph: Graph, path: str | Path) -> None:
    target = Path(path)
    with target.open("w", encoding="utf-8") as f:
        json.dump(graph_to_dict(graph), f, ensure_ascii=False, indent=2)


def load_graph_json(path: str | Path) -> Graph:
    target = Path(path)
    with target.open("r", encoding="utf-8") as f:
        return graph_from_dict(json.load(f))
