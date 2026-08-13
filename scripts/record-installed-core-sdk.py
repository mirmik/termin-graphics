#!/usr/bin/env python3
"""Record an immutable installed Core SDK as a Graphics SDK input."""

from __future__ import annotations

import argparse
from pathlib import Path

from termin_build.artifact_manifest import ArtifactManifest, SDK_MANIFEST_NAME
from termin_build.sdk_composition import (
    load_installed_sdk_input,
    write_sdk_inputs,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core-sdk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    core_sdk = args.core_sdk.resolve()
    manifest = ArtifactManifest.load(core_sdk / SDK_MANIFEST_NAME)
    installed = load_installed_sdk_input(
        core_sdk,
        expected_product="core",
        expected_build_id=manifest.native_build_id,
        expected_python_abi=manifest.python_abi,
    )
    args.output.mkdir(parents=True, exist_ok=True)
    write_sdk_inputs(args.output, (installed,))
    print(
        f"Recorded Core SDK {installed.native_build_id} from "
        f"{installed.root} for {args.output.resolve()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
