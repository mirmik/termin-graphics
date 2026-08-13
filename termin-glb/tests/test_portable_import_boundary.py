"""Import-boundary checks for the portable GLB distribution."""

from __future__ import annotations

import json
import subprocess
import sys
from importlib.metadata import metadata

from packaging.requirements import Requirement


def test_portable_glb_distribution_has_no_termin_adapter_dependencies() -> None:
    requirements = {
        Requirement(raw).name
        for raw in metadata("termin-glb").get_all("Requires-Dist", [])
    }

    assert requirements.isdisjoint(
        {
            "termin-assets",
            "termin-default-assets",
            "termin-glb-adapters",
            "termin-scene",
            "termin-components-animation",
            "termin-components-render",
            "termin-components-skeleton",
        }
    )


def test_portable_glb_import_does_not_load_termin_adapters() -> None:
    script = """
import json
import sys
import termin.glb

forbidden = (
    "termin.glb_adapters",
    "termin_assets",
    "termin.default_assets",
    "termin.scene",
    "termin.components",
)
print(json.dumps(sorted(
    name for name in sys.modules
    if name in forbidden or any(name.startswith(prefix + ".") for prefix in forbidden)
)))
"""
    result = subprocess.run(
        [sys.executable, "-c", script],
        check=True,
        capture_output=True,
        text=True,
    )

    assert json.loads(result.stdout) == []
