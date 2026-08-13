#!/bin/bash
# Run C/C++ test suites through the SDK's top-level CMake graph.
# The default build directory is shared with build-sdk.sh so native product
# libraries and bundled third-party dependencies are reused incrementally.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_TYPE="Release"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
BUILD_DIR="${BUILD_DIR:-}"
FULL=0
VULKAN_MODE="on"
OPENGL_MODE="on"
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
        --no-opengl) OPENGL_MODE="off" ;;
        --opengl)    OPENGL_MODE="on" ;;
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
            echo "  --no-opengl       Disable OpenGL support"
            echo "  --opengl          Enable OpenGL support (default)"
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

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE"
fi

case "$VULKAN_MODE" in
    off) TERMIN_ENABLE_VULKAN=OFF ;;
    on)  TERMIN_ENABLE_VULKAN=ON ;;
esac

case "$OPENGL_MODE" in
    off) TERMIN_ENABLE_OPENGL=OFF ;;
    on)  TERMIN_ENABLE_OPENGL=ON ;;
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
echo "  mode: shared SDK CMake graph"
echo "========================================"
echo ""
echo "Source dir:  $SCRIPT_DIR"
echo "Build dir:   $BUILD_DIR"
echo "SDK prefix:  $SDK_PREFIX"
echo "Vulkan:      $TERMIN_ENABLE_VULKAN"
echo "OpenGL:      $TERMIN_ENABLE_OPENGL"
echo "SDL2:        $TERMIN_ENABLE_SDL"
echo "Window tests:$TERMIN_BUILD_WINDOW_TESTS ($WINDOW_TESTS_MODE)"
echo "Full set:    $FULL"
echo "ccache:      $TERMIN_USE_CCACHE"
echo "Unity build: $TERMIN_ENABLE_UNITY_BUILD"
echo "PCH:         $TERMIN_ENABLE_PCH"
echo "Generator:   ${CMAKE_GENERATOR_NAME:-existing/default}"
echo "Jobs:        $BUILD_JOBS"
echo ""

PY_EXEC="${PYTHON_BIN:-}"
if [[ -z "$PY_EXEC" && -x "$SCRIPT_DIR/build/python-runtime/build-env/bin/python" ]]; then
    PY_EXEC="$SCRIPT_DIR/build/python-runtime/build-env/bin/python"
fi
if [[ -z "$PY_EXEC" ]]; then
    PY_EXEC="$(command -v python3 || command -v python || true)"
fi
if [[ -z "$PY_EXEC" ]]; then
    echo "ERROR: python3 not found; cannot run build doctor" >&2
    exit 1
fi
export PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}"
REPOSITORY_CONTROL=(
    "$PY_EXEC"
    -m termin_build.repository_control
    --repo-root "$SCRIPT_DIR"
)
if ! "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" doctor \
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

FILE_API_QUERY_DIR="$BUILD_DIR/.cmake/api/v1/query"
mkdir -p "$FILE_API_QUERY_DIR"
touch "$FILE_API_QUERY_DIR/codemodel-v2"

if [[ "$TERMIN_ENABLE_OPENGL" == "ON" ]]; then
    TEST_SHADER_ARTIFACTS=ON
    TEST_SHADER_ARTIFACT_TARGETS="opengl330"
else
    TEST_SHADER_ARTIFACTS=OFF
    TEST_SHADER_ARTIFACT_TARGETS=""
fi

if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${cmake_args[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_PREFIX_PATH="$SDK_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DTERMIN_USE_CCACHE="$TERMIN_USE_CCACHE" \
    -DTERMIN_ENABLE_UNITY_BUILD="$TERMIN_ENABLE_UNITY_BUILD" \
    -DTERMIN_ENABLE_PCH="$TERMIN_ENABLE_PCH" \
    -DTERMIN_BUILD_TESTS=ON \
    -DTERMIN_BUILD_TGFX2_TESTS=ON \
    -DTERMIN_BUILD_WINDOW_TESTS="$TERMIN_BUILD_WINDOW_TESTS" \
    -DTERMIN_ENABLE_VULKAN="$TERMIN_ENABLE_VULKAN" \
    -DTERMIN_ENABLE_OPENGL="$TERMIN_ENABLE_OPENGL" \
    -DTERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS="$TEST_SHADER_ARTIFACTS" \
    -DTERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS="$TEST_SHADER_ARTIFACT_TARGETS" \
    -DTERMIN_ENABLE_SDL="$TERMIN_ENABLE_SDL"; then
    echo "ERROR: CMake configure failed" >&2
    exit 1
fi

REPOSITORY_PROFILE="pr"
REPOSITORY_CAPABILITIES=(--capability host)
if [[ "$FULL" -eq 1 ]]; then
    REPOSITORY_PROFILE="linux-full"
fi
if [[ "$TERMIN_BUILD_WINDOW_TESTS" == "ON" ]]; then
    REPOSITORY_CAPABILITIES+=(--capability window)
fi
if [[ "$TERMIN_ENABLE_VULKAN" == "ON" ]]; then
    REPOSITORY_CAPABILITIES+=(--capability vulkan)
fi
if [[ "$TERMIN_ENABLE_OPENGL" == "ON" ]]; then
    REPOSITORY_CAPABILITIES+=(--capability opengl)
fi
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] \
    && grep -q '^TERMIN_TGFX2_GLFW_AVAILABLE:INTERNAL=TRUE$' "$BUILD_DIR/CMakeCache.txt"; then
    REPOSITORY_CAPABILITIES+=(--capability glfw)
fi
if ! "${REPOSITORY_CONTROL[@]}" check-ctest \
    --build-dir "$BUILD_DIR" \
    --profile "$REPOSITORY_PROFILE" \
    "${REPOSITORY_CAPABILITIES[@]}"; then
    echo "ERROR: CTest inventory validation failed" >&2
    exit 1
fi
CTEST_PLAN_COMMAND=(
    "${REPOSITORY_CONTROL[@]}"
    ctest-plan
    --build-dir "$BUILD_DIR"
    --profile "$REPOSITORY_PROFILE"
    --platform linux
    "${REPOSITORY_CAPABILITIES[@]}"
)
CTEST_SELECTION_JSON="$BUILD_DIR/ctest-selection.json"
if ! "${CTEST_PLAN_COMMAND[@]}" --json > "$CTEST_SELECTION_JSON"; then
    echo "ERROR: CTest planner selection failed" >&2
    exit 1
fi
CTEST_REGEX="$("${CTEST_PLAN_COMMAND[@]}" --regex)"
if [[ "$CTEST_REGEX" == "^()$" ]]; then
    echo "ERROR: CTest planner selected no tests" >&2
    exit 1
fi

CTEST_TARGETS_FILE="$BUILD_DIR/ctest-build-targets.txt"
if ! "${CTEST_PLAN_COMMAND[@]}" --build-targets > "$CTEST_TARGETS_FILE"; then
    echo "ERROR: CTest build target resolution failed" >&2
    exit 1
fi
mapfile -t CTEST_BUILD_TARGETS < "$CTEST_TARGETS_FILE"
if [[ "${#CTEST_BUILD_TARGETS[@]}" -eq 0 ]]; then
    echo "ERROR: CTest planner resolved no CMake build targets" >&2
    exit 1
fi
echo "Building ${#CTEST_BUILD_TARGETS[@]} selected CTest target(s)"
if ! cmake --build "$BUILD_DIR" \
    --target "${CTEST_BUILD_TARGETS[@]}" \
    --parallel "$BUILD_JOBS"; then
    echo "ERROR: C++ test build failed" >&2
    exit 1
fi

# Python shader tests must consume the artifact produced by this exact CMake
# graph/configuration.  Build the producer target explicitly: the aggregate
# native-test target is allowed to contain no dependency on termin_shaderc,
# and merely checking bin/termin_shaderc would therefore accept a stale file.
if ! cmake --build "$BUILD_DIR" \
    --target termin_shaderc \
    --parallel "$BUILD_JOBS"; then
    echo "ERROR: termin_shaderc build failed; refusing to run Python tests" >&2
    exit 1
fi

CTEST_JUNIT="$BUILD_DIR/ctest-results.xml"
# CTest does not reliably replace an existing JUnit document. A stale failure
# must never be reported as the result of a later successful run.
rm -f -- "$CTEST_JUNIT"
CTEST_EXIT=0
ctest --test-dir "$BUILD_DIR" -R "$CTEST_REGEX" --output-on-failure \
    --output-junit "$CTEST_JUNIT" || CTEST_EXIT=$?
if ! "${REPOSITORY_CONTROL[@]}" report-ctest \
    --selection "$CTEST_SELECTION_JSON" \
    --junit "$CTEST_JUNIT" \
    --output "$BUILD_DIR/ctest-execution-manifest.json"; then
    echo "ERROR: CTest execution manifest contains failed or unreported tests" >&2
    exit 1
fi
if [[ "$CTEST_EXIT" -ne 0 ]]; then
    echo "ERROR: C++ tests failed" >&2
    exit 1
fi

echo ""
echo "========================================"
echo "  C/C++ tests finished"
echo "========================================"
