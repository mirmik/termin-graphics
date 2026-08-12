#!/usr/bin/env python3
"""Run the Termin graphics-profile showcase from an installed SDK."""

from pathlib import Path
import sys


# The bundled launcher deliberately removes the script directory under -I.
# Admit only this source-only showcase package, never the repository root.
_SHOWCASE_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(_SHOWCASE_ROOT))

from graphics_showcase.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
