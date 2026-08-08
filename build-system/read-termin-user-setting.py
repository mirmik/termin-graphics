#!/usr/bin/env python3
"""Read one hierarchical key from the shared Termin user settings JSON."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys


def settings_path() -> Path:
    if os.name == "nt":
        root = os.environ.get("APPDATA")
        if root:
            return Path(root) / "termin/settings.json"
        return Path.home() / "AppData/Roaming/termin/settings.json"
    root = os.environ.get("XDG_CONFIG_HOME")
    return Path(root) / "termin/settings.json" if root else Path.home() / ".config/termin/settings.json"


def read_setting(path: Path, key: str) -> str | None:
    if not path.is_file():
        return None
    try:
        value: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read Termin settings {path}: {error}") from error
    for part in (part for part in key.split("/") if part):
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    if value is None or value == "":
        return None
    if not isinstance(value, str):
        raise RuntimeError(f"Termin setting {key!r} in {path} must be a string")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("key")
    parser.add_argument("--settings", type=Path, default=None)
    args = parser.parse_args()
    try:
        value = read_setting(args.settings or settings_path(), args.key)
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    if value is not None:
        print(value)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
