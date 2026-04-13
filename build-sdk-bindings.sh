#!/bin/bash
# Build and install Python bindings (nanobind SDK + C++ libs with Python + pip packages)
# Assumes C++ libraries are already built and installed via build-sdk-cpp.sh
#
# Usage:
#   ./build-sdk-bindings.sh                # Release build
#   ./build-sdk-bindings.sh --debug        # Debug build
#   ./build-sdk-bindings.sh --clean        # Clean before build
#   ./build-sdk-bindings.sh --no-parallel  # Disable parallel build jobs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"

BUILD_TYPE="Release"
CLEAN=0
NO_PARALLEL=0
BUILD_JOBS="$(nproc)"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)    BUILD_TYPE="Debug" ;;
        --clean|-c)    CLEAN=1 ;;
        --no-parallel) NO_PARALLEL=1 ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --clean, -c       Clean build directories first"
            echo "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
            echo "  --help, -h        Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

if [[ $NO_PARALLEL -eq 1 ]]; then
    BUILD_JOBS=1
fi

PY_EXEC="$(command -v python3 || command -v python || true)"
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found"
    exit 1
fi

# ── 1. nanobind SDK ──────────────────────────────────────────────
echo ""
echo "========================================"
echo "  Building termin-nanobind-sdk ($BUILD_TYPE)"
echo "========================================"
echo ""

cd "$SCRIPT_DIR/termin-nanobind-sdk"

build_dir="build/${BUILD_TYPE}"
[[ $CLEAN -eq 1 ]] && rm -rf "$build_dir"
mkdir -p "$build_dir"

"$PY_EXEC" -c "import nanobind" 2>/dev/null || { echo "ERROR: nanobind not installed. Run: pip install nanobind"; exit 1; }

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DPython_EXECUTABLE="$PY_EXEC"
cmake --build "$build_dir" --parallel "$BUILD_JOBS"
cmake --install "$build_dir"

echo "termin-nanobind-sdk installed to ${SDK_PREFIX}"

# ── 2. C++ libraries with Python bindings ────────────────────────
build_with_python() {
    local name="$1"
    local dir="$2"

    echo ""
    echo "========================================"
    echo "  Building $name Python bindings ($BUILD_TYPE)"
    echo "========================================"
    echo ""

    cd "$dir"

    local build_dir="build/${BUILD_TYPE}"
    [[ $CLEAN -eq 1 ]] && rm -rf "$build_dir"
    mkdir -p "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
        -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
        -DTERMIN_BUILD_PYTHON=ON \
        -DPython_EXECUTABLE="$PY_EXEC"
    cmake --build "$build_dir" --parallel "$BUILD_JOBS"
    cmake --install "$build_dir"

    echo "$name Python bindings installed to ${SDK_PREFIX}"
}

# Build Python bindings for modules marked has_python=yes in modules.conf
# (skip entries after @termin-cpp — those are built after termin/build.sh)
_before_termin=1
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | xargs)"
    [[ -z "$line" ]] && continue

    if [[ "$line" == "@termin-cpp" ]]; then
        _before_termin=0
        continue
    fi
    [[ "$line" == @* ]] && continue

    IFS='|' read -r name dir has_python _rest <<< "$line"
    name="$(echo "$name" | xargs)"
    dir="$(echo "$dir" | xargs)"
    has_python="$(echo "$has_python" | xargs)"

    [[ "$has_python" != "yes" ]] && continue
    [[ $_before_termin -eq 0 ]] && continue

    build_with_python "$name" "$SCRIPT_DIR/$dir"
done < "$SCRIPT_DIR/modules.conf"

# ── 3. termin ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo "  Building termin ($BUILD_TYPE)"
echo "========================================"
echo ""

build_args=()
if [[ "$BUILD_TYPE" == "Debug" ]]; then
    build_args+=("--debug")
fi
if [[ $CLEAN -eq 1 ]]; then
    build_args+=("--clean")
fi

if [[ $NO_PARALLEL -eq 1 ]]; then
    CMAKE_BUILD_PARALLEL_LEVEL=1 \
    CMAKE_PREFIX_PATH="$SDK_PREFIX" \
    CMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
    SDK_PREFIX="$SDK_PREFIX" \
    "$SCRIPT_DIR/termin-app/build.sh" "${build_args[@]}"
else
    CMAKE_PREFIX_PATH="$SDK_PREFIX" \
    CMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
    SDK_PREFIX="$SDK_PREFIX" \
    "$SCRIPT_DIR/termin-app/build.sh" "${build_args[@]}"
fi

echo "Installing termin to ${SDK_PREFIX}..."
cp -a "$SCRIPT_DIR/termin-app/install/." "$SDK_PREFIX/"

# Build post-termin Python bindings (modules after @termin-cpp in modules.conf)
_after_termin=0
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | xargs)"
    [[ -z "$line" ]] && continue

    if [[ "$line" == "@termin-cpp" ]]; then
        _after_termin=1
        continue
    fi
    [[ "$line" == @* ]] && continue
    [[ $_after_termin -eq 0 ]] && continue

    IFS='|' read -r name dir has_python _rest <<< "$line"
    name="$(echo "$name" | xargs)"
    dir="$(echo "$dir" | xargs)"
    has_python="$(echo "$has_python" | xargs)"

    [[ "$has_python" != "yes" ]] && continue

    build_with_python "$name" "$SCRIPT_DIR/$dir"
done < "$SCRIPT_DIR/modules.conf"

echo ""
echo "========================================"
echo "  All bindings done!"
echo "========================================"
