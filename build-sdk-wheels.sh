#!/bin/bash
# Build Termin Python wheels through the shared Python orchestrator.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PY_EXEC="${PYTHON_BIN:-${PYTHON_EXECUTABLE:-}}"
if [[ -z "$PY_EXEC" ]]; then
    PY_EXEC="$(command -v python3 || command -v python || true)"
fi
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found; cannot run Termin SDK wheelhouse builder" >&2
    exit 1
fi

PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" wheels "$@"
