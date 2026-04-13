#!/bin/bash
# Build and install C/C++ parts only (no Python bindings / nanobind modules)
# Dependency order:
#   termin-base -> termin-modules -> termin-mesh -> termin-graphics -> termin-inspect -> termin-scene
#   -> termin-render -> termin-input -> termin-display -> termin-collision -> termin-physics
#   -> termin-components-collision -> termin-components-render -> termin-components-mesh
#   -> termin-engine -> termin-components-kinematic -> termin-components-physics
#   -> termin-skeleton -> termin-animation -> termin-components-skeleton
#   -> termin(cpp only) -> termin-components-animation

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

build_cmake_lib_cpp() {
    local name="$1"
    local dir="$2"
    shift 2
    local extra_args=("$@")

    echo ""
    echo "========================================"
    echo "  Building $name ($BUILD_TYPE) [C/C++ only]"
    echo "========================================"
    echo ""

    cd "$dir"

    local build_dir="build/${BUILD_TYPE}"

    if [[ $CLEAN -eq 1 ]]; then
        echo "Cleaning $build_dir..."
        rm -rf "$build_dir"
    fi

    mkdir -p "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
        -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
        -DTERMIN_BUILD_PYTHON=OFF \
        "${extra_args[@]}"

    cmake --build "$build_dir" --parallel "$BUILD_JOBS"
    cmake --install "$build_dir"

    echo "$name installed to ${SDK_PREFIX}"
}

build_termin_cpp_only() {
    echo ""
    echo "========================================"
    echo "  Building termin ($BUILD_TYPE) [C/C++ only, no nanobind]"
    echo "========================================"
    echo ""

    cd "$SCRIPT_DIR/termin-app"

    local build_dir="build_standalone_cpp/${BUILD_TYPE}"
    if [[ $CLEAN -eq 1 ]]; then
        echo "Cleaning $build_dir..."
        rm -rf "$build_dir"
    fi

    mkdir -p "$build_dir"

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
        -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
        -DBUILD_EDITOR_MINIMAL=OFF \
        -DBUILD_EDITOR_EXE=OFF \
        -DBUILD_LAUNCHER=OFF \
        -DBUNDLE_PYTHON=OFF \
        -DTERMIN_BUILD_PYTHON=OFF

    cmake --build "$build_dir" --parallel "$BUILD_JOBS"
    cmake --install "$build_dir"

    echo "termin (C/C++ only) installed to ${SDK_PREFIX}"
}

# Build chain from modules.conf
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | xargs)"
    [[ -z "$line" ]] && continue

    if [[ "$line" == "@termin-cpp" ]]; then
        build_termin_cpp_only
        continue
    fi

    IFS='|' read -r name dir has_python test_flag extra_cmake <<< "$line"
    name="$(echo "$name" | xargs)"
    dir="$(echo "$dir" | xargs)"
    extra_cmake="$(echo "$extra_cmake" | xargs)"

    extra_args=()
    if [[ "$extra_cmake" != "-" && -n "$extra_cmake" ]]; then
        read -ra extra_args <<< "$extra_cmake"
    fi

    build_cmake_lib_cpp "$name" "$SCRIPT_DIR/$dir" "${extra_args[@]}"
done < "$SCRIPT_DIR/modules.conf"

echo ""
echo "========================================"
echo "  All done (C/C++ only)!"
echo "========================================"
