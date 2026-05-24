#!/bin/bash
# Build the SDK into ./sdk/ using the dedicated stage scripts:
#   1. build-sdk-bindings.sh — C/C++ libraries and Python bindings through one
#      top-level CMake configuration
#   2. build-sdk-csharp.sh   — C# bindings
#   3. install-pip-packages.sh --target sdk/lib/python3.*/site-packages
#      — populate bundled Python's site-packages from the pip packages
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

ensure_bundled_python_runtime() {
    local py_exec="${PYTHON_EXECUTABLE:-}"
    if [[ -z "$py_exec" ]]; then
        py_exec="$(command -v python3 || command -v python || true)"
    fi
    if [[ -z "$py_exec" ]]; then
        echo "ERROR: python3 not found; cannot bundle Python runtime" >&2
        exit 1
    fi

    local py_info=()
    mapfile -t py_info < <("$py_exec" -c 'import site, sys, sysconfig
print(f"{sys.version_info.major}.{sys.version_info.minor}")
print(sysconfig.get_paths()["stdlib"])
print(sysconfig.get_config_var("LIBDIR") or "")
print(sysconfig.get_config_var("LDLIBRARY") or "")
for p in site.getsitepackages():
    print("SITE:" + p)
usersite = site.getusersitepackages()
if usersite:
    print("SITE:" + usersite)
')

    local py_version="${py_info[0]}"
    local py_stdlib="${py_info[1]}"
    local py_libdir="${py_info[2]}"
    local py_ldlibrary="${py_info[3]}"
    local bundled_py_dir="$SDK_PREFIX/lib/python${py_version}"
    local bundled_site_packages="$bundled_py_dir/site-packages"

    if [[ ! -d "$py_stdlib" ]]; then
        echo "ERROR: Python stdlib not found: $py_stdlib" >&2
        exit 1
    fi

    mkdir -p "$SDK_PREFIX/lib" "$bundled_site_packages"

    if [[ -n "$py_libdir" && -n "$py_ldlibrary" && -e "$py_libdir/$py_ldlibrary" ]]; then
        rsync -a "$py_libdir"/libpython"${py_version}"*.so* "$SDK_PREFIX/lib/" 2>/dev/null || true
    fi

    rsync -a \
        --exclude '__pycache__/' \
        --exclude '*.pyc' \
        --exclude '*.pyo' \
        --exclude 'test/' \
        --exclude 'tests/' \
        --exclude 'idle_test/' \
        --exclude 'turtledemo/' \
        --exclude 'lib2to3/' \
        --exclude 'ensurepip/' \
        --exclude 'site-packages/' \
        "$py_stdlib"/ "$bundled_py_dir"/

    local external_packages=(
        numpy
        PyQt6
        sip
        sipbuild
        PIL
        Pillow
        scipy
        glfw
        OpenGL
        pyassimp
        pyopengl
        sdl2
        yaml
        watchdog
    )

    local site_dirs=()
    local line
    for line in "${py_info[@]:4}"; do
        if [[ "$line" == SITE:* ]]; then
            site_dirs+=("${line#SITE:}")
        fi
    done

    shopt -s nullglob
    local site_dir pkg item
    for site_dir in "${site_dirs[@]}"; do
        [[ -d "$site_dir" ]] || continue
        for pkg in "${external_packages[@]}"; do
            if [[ -d "$site_dir/$pkg" ]]; then
                rsync -a --exclude '__pycache__/' --exclude '*.pyc' "$site_dir/$pkg" "$bundled_site_packages"/
            fi
            for item in "$site_dir"/"$pkg"*.dist-info; do
                rsync -a "$item" "$bundled_site_packages"/
            done
        done
        for item in "$site_dir"/*.so "$site_dir"/*.pyd "$site_dir"/numpy.libs "$site_dir"/scipy.libs "$site_dir"/pillow.libs "$site_dir"/Pillow.libs; do
            if [[ -e "$item" ]]; then
                rsync -a "$item" "$bundled_site_packages"/
            fi
        done
    done
    shopt -u nullglob
}

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
BUNDLED_PY_DIR="$(find "$SDK_PREFIX/lib" -maxdepth 1 -type d -name 'python3.*' 2>/dev/null | head -1)"
if [[ -z "$BUNDLED_PY_DIR" ]]; then
    echo "Bundled Python stdlib not found under $SDK_PREFIX/lib/python3.*; creating it from host Python."
    ensure_bundled_python_runtime
    BUNDLED_PY_DIR="$(find "$SDK_PREFIX/lib" -maxdepth 1 -type d -name 'python3.*' 2>/dev/null | head -1)"
fi
if [[ -z "$BUNDLED_PY_DIR" ]]; then
    echo "ERROR: failed to create bundled Python stdlib under $SDK_PREFIX/lib/python3.*" >&2
    exit 1
fi

BUNDLED_SITE_PACKAGES="$BUNDLED_PY_DIR/site-packages"
echo "Bundled Python stdlib:        $BUNDLED_PY_DIR"
echo "Bundled Python site-packages: $BUNDLED_SITE_PACKAGES"
echo ""
# --force bypasses pip's wheel cache: build-sdk.sh can rebuild the
# native .so files without changing the package version string, and
# pip would then happily reuse a stale wheel. See
# install-pip-packages.sh for the full ABI-drift rationale.
TERMIN_SDK="$SDK_PREFIX" \
TERMIN_BINDINGS_DIR="$BUILD_DIR/bin" \
TERMIN_PIP_BUNDLE_LIBS=0 \
TERMIN_PIP_COPY_TO_SOURCE=0 \
    "$SCRIPT_DIR/install-pip-packages.sh" --force --target "$BUNDLED_SITE_PACKAGES"

LEGACY_SDK_PYTHON="$SDK_PREFIX/lib/python"
if [[ -d "$LEGACY_SDK_PYTHON" ]]; then
    echo ""
    echo "Removing legacy SDK Python staging tree: $LEGACY_SDK_PYTHON"
    rm -rf "$LEGACY_SDK_PYTHON"
fi

echo ""
echo "========================================"
echo "  Verifying: no duplicate libraries"
echo "========================================"
echo ""

SDK_DIR="$SDK_PREFIX"
DUPES=0

# Check that no .so files are duplicated within the host sdk/, excluding
# layouts that intentionally carry their own platform-specific copies.
declare -A LIB_SEEN
while IFS= read -r so_path; do
    lib_name=$(basename "$so_path")
    [[ -L "$so_path" ]] && continue
    # Skip Android SDK slices — these are cross-compiled libraries for
    # another ABI, intentionally colocated under sdk/android/<abi>/.
    [[ "$so_path" == "$SDK_DIR"/android/* ]] && continue
    # Skip csharp runtime copies — NuGet layout requires them
    [[ "$so_path" == */csharp/runtimes/* ]] && continue
    # scipy has unrelated internal extension modules with the same basename
    # in different subpackages. Keep the exception narrow so termin packages
    # inside site-packages are still checked.
    [[ "$so_path" == */site-packages/scipy/* ]] && continue
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
