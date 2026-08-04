#!/bin/bash
# Install the pinned Slang compiler and register it in common Termin settings.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TERMIN_PYTHON="${TERMIN_PYTHON:-$SCRIPT_DIR/sdk/bin/termin_python}"

if [[ ! -x "$TERMIN_PYTHON" ]]; then
    echo "ERROR: Termin SDK Python is missing: $TERMIN_PYTHON" >&2
    echo "Build the SDK first with ./build-sdk.sh, or set TERMIN_PYTHON." >&2
    exit 1
fi

exec "$TERMIN_PYTHON" "$SCRIPT_DIR/scripts/install_slang_toolchain.py" "$@"
