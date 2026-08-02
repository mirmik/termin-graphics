#!/bin/bash
# Install the repository-pinned Slang and Naga WGSL audit tools.

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

SLANG_VERSION="$(read_lock slang.version)"
SLANG_PLATFORM="$(read_lock slang.platform)"
SLANG_URL="$(read_lock slang.url)"
SLANG_SHA256="$(read_lock slang.sha256)"
NAGA_VERSION="$(read_lock naga_cli.version)"
NAGA_CRATE="$(read_lock naga_cli.crate)"

SLANG_ARCHIVE="$TOOLCHAIN_ROOT/downloads/slang-$SLANG_VERSION-$SLANG_PLATFORM.tar.gz"
SLANG_DIR="$TOOLCHAIN_ROOT/slang-$SLANG_VERSION"
NAGA_DIR="$TOOLCHAIN_ROOT/naga-$NAGA_VERSION"

mkdir -p "$TOOLCHAIN_ROOT/downloads"

if [[ ! -f "$SLANG_ARCHIVE" ]]; then
    curl -fL "$SLANG_URL" -o "$SLANG_ARCHIVE"
fi

ACTUAL_SHA256="$(sha256sum "$SLANG_ARCHIVE" | cut -d ' ' -f 1)"
if [[ "$ACTUAL_SHA256" != "$SLANG_SHA256" ]]; then
    echo "ERROR: Slang archive checksum mismatch" >&2
    echo "Expected: $SLANG_SHA256" >&2
    echo "Actual:   $ACTUAL_SHA256" >&2
    exit 1
fi

if [[ ! -x "$SLANG_DIR/bin/slangc" ]]; then
    mkdir -p "$SLANG_DIR"
    tar -xzf "$SLANG_ARCHIVE" -C "$SLANG_DIR"
fi

ACTUAL_SLANG_VERSION="$($SLANG_DIR/bin/slangc -version 2>&1)"
if [[ "$ACTUAL_SLANG_VERSION" != "$SLANG_VERSION" ]]; then
    echo "ERROR: expected Slang $SLANG_VERSION, got $ACTUAL_SLANG_VERSION" >&2
    exit 1
fi

if [[ ! -x "$NAGA_DIR/bin/naga" ]]; then
    cargo install "$NAGA_CRATE" --version "$NAGA_VERSION" --locked --root "$NAGA_DIR"
fi

ACTUAL_NAGA_VERSION="$($NAGA_DIR/bin/naga --version 2>&1)"
if [[ "$ACTUAL_NAGA_VERSION" != "$NAGA_VERSION" ]]; then
    echo "ERROR: expected Naga $NAGA_VERSION, got $ACTUAL_NAGA_VERSION" >&2
    exit 1
fi

echo "Termin Web shader toolchain ready"
echo "Slang: $SLANG_DIR/bin/slangc ($SLANG_VERSION)"
echo "Naga:  $NAGA_DIR/bin/naga ($NAGA_VERSION)"
