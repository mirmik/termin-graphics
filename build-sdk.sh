#!/bin/bash
# Build the SDK into ./sdk/ using the dedicated stage scripts:
#   1. build-sdk-bindings.sh — C/C++ libraries and Python bindings through one
#      top-level CMake configuration
#   2. build-sdk-csharp.sh   — C# bindings
#   3. install-pip-packages.sh --target sdk/lib/python3.*/site-packages
#      — populate bundled Python's site-packages from the thin pip packages
#
# To install pip packages into your own user Python environment, run separately:
#   ./install-pip-packages.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
BUILD_DIR="${BUILD_DIR:-}"
BUILD_TYPE="Release"

for arg in "$@"; do
    case "$arg" in
        --debug|-d)
            BUILD_TYPE="Debug"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --clean, -c       Clean build directories first"
            echo "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
            echo "  --ccache          Use ccache if available (default)"
            echo "  --no-ccache       Disable ccache compiler launcher"
            echo "  --ninja           Use Ninja generator for a new build dir"
            echo "  --unity           Enable CMake unity build for C/C++ stages (experimental)"
            echo "  --no-unity        Disable CMake unity build (default)"
            echo "  --pch             Enable precompiled headers for C/C++ stages (default)"
            echo "  --no-pch          Disable precompiled headers"
            echo "  --no-vulkan       Disable Vulkan support"
            echo "  --vulkan          Enable Vulkan support (default for C/C++ stage)"
            echo "  --no-sdl          Disable SDL2 support"
            echo "  --sdl             Enable SDL2 support (default for C/C++ stage)"
            echo "  --no-opengl       Disable OpenGL backend; keep Vulkan render/editor targets"
            echo "  --opengl          Enable desktop OpenGL targets (default)"
            echo "  --help, -h        Show this help"
            echo ""
            echo "Environment:"
            echo "  SDK_PREFIX        Install prefix (default: ./sdk)"
            echo "  BUILD_DIR         C/C++ CMake build directory (default: ./build/<BUILD_TYPE>)"
            echo "  BUILD_JOBS        Parallel build jobs (default: nproc)"
            echo "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
            echo "                    CMake generator for a new build dir (default: CMake default)"
            exit 0
            ;;
    esac
done

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/$BUILD_TYPE"
fi

echo ""
echo "========================================"
echo "  Stage 1/3: C/C++ libraries + Python bindings"
echo "========================================"
echo ""
"$SCRIPT_DIR/build-sdk-bindings.sh" "$@"

echo ""
echo "========================================"
echo "  Stage 2/3: C# bindings"
echo "========================================"
echo ""
bash "$SCRIPT_DIR/build-sdk-csharp.sh" "$@"

echo ""
echo "========================================"
echo "  Stage 3/3: Populate bundled Python site-packages"
echo "========================================"
echo ""
# Resolve the Python version used by the bundled interpreter. Stage 1
# installs the stdlib under sdk/lib/python<MAJOR>.<MINOR>/ (only when
# BUNDLE_PYTHON=ON during the termin CMake build), so we probe for that
# directory and target its site-packages.
BUNDLED_PY_DIR="$(find "$SCRIPT_DIR/sdk/lib" -maxdepth 1 -type d -name 'python3.*' 2>/dev/null | head -1)"
if [[ -z "$BUNDLED_PY_DIR" ]]; then
    echo "WARNING: bundled Python stdlib not found under $SCRIPT_DIR/sdk/lib/python3.*" >&2
    echo "  Skipping pip install into bundled site-packages." >&2
    echo "  Was BUNDLE_PYTHON=ON during the termin CMake build?" >&2
else
    BUNDLED_SITE_PACKAGES="$BUNDLED_PY_DIR/site-packages"
    echo "Bundled Python stdlib:        $BUNDLED_PY_DIR"
    echo "Bundled Python site-packages: $BUNDLED_SITE_PACKAGES"
    echo ""
    # --force bypasses pip's wheel cache: build-sdk.sh can rebuild the
    # native .so files without changing the package version string, and
    # pip would then happily reuse a stale wheel. See
    # install-pip-packages.sh for the full ABI-drift rationale.
    TERMIN_SDK="$SCRIPT_DIR/sdk" \
        "$SCRIPT_DIR/install-pip-packages.sh" --force --target "$BUNDLED_SITE_PACKAGES"
fi

echo ""
echo "========================================"
echo "  Verifying: no duplicate libraries"
echo "========================================"
echo ""

SDK_DIR="$SDK_PREFIX"
DUPES=0

# Check that no .so files are duplicated within the host sdk/,
# excluding layouts that intentionally carry their own platform-specific
# copies.
declare -A LIB_SEEN
while IFS= read -r so_path; do
    lib_name=$(basename "$so_path")
    [[ -L "$so_path" ]] && continue
    # Skip Android SDK slices — these are cross-compiled libraries for
    # another ABI, intentionally colocated under sdk/android/<abi>/.
    [[ "$so_path" == "$SDK_DIR"/android/* ]] && continue
    # Skip csharp runtime copies — NuGet layout requires them
    [[ "$so_path" == */csharp/runtimes/* ]] && continue
    # Skip third-party Python packages (scipy, numpy, etc.)
    [[ "$so_path" == */site-packages/* ]] && continue

    if [[ -n "${LIB_SEEN[$lib_name]}" ]]; then
        echo "  DUPLICATE: $lib_name"
        echo "    - ${LIB_SEEN[$lib_name]}"
        echo "    - $so_path"
        DUPES=$((DUPES + 1))
    else
        LIB_SEEN[$lib_name]="$so_path"
    fi
done < <(find "$SDK_DIR" -name "*.so" -type f 2>/dev/null)

if [[ $DUPES -gt 0 ]]; then
    echo ""
    echo "  FAILED: $DUPES duplicate library/libraries found"
    exit 1
else
    echo "  OK: no duplicate libraries"
fi

echo ""
echo "========================================"
echo "  Verifying: SDK artifacts are up to date"
echo "========================================"
echo ""

STALE=0
SAME_SECOND=0

if [[ ! -d "$BUILD_DIR/bin" ]]; then
    echo "  WARNING: build bin directory not found: $BUILD_DIR/bin"
else
    while IFS= read -r build_so; do
        so_name=$(basename "$build_so")
        build_mtime=$(stat -c '%Y' "$build_so")
        while IFS= read -r sdk_so; do
            sdk_mtime=$(stat -c '%Y' "$sdk_so")
            if [[ $sdk_mtime -lt $build_mtime ]]; then
                echo "  STALE: $sdk_so"
                echo "    older than: $build_so"
                echo "    sdk:   $(stat -c '%y' "$sdk_so")"
                echo "    build: $(stat -c '%y' "$build_so")"
                STALE=$((STALE + 1))
            elif [[ "$sdk_so" -ot "$build_so" ]]; then
                SAME_SECOND=$((SAME_SECOND + 1))
            fi
        done < <(
            find "$SDK_PREFIX" -type f -name "$so_name" \
                ! -path "$SDK_PREFIX/android/*" \
                ! -path "*/csharp/runtimes/*" \
                ! -path "*/site-packages/*" \
                2>/dev/null
        )
    done < <(find "$BUILD_DIR/bin" -maxdepth 1 -type f -name "*.so" 2>/dev/null)
fi

if [[ $STALE -gt 0 ]]; then
    echo ""
    echo "  FAILED: $STALE stale SDK artifact(s) found"
    echo "  Re-run the relevant install stage or remove stale files from $SDK_PREFIX."
    exit 1
else
    echo "  OK: SDK artifacts are not older than matching build artifacts"
    if [[ $SAME_SECOND -gt 0 ]]; then
        echo "  NOTE: $SAME_SECOND matching artifact(s) differed only within timestamp sub-second precision"
    fi
fi

echo ""
echo "========================================"
echo "  All done!"
echo "========================================"
