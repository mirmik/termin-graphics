"""Python snapshots and ownership wrapper for the native graph core."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field
from typing import Any

from tcnodegraph._nodegraph_native import NativeGraph


@dataclass
class Socket:
    """Disconnected snapshot of a socket declaration."""

    name: str
    socket_type: str = "any"
    is_input: bool = True
    multi: bool = False


@dataclass
class Node:
    """Disconnected snapshot of a native graph node."""

    id: str
    kind: str
    title: str
    x: float = 0.0
    y: float = 0.0
    width: float = 190.0
    height: float = 120.0
    params: dict[str, Any] = field(default_factory=dict)
    data: dict[str, Any] = field(default_factory=dict)
    inputs: list[Socket] = field(default_factory=list)
    outputs: list[Socket] = field(default_factory=list)


@dataclass
class Edge:
    """Disconnected snapshot of a native graph edge."""

    id: str
    src_node_id: str
    src_socket: str
    dst_node_id: str
    dst_socket: str


@dataclass
class Group:
    """Disconnected snapshot of a native visual group."""

    id: str
    title: str
    x: float
    y: float
    width: float
    height: float
    data: dict[str, Any] = field(default_factory=dict)


class Graph:
    """Owns a native graph; exposed entities are disconnected snapshots."""

    def __init__(self, *, data: dict[str, Any] | None = None) -> None:
        self._native = NativeGraph()
        if data is not None:
            self._native.set_data(deepcopy(data))

    @property
    def revision(self) -> int:
        return self._native.revision

    @property
    def nodes(self) -> dict[str, Node]:
        return {
            raw["id"]: _node_from_dict(raw)
            for raw in self._native.serialize()["nodes"]
        }

    @property
    def edges(self) -> dict[str, Edge]:
        return {
            raw["id"]: Edge(
                id=raw["id"],
                src_node_id=raw["src_node_id"],
                src_socket=raw["src_socket"],
                dst_node_id=raw["dst_node_id"],
                dst_socket=raw["dst_socket"],
            )
            for raw in self._native.serialize()["edges"]
        }

    @property
    def groups(self) -> dict[str, Group]:
        return {
            raw["id"]: _group_from_dict(raw)
            for raw in self._native.serialize()["groups"]
        }

    @property
    def data(self) -> dict[str, Any]:
        return deepcopy(self._native.serialize()["data"])

    @data.setter
    def data(self, value: dict[str, Any]) -> None:
        self._native.set_data(deepcopy(value))


def _socket_from_dict(raw: dict[str, Any], *, is_input: bool) -> Socket:
    return Socket(
        name=raw["name"],
        socket_type=raw.get("socket_type", "any"),
        is_input=is_input,
        multi=bool(raw.get("multi", not is_input)),
    )


def _node_from_dict(raw: dict[str, Any]) -> Node:
    return Node(
        id=raw["id"],
        kind=raw.get("kind", ""),
        title=raw.get("title", raw.get("kind", "")),
        x=float(raw.get("x", 0.0)),
        y=float(raw.get("y", 0.0)),
        width=float(raw.get("width", 190.0)),
        height=float(raw.get("height", 120.0)),
        params=deepcopy(raw.get("params", {})),
        data=deepcopy(raw.get("data", {})),
        inputs=[_socket_from_dict(item, is_input=True) for item in raw.get("inputs", [])],
        outputs=[_socket_from_dict(item, is_input=False) for item in raw.get("outputs", [])],
    )


def _group_from_dict(raw: dict[str, Any]) -> Group:
    return Group(
        id=raw["id"],
        title=raw.get("title", ""),
        x=float(raw.get("x", 0.0)),
        y=float(raw.get("y", 0.0)),
        width=float(raw.get("width", 0.0)),
        height=float(raw.get("height", 0.0)),
        data=deepcopy(raw.get("data", {})),
    )
