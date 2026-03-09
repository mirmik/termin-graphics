"""Core graph data model without UI dependencies."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class Socket:
    """Socket declaration inside a node."""

    name: str
    socket_type: str = "any"
    is_input: bool = True
    multi: bool = False


@dataclass
class Node:
    """Graph node."""

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
    """Connection from output socket to input socket."""

    id: str
    src_node_id: str
    src_socket: str
    dst_node_id: str
    dst_socket: str


@dataclass
class Group:
    """Visual group/container frame."""

    id: str
    title: str
    x: float
    y: float
    width: float
    height: float
    data: dict[str, Any] = field(default_factory=dict)


@dataclass
class Graph:
    """Whole graph container."""

    nodes: dict[str, Node] = field(default_factory=dict)
    edges: dict[str, Edge] = field(default_factory=dict)
    groups: dict[str, Group] = field(default_factory=dict)
