"""Graph schema extension points."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol


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


class DictSchemaProvider:
    """Schema provider backed by an in-memory dict."""

    def __init__(self, templates: dict[str, NodeTemplate] | None = None) -> None:
        self.templates = templates if templates is not None else {}

    def get_template(self, kind: str) -> NodeTemplate | None:
        return self.templates.get(kind)

    def register(self, template: NodeTemplate) -> None:
        self.templates[template.kind] = template
