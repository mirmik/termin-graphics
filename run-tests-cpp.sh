#!/bin/bash
# Run C/C++ test suites across projects that define them.
# Assumes SDK dependencies are already installed, typically via:
#   ./build-and-install-cpp.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-/opt/termin}"
BUILD_TYPE="Release"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"

for arg in "$@"; do
    case "$arg" in
        --debug|-d) BUILD_TYPE="Debug" ;;
        --help|-h)
            echo "Usage: $0 [--debug]"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

export LD_LIBRARY_PATH="${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

rebuild_with_tests() {
    local name="$1"
    local dir="$2"
    local test_flag="$3"

    echo ""
    echo "========================================"
    echo "  Testing $name ($BUILD_TYPE)"
    echo "========================================"
    echo ""

    cd "$dir"

    cmake -S . -B "build/${BUILD_TYPE}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_PREFIX_PATH="${SDK_PREFIX}" \
        -DCMAKE_INSTALL_PREFIX="${SDK_PREFIX}" \
        -DTERMIN_BUILD_PYTHON=OFF \
        -D"${test_flag}"=ON

    cmake --build "build/${BUILD_TYPE}" --parallel "${BUILD_JOBS}"
    ctest --test-dir "build/${BUILD_TYPE}" --output-on-failure
}

echo ""
echo "========================================"
echo "  C/C++ tests"
echo "========================================"

rebuild_with_tests "termin-base" "$SCRIPT_DIR/termin-base" "TERMIN_BASE_BUILD_TESTS"
rebuild_with_tests "termin-inspect" "$SCRIPT_DIR/termin-inspect" "TI_BUILD_TESTS"
rebuild_with_tests "termin-graphics" "$SCRIPT_DIR/termin-graphics" "BUILD_TESTS"
rebuild_with_tests "termin-scene" "$SCRIPT_DIR/termin-scene" "TERMIN_SCENE_BUILD_TESTS"
rebuild_with_tests "termin-render" "$SCRIPT_DIR/termin-render" "TERMIN_RENDER_BUILD_TESTS"
rebuild_with_tests "termin-collision" "$SCRIPT_DIR/termin-collision" "TERMIN_COLLISION_BUILD_TESTS"

echo ""
echo "========================================"
echo "  Testing termin-modules"
echo "========================================"
echo ""
cd "$SCRIPT_DIR/termin-modules"
cmake -S . -B "build/${BUILD_TYPE}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_PREFIX_PATH="${SDK_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX="${SDK_PREFIX}" \
    -Dtermin_base_DIR="${SDK_PREFIX}/lib/cmake/termin_base" \
    -DTERMIN_MODULES_BUILD_PYTHON=OFF \
    -DTERMIN_MODULES_BUILD_TESTS=ON
cmake --build "build/${BUILD_TYPE}" --parallel "${BUILD_JOBS}"
ctest --test-dir "build/${BUILD_TYPE}" --output-on-failure

echo ""
echo "========================================"
echo "  Testing termin ($BUILD_TYPE)"
echo "========================================"
echo ""

cd "$SCRIPT_DIR/termin"
cmake -S . -B "build/${BUILD_TYPE}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_PREFIX_PATH="${SDK_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX="${SDK_PREFIX}" \
    -DBUILD_TESTS=ON \
    -DBUILD_EDITOR_EXE=OFF \
    -DBUILD_LAUNCHER=OFF \
    -DBUNDLE_PYTHON=OFF \
    -DTERMIN_BUILD_PYTHON=OFF

cmake --build "build/${BUILD_TYPE}" --parallel "${BUILD_JOBS}"
ctest --test-dir "build/${BUILD_TYPE}" --output-on-failure

echo ""
echo "========================================"
echo "  C/C++ tests finished"
echo "========================================"
