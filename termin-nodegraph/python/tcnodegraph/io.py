"""Serialization helpers backed by the native graph codec."""

from __future__ import annotations

from copy import deepcopy
import logging
from pathlib import Path

from tcnodegraph.model import Graph


_log = logging.getLogger(__name__)


def graph_to_dict(graph: Graph) -> dict:
    return deepcopy(graph._native.serialize())


def graph_from_dict(data: dict) -> Graph:
    graph = Graph()
    try:
        graph._native.replace(deepcopy(data))
    except (TypeError, ValueError) as error:
        _log.error("NodeGraph: rejected invalid graph: %s", error)
        raise
    return graph


def save_graph_json(graph: Graph, path: str | Path) -> None:
    Path(path).write_text(graph._native.to_json(2), encoding="utf-8")


def load_graph_json(path: str | Path) -> Graph:
    graph = Graph()
    try:
        graph._native.replace_json(Path(path).read_text(encoding="utf-8"))
    except (TypeError, ValueError) as error:
        _log.error("NodeGraph: rejected invalid graph: %s", error)
        raise
    return graph
