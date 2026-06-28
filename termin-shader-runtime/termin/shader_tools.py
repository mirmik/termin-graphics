"""Shared helpers for resolving shader tool executables."""

from __future__ import annotations

import os
from pathlib import Path
import shutil


def candidate_executable_paths(path: Path) -> tuple[Path, ...]:
    if os.name == "nt" and path.suffix == "":
        return (path.with_name(f"{path.name}.exe"), path)
    return (path,)


def existing_executable(path: Path) -> Path | None:
    for candidate in candidate_executable_paths(path):
        if candidate.is_file():
            return candidate
    return None


def sdk_tool_dirs(anchor_file: Path) -> tuple[Path, ...]:
    dirs: list[Path] = []

    sdk = os.environ.get("TERMIN_SDK")
    if sdk:
        dirs.append(Path(sdk) / "bin")

    for parent in anchor_file.resolve().parents:
        dirs.append(parent / "bin")
        dirs.append(parent / "sdk" / "bin")

    return tuple(dirs)


def resolve_sdk_tool(name: str, anchor_file: Path) -> Path | None:
    for directory in sdk_tool_dirs(anchor_file):
        resolved = existing_executable(directory / name)
        if resolved is not None:
            return resolved
    return None


def resolve_path_tool(name: str) -> Path | None:
    found = shutil.which(name)
    if found:
        return Path(found)
    return None
