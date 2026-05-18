#!/bin/bash
# Run C/C++ test suites through the top-level CMake graph.
# Assumes SDK dependencies are available, typically via:
#   ./build-sdk-cpp.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_TYPE="Release"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
BUILD_DIR=""
VULKAN_MODE="off"
SDL_MODE="on"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)  BUILD_TYPE="Debug" ;;
        --no-vulkan) VULKAN_MODE="off" ;;
        --vulkan)    VULKAN_MODE="on" ;;
        --no-sdl)    SDL_MODE="off" ;;
        --sdl)       SDL_MODE="on" ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --no-vulkan       Disable Vulkan support (default)"
            echo "  --vulkan          Enable Vulkan support"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default)"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        SDK prefix for installed dependencies (default: ./sdk)"
            echo "  BUILD_DIR         CMake build directory (default: ./build/<BUILD_TYPE>-tests)"
            echo "  BUILD_JOBS        Parallel build jobs (default: nproc)"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/${BUILD_TYPE}-tests"
fi

case "$VULKAN_MODE" in
    off) TERMIN_ENABLE_VULKAN=OFF ;;
    on)  TERMIN_ENABLE_VULKAN=ON ;;
esac

case "$SDL_MODE" in
    off) TERMIN_ENABLE_SDL=OFF ;;
    on)  TERMIN_ENABLE_SDL=ON ;;
esac

export LD_LIBRARY_PATH="${SDK_PREFIX}/lib:${BUILD_DIR}/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo ""
echo "========================================"
echo "  C/C++ tests ($BUILD_TYPE)"
echo "  mode: top-level CMake graph"
echo "========================================"
echo ""
echo "Source dir:  $SCRIPT_DIR"
echo "Build dir:   $BUILD_DIR"
echo "SDK prefix:  $SDK_PREFIX"
echo "Vulkan:      $TERMIN_ENABLE_VULKAN"
echo "SDL2:        $TERMIN_ENABLE_SDL"
echo "Jobs:        $BUILD_JOBS"
echo ""

if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_BUILD_RPATH="${SDK_PREFIX}/lib;${BUILD_DIR}/bin" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DTERMIN_BUILD_PYTHON=OFF \
    -DTERMIN_BUILD_TESTS=ON \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL" \
    -DTERMIN_BUILD_EDITOR_MINIMAL=OFF \
    -DTERMIN_BUILD_EDITOR_EXE=OFF \
    -DTERMIN_BUILD_LAUNCHER=OFF; then
    echo "ERROR: CMake configure failed" >&2
    exit 1
fi

if ! cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"; then
    echo "ERROR: C++ test build failed" >&2
    exit 1
fi

if ! ctest --test-dir "$BUILD_DIR" --output-on-failure; then
    echo "ERROR: C++ tests failed" >&2
    exit 1
fi

echo ""
echo "========================================"
echo "  C/C++ tests finished"
echo "========================================"
