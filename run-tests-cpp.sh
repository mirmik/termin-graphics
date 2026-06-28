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
FULL=0
VULKAN_MODE="on"
SDL_MODE="on"
WINDOW_TESTS_MODE="off"
CCACHE_MODE="on"
UNITY_MODE="off"
PCH_MODE="on"
CMAKE_GENERATOR_NAME="${CMAKE_GENERATOR_NAME:-${TERMIN_CMAKE_GENERATOR:-}}"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)  BUILD_TYPE="Debug" ;;
        --full)      FULL=1; WINDOW_TESTS_MODE="on" ;;
        --no-vulkan) VULKAN_MODE="off" ;;
        --vulkan)    VULKAN_MODE="on" ;;
        --no-sdl)    SDL_MODE="off" ;;
        --sdl)       SDL_MODE="on" ;;
        --ccache)    CCACHE_MODE="on" ;;
        --no-ccache) CCACHE_MODE="off" ;;
        --ninja)     CMAKE_GENERATOR_NAME="Ninja" ;;
        --unity)     UNITY_MODE="on" ;;
        --no-unity)  UNITY_MODE="off" ;;
        --pch)       PCH_MODE="on" ;;
        --no-pch)    PCH_MODE="off" ;;
        --window-tests)    WINDOW_TESTS_MODE="on" ;;
        --no-window-tests) WINDOW_TESTS_MODE="off" ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "By default this runs the working CTest set and does not build"
            echo "tests that create windows/GL contexts. Use --full to include them."
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --full            Include window/full C++ tests"
            echo "  --no-vulkan       Disable Vulkan support"
            echo "  --vulkan          Enable Vulkan support (default)"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default)"
            echo "  --ccache          Use ccache if available (default)"
            echo "  --no-ccache       Disable ccache compiler launcher"
            echo "  --ninja           Use Ninja generator for a new build dir"
            echo "  --unity           Enable CMake unity build (experimental)"
            echo "  --no-unity        Disable CMake unity build (default)"
            echo "  --pch             Enable precompiled headers for selected C++ targets (default)"
            echo "  --no-pch          Disable precompiled headers"
            echo "  --window-tests    Build and run tests that create windows/GL contexts"
            echo "  --no-window-tests Disable tests that require a windowing system"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        SDK prefix for installed dependencies (default: ./sdk)"
            echo "  BUILD_DIR         CMake build directory (default: ./build/<BUILD_TYPE>-tests)"
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

case "$WINDOW_TESTS_MODE" in
    off)
        TERMIN_BUILD_WINDOW_TESTS=OFF
        ;;
    on)
        TERMIN_BUILD_WINDOW_TESTS=ON
        ;;
    auto)
        if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
            TERMIN_BUILD_WINDOW_TESTS=ON
        else
            TERMIN_BUILD_WINDOW_TESTS=OFF
        fi
        ;;
esac

export LD_LIBRARY_PATH="${BUILD_DIR}/bin:${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

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
echo "Window tests:$TERMIN_BUILD_WINDOW_TESTS ($WINDOW_TESTS_MODE)"
echo "Full set:    $FULL"
echo "ccache:      $TERMIN_USE_CCACHE"
echo "Unity build: $TERMIN_ENABLE_UNITY_BUILD"
echo "PCH:         $TERMIN_ENABLE_PCH"
echo "Generator:   ${CMAKE_GENERATOR_NAME:-existing/default}"
echo "Jobs:        $BUILD_JOBS"
echo ""

PY_EXEC="$(command -v python3 || command -v python || true)"
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found; cannot run build doctor" >&2
    exit 1
fi
if ! PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" doctor \
    --profile cpp-tests \
    --vulkan "$TERMIN_ENABLE_VULKAN" \
    --init-submodules; then
    echo "ERROR: Termin build doctor failed" >&2
    exit 1
fi

cmake_args=()
if [[ -n "$CMAKE_GENERATOR_NAME" && ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake_args+=(-G "$CMAKE_GENERATOR_NAME")
fi

if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${cmake_args[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_BUILD_RPATH="${BUILD_DIR}/bin;${SDK_PREFIX}/lib" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DTERMIN_USE_CCACHE="$TERMIN_USE_CCACHE" \
    -DTERMIN_ENABLE_UNITY_BUILD="$TERMIN_ENABLE_UNITY_BUILD" \
    -DTERMIN_ENABLE_PCH="$TERMIN_ENABLE_PCH" \
    -DTERMIN_BUILD_PYTHON=OFF \
    -DTERMIN_BUILD_TESTS=ON \
    -DTERMIN_BUILD_TGFX2_TESTS=ON \
    -DTERMIN_BUILD_WINDOW_TESTS="$TERMIN_BUILD_WINDOW_TESTS" \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL" \
    -DTERMIN_BUILD_EDITOR_MINIMAL=OFF \
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
