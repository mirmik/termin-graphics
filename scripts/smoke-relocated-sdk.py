#!/usr/bin/env python3
"""Cross-platform test-suite entrypoint for the relocated SDK smoke."""

from pathlib import Path
import os
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "termin-build-tools"))

from termin_build.relocated_sdk_smoke import main  # noqa: E402


if __name__ == "__main__":
    sdk_root = Path(os.environ.get("SDK_PREFIX", REPO_ROOT / "sdk"))
    raise SystemExit(main(["--sdk-root", str(sdk_root)]))
