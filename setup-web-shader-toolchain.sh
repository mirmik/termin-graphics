#!/bin/bash
# Install the common Slang toolchain and the repository-pinned Naga audit tool.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCK_FILE="$SCRIPT_DIR/build-system/web-shader-toolchain-lock.json"
TOOLCHAIN_ROOT="${TERMIN_WEB_SHADER_TOOLCHAIN_DIR:-$SCRIPT_DIR/build/toolchains}"

read_lock() {
    python3 - "$LOCK_FILE" "$1" <<'PY'
import json
import pathlib
import sys

value = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
for component in sys.argv[2].split("."):
    value = value[component]
print(value)
PY
}

NAGA_VERSION="$(read_lock naga_cli.version)"
NAGA_CRATE="$(read_lock naga_cli.crate)"

NAGA_DIR="$TOOLCHAIN_ROOT/naga-$NAGA_VERSION"

SLANGC_PATH="$(python3 "$SCRIPT_DIR/scripts/install_slang_toolchain.py" \
    --install-root "$TOOLCHAIN_ROOT" --no-configure --print-path)"

if [[ ! -x "$NAGA_DIR/bin/naga" ]]; then
    cargo install "$NAGA_CRATE" --version "$NAGA_VERSION" --locked --root "$NAGA_DIR"
fi

ACTUAL_NAGA_VERSION="$($NAGA_DIR/bin/naga --version 2>&1)"
if [[ "$ACTUAL_NAGA_VERSION" != "$NAGA_VERSION" ]]; then
    echo "ERROR: expected Naga $NAGA_VERSION, got $ACTUAL_NAGA_VERSION" >&2
    exit 1
fi

echo "Termin Web shader toolchain ready"
echo "Slang: $SLANGC_PATH"
echo "Naga:  $NAGA_DIR/bin/naga ($NAGA_VERSION)"
