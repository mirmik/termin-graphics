#!/bin/bash
# Install the repository-pinned Emscripten SDK into the ignored build tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/build-system/emscripten-version.txt")"
EMSDK_DIR="${TERMIN_EMSDK_DIR:-$SCRIPT_DIR/build/toolchains/emsdk}"

if [[ ! -d "$EMSDK_DIR/.git" ]]; then
    mkdir -p "$(dirname "$EMSDK_DIR")"
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi

"$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"

echo "Termin Web toolchain ready: Emscripten $EMSDK_VERSION"
echo "Location: $EMSDK_DIR"
