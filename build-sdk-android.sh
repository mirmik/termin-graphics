#!/bin/bash
# Build and optionally install the Termin native Android SDK profile.
#
# This script builds the Android-only CMake graph:
#   - no Python/nanobind bindings
#   - no desktop SDL/OpenGL/editor launcher
#   - Vulkan enabled
#   - termin-android included
#
# It does not build APK/AAB packages. Use ./build-android-apk.sh after
# this script to package the Android app wrapper.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_TYPE="Release"
ANDROID_ABI_VALUE="${ANDROID_ABI:-arm64-v8a}"
ANDROID_PLATFORM_VALUE="${ANDROID_PLATFORM:-android-26}"
ANDROID_NDK_VALUE="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
BUILD_DIR=""
SDK_PREFIX=""
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
CLEAN=0
INSTALL=1
NO_PARALLEL=0
CMAKE_GENERATOR_NAME="${CMAKE_GENERATOR_NAME:-${TERMIN_CMAKE_GENERATOR:-}}"
CCACHE_MODE="on"
CORE_SDK_VALUE="${TERMIN_CORE_SDK:-}"
CORE_BUILD_ID_VALUE="${TERMIN_CORE_BUILD_ID:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug|-d) BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --clean|-c) CLEAN=1 ;;
        --install) INSTALL=1 ;;
        --no-install) INSTALL=0 ;;
        --no-parallel) NO_PARALLEL=1 ;;
        --ccache) CCACHE_MODE="on" ;;
        --no-ccache) CCACHE_MODE="off" ;;
        --ninja) CMAKE_GENERATOR_NAME="Ninja" ;;
        --abi)
            ANDROID_ABI_VALUE="$2"
            shift
            ;;
        --abi=*)
            ANDROID_ABI_VALUE="${1#--abi=}"
            ;;
        --platform)
            ANDROID_PLATFORM_VALUE="$2"
            shift
            ;;
        --platform=*)
            ANDROID_PLATFORM_VALUE="${1#--platform=}"
            ;;
        --ndk)
            ANDROID_NDK_VALUE="$2"
            shift
            ;;
        --ndk=*)
            ANDROID_NDK_VALUE="${1#--ndk=}"
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift
            ;;
        --build-dir=*)
            BUILD_DIR="${1#--build-dir=}"
            ;;
        --prefix)
            SDK_PREFIX="$2"
            shift
            ;;
        --prefix=*)
            SDK_PREFIX="${1#--prefix=}"
            ;;
        --core-sdk)
            CORE_SDK_VALUE="$2"
            shift
            ;;
        --core-sdk=*)
            CORE_SDK_VALUE="${1#--core-sdk=}"
            ;;
        --core-build-id)
            CORE_BUILD_ID_VALUE="$2"
            shift
            ;;
        --core-build-id=*)
            CORE_BUILD_ID_VALUE="${1#--core-build-id=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d           Debug build"
            echo "  --release             Release build (default)"
            echo "  --clean, -c           Clean build directory first"
            echo "  --install             Install after build (default)"
            echo "  --no-install          Configure/build only"
            echo "  --no-parallel         Disable parallel compilation (equivalent to -j1)"
            echo "  --ccache              Use ccache if available (default)"
            echo "  --no-ccache           Disable ccache compiler launcher"
            echo "  --ninja               Use Ninja generator for a new build dir"
            echo "  --abi ABI             Android ABI (default: arm64-v8a)"
            echo "  --platform API        Android platform (default: android-26)"
            echo "  --ndk PATH            Android NDK root"
            echo "  --build-dir DIR       CMake build dir (default: ./build/android/<ABI>)"
            echo "  --prefix DIR          Install prefix (default: ./sdk/android/<ABI>)"
            echo "  --core-sdk DIR        Installed Android Core SDK for this ABI"
            echo "  --core-build-id ID    Exact native_build_id of the Core SDK"
            echo "  --help, -h            Show this help"
            echo ""
            echo "Environment:"
            echo "  ANDROID_NDK_HOME or ANDROID_NDK_ROOT"
            echo "                        Android NDK root if --ndk is omitted; otherwise"
            echo "                        Build/androidNdkRoot from Termin user settings"
            echo "  ANDROID_ABI           Default ABI if --abi is omitted"
            echo "  ANDROID_PLATFORM      Default API/platform if --platform is omitted"
            echo "  BUILD_JOBS            Parallel build jobs (default: nproc)"
            echo "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
            echo "                        CMake generator for a new build dir"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
    shift
done

if [[ $NO_PARALLEL -eq 1 ]]; then
    BUILD_JOBS=1
fi

if [[ -z "$ANDROID_NDK_VALUE" ]]; then
    PY_EXEC="${PYTHON_BIN:-${PYTHON_EXECUTABLE:-}}"
    if [[ -z "$PY_EXEC" ]]; then
        PY_EXEC="$(command -v python3 || command -v python || true)"
    fi
    if [[ -n "$PY_EXEC" ]]; then
        if ! ANDROID_NDK_VALUE="$(
            "$PY_EXEC" "$SCRIPT_DIR/build-system/read-termin-user-setting.py" \
                "Build/androidNdkRoot"
        )"; then
            echo "WARNING: Failed to read Build/androidNdkRoot from Termin user settings." >&2
            ANDROID_NDK_VALUE=""
        fi
    fi
fi

if [[ -z "$ANDROID_NDK_VALUE" ]]; then
    echo "ERROR: Android NDK not found." >&2
    echo "  Pass --ndk, set ANDROID_NDK_HOME/ANDROID_NDK_ROOT, or configure" >&2
    echo "  Build/androidNdkRoot in ~/.config/termin/settings.json." >&2
    exit 1
fi

ANDROID_NDK_VALUE="$(cd "$ANDROID_NDK_VALUE" && pwd)"
ANDROID_TOOLCHAIN_FILE="$ANDROID_NDK_VALUE/build/cmake/android.toolchain.cmake"
if [[ ! -f "$ANDROID_TOOLCHAIN_FILE" ]]; then
    echo "ERROR: Android CMake toolchain not found: $ANDROID_TOOLCHAIN_FILE" >&2
    exit 1
fi
if [[ -z "$CORE_SDK_VALUE" || -z "$CORE_BUILD_ID_VALUE" ]]; then
    echo "ERROR: --core-sdk and --core-build-id are required" >&2
    exit 1
fi
CORE_SDK_VALUE="$(cd "$CORE_SDK_VALUE" && pwd)"
CORE_PLATFORM_MANIFEST="$CORE_SDK_VALUE/termin-core-platform.json"
if [[ ! -f "$CORE_PLATFORM_MANIFEST" ]]; then
    echo "ERROR: Android Core platform manifest is missing: $CORE_PLATFORM_MANIFEST" >&2
    exit 1
fi
CORE_TOOLCHAIN_VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["target"]["toolchain_version"])' "$CORE_PLATFORM_MANIFEST")"

verify_core_owned_artifacts() {
    python3 - "$1" <<'PY'
import hashlib
import json
import pathlib
import sys

sdk = pathlib.Path(sys.argv[1])
manifest = json.loads((sdk / "termin-core-platform.json").read_text())
for entry in manifest["artifacts"]:
    artifact = sdk / entry["path"]
    if not artifact.is_file():
        raise SystemExit(f"ERROR: Core-owned artifact is missing: {artifact}")
    actual = hashlib.sha256(artifact.read_bytes()).hexdigest()
    if actual != entry["sha256"]:
        raise SystemExit(
            f"ERROR: Core-owned artifact was modified: {artifact}: "
            f"expected {entry['sha256']}, got {actual}"
        )
PY
}
verify_core_owned_artifacts "$CORE_SDK_VALUE"

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SCRIPT_DIR/build/android/$ANDROID_ABI_VALUE"
fi
if [[ -z "$SDK_PREFIX" ]]; then
    SDK_PREFIX="$SCRIPT_DIR/sdk/android/$ANDROID_ABI_VALUE"
fi

case "$CCACHE_MODE" in
    off) TERMIN_USE_CCACHE=OFF ;;
    on)  TERMIN_USE_CCACHE=ON ;;
esac

echo ""
echo "========================================"
echo "  Building Termin Android SDK ($BUILD_TYPE)"
echo "========================================"
echo ""
echo "Source dir:       $SCRIPT_DIR"
echo "Build dir:        $BUILD_DIR"
echo "Install prefix:   $SDK_PREFIX"
echo "Core SDK:         $CORE_SDK_VALUE ($CORE_BUILD_ID_VALUE)"
echo "NDK:              $ANDROID_NDK_VALUE"
echo "Toolchain:        $ANDROID_TOOLCHAIN_FILE"
echo "ABI:              $ANDROID_ABI_VALUE"
echo "Platform:         $ANDROID_PLATFORM_VALUE"
echo "ccache:           $TERMIN_USE_CCACHE"
echo "Generator:        ${CMAKE_GENERATOR_NAME:-existing/default}"
echo "Jobs:             $BUILD_JOBS"
echo "Install:          $INSTALL"
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
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_TOOLCHAIN_FILE" \
    -DANDROID_ABI="$ANDROID_ABI_VALUE" \
    -DANDROID_PLATFORM="$ANDROID_PLATFORM_VALUE" \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCMAKE_PREFIX_PATH="$CORE_SDK_VALUE" \
    -DTERMIN_PLATFORM_ANDROID=ON \
    -DTERMIN_CORE_SDK="$CORE_SDK_VALUE" \
    -DTERMIN_CORE_BUILD_ID="$CORE_BUILD_ID_VALUE" \
    -DTERMIN_CORE_TOOLCHAIN_VERSION="$CORE_TOOLCHAIN_VERSION" \
    -DTERMIN_USE_CCACHE="$TERMIN_USE_CCACHE"

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

if [[ $INSTALL -eq 1 ]]; then
    mkdir -p "$SDK_PREFIX"
    cp -a "$CORE_SDK_VALUE/." "$SDK_PREFIX/"
    cmake --install "$BUILD_DIR"
    verify_core_owned_artifacts "$SDK_PREFIX"

    PY_EXEC="${PYTHON_BIN:-${PYTHON_EXECUTABLE:-}}"
    if [[ -z "$PY_EXEC" ]]; then
        PY_EXEC="$(command -v python3 || command -v python || true)"
    fi
    if [[ -z "$PY_EXEC" ]]; then
        echo "ERROR: python3 not found; cannot record Android SDK capabilities" >&2
        exit 1
    fi
    ANDROID_SDK_ROOT_VALUE="$(dirname "$SDK_PREFIX")"
    if [[ "$(basename "$ANDROID_SDK_ROOT_VALUE")" == "android" ]]; then
        SDK_ROOT_VALUE="$(dirname "$ANDROID_SDK_ROOT_VALUE")"
    else
        SDK_ROOT_VALUE="$ANDROID_SDK_ROOT_VALUE"
    fi
    PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
        "$PY_EXEC" -m termin_build.sdk --repo-root "$SCRIPT_DIR" \
        write-android-capabilities \
        --sdk-root "$SDK_ROOT_VALUE" \
        --android-sdk-root "$ANDROID_SDK_ROOT_VALUE" \
        --abi "$ANDROID_ABI_VALUE" \
        --build-dir "$BUILD_DIR"
fi

echo ""
echo "========================================"
echo "  Android SDK build complete"
echo "========================================"
echo ""
echo "Build dir:      $BUILD_DIR"
if [[ $INSTALL -eq 1 ]]; then
    echo "Install prefix: $SDK_PREFIX"
fi
