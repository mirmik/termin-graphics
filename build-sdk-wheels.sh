#!/bin/bash
# Build Termin Python wheels into the SDK wheelhouse.
#
# The generated wheels are SDK-backed: binding modules are copied from the
# CMake build output, while shared runtime libraries stay in $TERMIN_SDK/lib.
# Consumers install them with:
#
#   TERMIN_SDK=/path/to/sdk pip install --find-links /path/to/sdk/wheels ...

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/scripts/termin-python-packages.sh"

SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_DIR="${BUILD_DIR:-}"
BUILD_TYPE="Release"
WHEEL_DIR="${WHEEL_DIR:-}"
FORCE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug|-d)
            BUILD_TYPE="Debug"
            shift
            ;;
        --force|-f)
            FORCE=1
            shift
            ;;
        --wheel-dir)
            WHEEL_DIR="$2"
            shift 2
            ;;
        --wheel-dir=*)
            WHEEL_DIR="${1#--wheel-dir=}"
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Use build/Debug/bin when BUILD_DIR is unset"
            echo "  --force, -f       Clear wheelhouse and per-package pip build caches first"
            echo "  --wheel-dir DIR   Output directory (default: \$SDK_PREFIX/wheels)"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        SDK install prefix (default: ./sdk)"
            echo "  BUILD_DIR         CMake build directory (default: ./build/<BUILD_TYPE>)"
            echo "  TERMIN_SDK        SDK used by package build_ext (default: SDK_PREFIX)"
            echo "  TERMIN_BINDINGS_DIR"
            echo "                    Directory with pre-built Python bindings"
            echo "  WHEEL_DIR         Output directory alternative to --wheel-dir"
            exit 0
            ;;
        *)
            # build-sdk.sh forwards its full option set; ignore options that
            # only affect CMake/build stages.
            shift
            ;;
    esac
done

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE"
fi
if [[ -z "$WHEEL_DIR" ]]; then
    WHEEL_DIR="$SDK_PREFIX/wheels"
fi

_sdk_valid() { [[ -d "$1/lib" ]]; }

if [[ -n "${TERMIN_SDK:-}" ]]; then
    if ! _sdk_valid "$TERMIN_SDK"; then
        echo "ERROR: TERMIN_SDK=$TERMIN_SDK is set but does not contain lib/" >&2
        exit 1
    fi
elif _sdk_valid "$SDK_PREFIX"; then
    export TERMIN_SDK="$SDK_PREFIX"
else
    echo "ERROR: termin SDK not found at $SDK_PREFIX." >&2
    echo "  Build it first with ./build-sdk-bindings.sh or set TERMIN_SDK." >&2
    exit 1
fi

if [[ -z "${TERMIN_BINDINGS_DIR:-}" ]]; then
    if [[ -d "$BUILD_DIR/bin" ]]; then
        export TERMIN_BINDINGS_DIR="$BUILD_DIR/bin"
    elif [[ -d "$SCRIPT_DIR/build/Release/bin" ]]; then
        export TERMIN_BINDINGS_DIR="$SCRIPT_DIR/build/Release/bin"
    elif [[ -d "$SCRIPT_DIR/build/Debug/bin" ]]; then
        export TERMIN_BINDINGS_DIR="$SCRIPT_DIR/build/Debug/bin"
    fi
fi
if [[ -z "${TERMIN_BINDINGS_DIR:-}" || ! -d "$TERMIN_BINDINGS_DIR" ]]; then
    echo "ERROR: Termin Python bindings directory not found." >&2
    echo "  Set TERMIN_BINDINGS_DIR or build bindings first." >&2
    exit 1
fi

if [[ -z "${PYTHON_BIN:-}" ]]; then
    PYTHON_LAUNCHER="$(command -v python3 || command -v python || true)"
    if [[ -n "$PYTHON_LAUNCHER" ]]; then
        PYTHON_BIN="$("$PYTHON_LAUNCHER" -c 'import sys; print(sys.executable)' 2>/dev/null || true)"
    fi
fi
if [[ -z "$PYTHON_BIN" ]]; then
    echo "ERROR: python3 not found" >&2
    exit 1
fi
PIP_CMD=("$PYTHON_BIN" -m pip)

mkdir -p "$WHEEL_DIR"
WHEEL_DIR="$(cd "$WHEEL_DIR" && pwd)"

export TERMIN_PIP_BUNDLE_LIBS=0
export TERMIN_PIP_COPY_TO_SOURCE=0
export PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}"

echo "Using TERMIN_SDK=$TERMIN_SDK"
echo "Using TERMIN_BINDINGS_DIR=$TERMIN_BINDINGS_DIR"
echo "Using pip: ${PIP_CMD[*]}"
echo "Wheelhouse: $WHEEL_DIR"
echo "TERMIN_PIP_BUNDLE_LIBS=$TERMIN_PIP_BUNDLE_LIBS"
echo "TERMIN_PIP_COPY_TO_SOURCE=$TERMIN_PIP_COPY_TO_SOURCE"

if [[ $FORCE -eq 1 ]]; then
    echo "--force: clearing wheelhouse and per-package pip build caches"
    find "$WHEEL_DIR" -maxdepth 1 -type f -name '*.whl' -delete
    termin_clear_python_package_build_caches "$SCRIPT_DIR"
fi

PACKAGES=("${TERMIN_PYTHON_PACKAGES[@]}")

for pkg in "${PACKAGES[@]}"; do
    echo ""
    echo "========================================"
    echo "  Building wheel: $pkg"
    echo "========================================"
    echo ""
    "${PIP_CMD[@]}" wheel \
        --no-build-isolation \
        --no-deps \
        --no-cache-dir \
        --wheel-dir "$WHEEL_DIR" \
        "$SCRIPT_DIR/$pkg"
done

echo ""
echo "========================================"
echo "  SDK wheelhouse ready: $WHEEL_DIR"
echo "========================================"
