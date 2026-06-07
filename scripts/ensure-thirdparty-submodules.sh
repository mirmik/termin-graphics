#!/bin/bash
# Ensure required third-party submodules are initialized before configuring CMake.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <submodule-path>..." >&2
    exit 2
fi

PY_EXEC="${PYTHON_BIN:-}"
if [[ -z "$PY_EXEC" ]]; then
    PY_EXEC="$(command -v python3 || command -v python || true)"
fi
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found; cannot initialize submodules" >&2
    exit 1
fi

PYTHONPATH="$REPO_ROOT/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$REPO_ROOT" ensure-submodules "$@"
