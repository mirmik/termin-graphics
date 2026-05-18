#!/bin/bash
# Build and install C/C++ SDK libraries through the top-level CMake graph.
# This is the monorepo/superbuild replacement for the old per-module
# configure -> build -> install loop.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_DIR="${BUILD_DIR:-}"

BUILD_TYPE="Release"
CLEAN=0
NO_PARALLEL=0
VULKAN_MODE="off"
SDL_MODE="on"
BUILD_JOBS="$(nproc)"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)    BUILD_TYPE="Debug" ;;
        --clean|-c)    CLEAN=1 ;;
        --no-parallel) NO_PARALLEL=1 ;;
        --no-vulkan)   VULKAN_MODE="off" ;;
        --vulkan)      VULKAN_MODE="on" ;;
        --no-sdl)      SDL_MODE="off" ;;
        --sdl)         SDL_MODE="on" ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --clean, -c       Clean build directory first"
            echo "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
            echo "  --no-vulkan       Disable Vulkan support (default)"
            echo "  --vulkan          Enable Vulkan support"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default)"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        Install prefix (default: ./sdk)"
            echo "  BUILD_DIR         CMake build directory (default: ./build/<BUILD_TYPE>)"
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

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE"
fi

case "$VULKAN_MODE" in
    off) TERMIN_ENABLE_VULKAN=OFF ;;
    on)  TERMIN_ENABLE_VULKAN=ON ;;
esac

case "$SDL_MODE" in
    off) TERMIN_ENABLE_SDL=OFF ;;
    on)  TERMIN_ENABLE_SDL=ON ;;
esac

echo ""
echo "========================================"
echo "  Building Termin C/C++ SDK ($BUILD_TYPE)"
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

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DTERMIN_BUILD_PYTHON=OFF \
    -DTERMIN_BUILD_TESTS=OFF \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL" \
    -DTERMIN_BUILD_EDITOR_MINIMAL=OFF \
    -DTERMIN_BUILD_EDITOR_EXE=OFF \
    -DTERMIN_BUILD_LAUNCHER=OFF

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
cmake --install "$BUILD_DIR"

echo ""
echo "========================================"
echo "  C/C++ SDK installed to $SDK_PREFIX"
echo "========================================"
