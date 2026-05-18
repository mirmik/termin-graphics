#!/bin/bash
# Build and install Python/nanobind bindings through the top-level CMake graph.
# Assumes the C/C++ SDK stage can be built by the same root graph; incremental
# runs only build the Python-related targets that are missing or out of date.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_DIR="${BUILD_DIR:-}"

BUILD_TYPE="Release"
CLEAN=0
NO_PARALLEL=0
VULKAN_MODE="off"
SDL_MODE="on"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
CCACHE_MODE="on"
UNITY_MODE="off"
PCH_MODE="off"
CMAKE_GENERATOR_NAME="${CMAKE_GENERATOR_NAME:-${TERMIN_CMAKE_GENERATOR:-}}"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)    BUILD_TYPE="Debug" ;;
        --clean|-c)    CLEAN=1 ;;
        --no-parallel) NO_PARALLEL=1 ;;
        --ccache)      CCACHE_MODE="on" ;;
        --no-ccache)   CCACHE_MODE="off" ;;
        --ninja)       CMAKE_GENERATOR_NAME="Ninja" ;;
        --unity)       UNITY_MODE="on" ;;
        --no-unity)    UNITY_MODE="off" ;;
        --pch)         PCH_MODE="on" ;;
        --no-pch)      PCH_MODE="off" ;;
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
            echo "  --ccache          Use ccache if available (default)"
            echo "  --no-ccache       Disable ccache compiler launcher"
            echo "  --ninja           Use Ninja generator for a new build dir"
            echo "  --unity           Enable CMake unity build (experimental)"
            echo "  --no-unity        Disable CMake unity build (default)"
            echo "  --pch             Enable precompiled headers for selected C++ targets (experimental)"
            echo "  --no-pch          Disable precompiled headers (default)"
            echo "  --no-vulkan       Disable Vulkan support (default)"
            echo "  --vulkan          Enable Vulkan support"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default)"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        Install prefix (default: ./sdk)"
            echo "  BUILD_DIR         CMake build directory (default: ./build/<BUILD_TYPE>)"
            echo "  BUILD_JOBS        Parallel build jobs (default: nproc)"
            echo "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
            echo "                    CMake generator for a new build dir (default: CMake default)"
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

case "$CCACHE_MODE" in
    off) TERMIN_USE_CCACHE=OFF ;;
    on)  TERMIN_USE_CCACHE=ON ;;
esac

case "$UNITY_MODE" in
    off) TERMIN_ENABLE_UNITY_BUILD=OFF ;;
    on)  TERMIN_ENABLE_UNITY_BUILD=ON ;;
esac

case "$PCH_MODE" in
    off) TERMIN_ENABLE_PCH=OFF ;;
    on)  TERMIN_ENABLE_PCH=ON ;;
esac

PY_EXEC="$(command -v python3 || command -v python || true)"
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found"
    exit 1
fi

"$PY_EXEC" -c "import nanobind" 2>/dev/null || {
    echo "ERROR: nanobind not installed for $PY_EXEC. Run: pip install nanobind"
    exit 1
}

echo ""
echo "========================================"
echo "  Building Termin Python bindings ($BUILD_TYPE)"
echo "  mode: top-level CMake graph"
echo "========================================"
echo ""
echo "Source dir:  $SCRIPT_DIR"
echo "Build dir:   $BUILD_DIR"
echo "SDK prefix:  $SDK_PREFIX"
echo "Python:      $PY_EXEC"
echo "Vulkan:      $TERMIN_ENABLE_VULKAN"
echo "SDL2:        $TERMIN_ENABLE_SDL"
echo "ccache:      $TERMIN_USE_CCACHE"
echo "Unity build: $TERMIN_ENABLE_UNITY_BUILD"
echo "PCH:         $TERMIN_ENABLE_PCH"
echo "Generator:   ${CMAKE_GENERATOR_NAME:-existing/default}"
echo "Jobs:        $BUILD_JOBS"
echo ""

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

cmake_args=()
if [[ -n "$CMAKE_GENERATOR_NAME" && ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake_args+=(-G "$CMAKE_GENERATOR_NAME")
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${cmake_args[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
    -DTERMIN_USE_CCACHE="$TERMIN_USE_CCACHE" \
    -DTERMIN_ENABLE_UNITY_BUILD="$TERMIN_ENABLE_UNITY_BUILD" \
    -DTERMIN_ENABLE_PCH="$TERMIN_ENABLE_PCH" \
    -DTERMIN_BUILD_PYTHON=ON \
    -DTERMIN_BUILD_TESTS=OFF \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL" \
    -DTERMIN_BUILD_EDITOR_MINIMAL=ON \
    -DTERMIN_BUILD_EDITOR_EXE=OFF \
    -DTERMIN_BUILD_LAUNCHER=ON \
    -DTERMIN_BUNDLE_PYTHON=ON \
    -DPython_EXECUTABLE="$PY_EXEC"

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
cmake --install "$BUILD_DIR"

echo ""
echo "========================================"
echo "  Python bindings installed to $SDK_PREFIX"
echo "========================================"
