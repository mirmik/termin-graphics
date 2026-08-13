#!/bin/bash
# Build and install Python/nanobind bindings through the top-level CMake graph.
# Assumes the C/C++ SDK stage can be built by the same root graph; incremental
# runs only build the Python-related targets that are missing or out of date.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_DIR="${BUILD_DIR:-}"
INSTALL_STAGING_DIR="${TERMIN_SDK_INSTALL_STAGING_DIR:-}"

BUILD_TYPE="Release"
CLEAN=0
NO_PARALLEL=0
VULKAN_MODE="on"
SDL_MODE="on"
OPENGL_MODE="on"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
CCACHE_MODE="on"
UNITY_MODE="off"
PCH_MODE="on"
SDK_PROFILE="full"
CMAKE_GENERATOR_NAME="${CMAKE_GENERATOR_NAME:-${TERMIN_CMAKE_GENERATOR:-}}"

for arg in "$@"; do
    case "$arg" in
        --profile=*)   SDK_PROFILE="${arg#--profile=}" ;;
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
        --no-opengl)   OPENGL_MODE="off" ;;
        --opengl)      OPENGL_MODE="on" ;;
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
            echo "  --pch             Enable precompiled headers for selected C++ targets (default)"
            echo "  --no-pch          Disable precompiled headers"
            echo "  --no-vulkan       Disable Vulkan support"
            echo "  --vulkan          Enable Vulkan support (default)"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default)"
            echo "  --no-opengl       Disable OpenGL backend; keep Vulkan render/editor targets"
            echo "  --opengl          Enable desktop OpenGL targets (default)"
            echo "  --profile=NAME    SDK graph profile: full (default), graphics, or core"
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

case "$SDK_PROFILE" in
    full|graphics|core) ;;
    *) echo "Unsupported SDK profile: $SDK_PROFILE (expected full, graphics, or core)"; exit 1 ;;
esac
export TERMIN_SDK_PROFILE="$SDK_PROFILE"

if [[ "$SDK_PROFILE" == "core" ]]; then
    # Graphics backend selection is not part of the Core product contract.
    VULKAN_MODE="off"
    SDL_MODE="off"
    OPENGL_MODE="off"
fi

if [[ $NO_PARALLEL -eq 1 ]]; then
    BUILD_JOBS=1
fi

if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$SDK_PROFILE" == "full" ]]; then
        BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE"
    else
        BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE-$SDK_PROFILE"
    fi
fi

case "$VULKAN_MODE" in
    off) TERMIN_ENABLE_VULKAN=OFF ;;
    on)  TERMIN_ENABLE_VULKAN=ON ;;
esac

case "$SDL_MODE" in
    off) TERMIN_ENABLE_SDL=OFF ;;
    on)  TERMIN_ENABLE_SDL=ON ;;
esac

case "$OPENGL_MODE" in
    off) TERMIN_ENABLE_OPENGL=OFF ;;
    on)  TERMIN_ENABLE_OPENGL=ON ;;
esac

if [[ "$TERMIN_ENABLE_OPENGL" == "ON" ]]; then
    TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=ON
    if [[ "$OSTYPE" == msys* || "$OSTYPE" == cygwin* ]]; then
        TERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS="d3d11;opengl330"
    else
        TERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS="opengl330"
    fi
else
    TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=OFF
    TERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS=""
fi

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

PY_EXEC="${PYTHON_BIN:-${PYTHON_EXECUTABLE:-}}"
if [[ -z "$PY_EXEC" ]]; then
    PY_EXEC="$(command -v python3 || command -v python || true)"
elif command -v "$PY_EXEC" >/dev/null 2>&1; then
    PY_EXEC="$(command -v "$PY_EXEC")"
fi
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
echo "OpenGL:      $TERMIN_ENABLE_OPENGL"
echo "Shaders:     ${TERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS:-source-only}"
echo "ccache:      $TERMIN_USE_CCACHE"
echo "Unity build: $TERMIN_ENABLE_UNITY_BUILD"
echo "PCH:         $TERMIN_ENABLE_PCH"
echo "SDK profile: $SDK_PROFILE"
echo "Generator:   ${CMAKE_GENERATOR_NAME:-existing/default}"
echo "Jobs:        $BUILD_JOBS"
echo ""

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

DOCTOR_PROFILE="sdk-bindings"
if [[ "$SDK_PROFILE" == "graphics" ]]; then
    DOCTOR_PROFILE="sdk-bindings-graphics"
elif [[ "$SDK_PROFILE" == "core" ]]; then
    DOCTOR_PROFILE="sdk-bindings-core"
fi
PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" doctor \
    --profile "$DOCTOR_PROFILE" \
    --vulkan "$TERMIN_ENABLE_VULKAN" \
    --init-submodules

PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" prepare-build-python-runtime \
    --sdk-prefix "$SDK_PREFIX"

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
    -DTERMIN_SDK_PROFILE="$SDK_PROFILE" \
    -DTERMIN_BUILD_PYTHON=ON \
    -DTERMIN_BUILD_TESTS=OFF \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL" \
    -DTERMIN_ENABLE_OPENGL="$TERMIN_ENABLE_OPENGL" \
    -DTERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS="$TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS" \
    -DTERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS="$TERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS" \
    -DTERMIN_BUILD_EDITOR_MINIMAL="$([[ "$SDK_PROFILE" == "full" ]] && echo ON || echo OFF)" \
    -DTERMIN_BUILD_LAUNCHER="$([[ "$SDK_PROFILE" == "full" ]] && echo ON || echo OFF)" \
    -DPython_EXECUTABLE="$PY_EXEC"

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

if [[ -z "$INSTALL_STAGING_DIR" ]]; then
    INSTALL_STAGING_DIR="$BUILD_DIR/sdk-install-staging"
fi
echo "Install staging: $INSTALL_STAGING_DIR"
rm -rf "$INSTALL_STAGING_DIR"
mkdir -p "$INSTALL_STAGING_DIR" "$SDK_PREFIX"
cmake --install "$BUILD_DIR" --prefix "$INSTALL_STAGING_DIR"

sync_staged_dir() {
    local name="$1"
    shift

    if [[ -d "$INSTALL_STAGING_DIR/$name" ]]; then
        mkdir -p "$SDK_PREFIX/$name"
        rsync -a --delete "$@" "$INSTALL_STAGING_DIR/$name"/ "$SDK_PREFIX/$name"/
    fi
}

sync_staged_dir bin
sync_staged_dir include
sync_staged_dir share
sync_staged_dir lib --exclude '/python*/'

# The staged lib/ tree intentionally does not own the bundled CPython shared
# library.  rsync --delete therefore removes it unless we restore the runtime
# after synchronizing native SDK artifacts.
PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" prepare-build-python-runtime \
    --sdk-prefix "$SDK_PREFIX"

PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" publish-cmake-python \
    --install-dir "$INSTALL_STAGING_DIR" \
    --sdk-prefix "$SDK_PREFIX"

PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
    "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" write-artifacts \
    --build-dir "$BUILD_DIR" \
    --sdk-prefix "$SDK_PREFIX" \
    --install-dir "$SDK_PREFIX"

echo ""
echo "========================================"
echo "  Python bindings installed to $SDK_PREFIX"
echo "========================================"
