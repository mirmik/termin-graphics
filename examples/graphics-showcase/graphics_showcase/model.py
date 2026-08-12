from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable


Cleanup = Callable[[], None]


@dataclass
class SectionContent:
    """Objects and facts retained until a showcase section has rendered."""

    root: Any | None = None
    cleanup: Cleanup = lambda: None
    facts: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class Section:
    """One independently rendered part of the graphics product surface."""

    name: str
    description: str
    capabilities: tuple[str, ...]
    build: Callable[[Any], SectionContent]
    artifact: bool = False


@dataclass(frozen=True)
class ShowcaseConfig:
    width: int
    height: int
    output_path: Path
    report_path: Path
