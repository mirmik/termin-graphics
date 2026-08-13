#!/usr/bin/env python3
"""Load the build frontend from an installed Core SDK under host Python."""

from __future__ import annotations

import os
from pathlib import Path
import sys


def main() -> int:
    site = os.environ.get("TERMIN_CORE_BUILD_FRONTEND")
    if not site:
        print("ERROR: TERMIN_CORE_BUILD_FRONTEND is not set", file=sys.stderr)
        return 1
    site_path = Path(site).resolve()
    if not (site_path / "termin_build").is_dir():
        print(f"ERROR: installed Core build frontend is missing: {site_path}", file=sys.stderr)
        return 1
    sys.path.insert(0, str(site_path))
    from termin_build.sdk import main as sdk_main

    return sdk_main()


if __name__ == "__main__":
    raise SystemExit(main())
