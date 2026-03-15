#!/bin/bash
# Run C/C++ test suites across projects that define them.
# Assumes SDK dependencies are already installed, typically via:
#   ./build-and-install-cpp.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
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
        -DCMAKE_BUILD_RPATH="${SDK_PREFIX}/lib" \
        -DTERMIN_BUILD_PYTHON=OFF \
        -D"${test_flag}"=ON

    cmake --build "build/${BUILD_TYPE}" --parallel "${BUILD_JOBS}"
    ctest --test-dir "build/${BUILD_TYPE}" --output-on-failure
}

echo ""
echo "========================================"
echo "  C/C++ tests"
echo "========================================"

# Build modules with tests from modules.conf
while IFS= read -r line; do
    line="${line%%#*}"
    line="$(echo "$line" | xargs)"
    [[ -z "$line" ]] && continue
    [[ "$line" == @* ]] && continue

    IFS='|' read -r name dir has_python test_flag _rest <<< "$line"
    name="$(echo "$name" | xargs)"
    dir="$(echo "$dir" | xargs)"
    test_flag="$(echo "$test_flag" | xargs)"

    [[ "$test_flag" == "-" || -z "$test_flag" ]] && continue

    rebuild_with_tests "$name" "$SCRIPT_DIR/$dir" "$test_flag"
done < "$SCRIPT_DIR/modules.conf"

# Test termin itself
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
    -DCMAKE_BUILD_RPATH="${SDK_PREFIX}/lib" \
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
