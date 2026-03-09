"""JSON serialization helpers."""

from __future__ import annotations

import json
from pathlib import Path

from tcnodegraph.model import Edge, Graph, Group, Node, Socket


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
    for raw in data.get("nodes", []):
        node = Node(
            id=raw["id"],
            kind=raw.get("kind", ""),
            title=raw.get("title", raw.get("kind", "")),
            x=float(raw.get("x", 0.0)),
            y=float(raw.get("y", 0.0)),
            width=float(raw.get("width", 190.0)),
            height=float(raw.get("height", 120.0)),
            params=dict(raw.get("params", {})),
            data=dict(raw.get("data", {})),
            inputs=[
                Socket(
                    name=s.get("name", ""),
                    socket_type=s.get("socket_type", "any"),
                    is_input=True,
                    multi=bool(s.get("multi", False)),
                )
                for s in raw.get("inputs", [])
            ],
            outputs=[
                Socket(
                    name=s.get("name", ""),
                    socket_type=s.get("socket_type", "any"),
                    is_input=False,
                    multi=bool(s.get("multi", True)),
                )
                for s in raw.get("outputs", [])
            ],
        )
        g.nodes[node.id] = node

    for raw in data.get("edges", []):
        edge = Edge(
            id=raw["id"],
            src_node_id=raw["src_node_id"],
            src_socket=raw["src_socket"],
            dst_node_id=raw["dst_node_id"],
            dst_socket=raw["dst_socket"],
        )
        g.edges[edge.id] = edge

    for raw in data.get("groups", []):
        group = Group(
            id=raw["id"],
            title=raw.get("title", ""),
            x=float(raw.get("x", 0.0)),
            y=float(raw.get("y", 0.0)),
            width=float(raw.get("width", 0.0)),
            height=float(raw.get("height", 0.0)),
            data=dict(raw.get("data", {})),
        )
        g.groups[group.id] = group
    return g


def save_graph_json(graph: Graph, path: str | Path) -> None:
    target = Path(path)
    with target.open("w", encoding="utf-8") as f:
        json.dump(graph_to_dict(graph), f, ensure_ascii=False, indent=2)


def load_graph_json(path: str | Path) -> Graph:
    target = Path(path)
    with target.open("r", encoding="utf-8") as f:
        return graph_from_dict(json.load(f))
