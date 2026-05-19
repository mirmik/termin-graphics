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
            echo "  --help, -h            Show this help"
            echo ""
            echo "Environment:"
            echo "  ANDROID_NDK_HOME or ANDROID_NDK_ROOT"
            echo "                        Android NDK root if --ndk is omitted"
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
    echo "ERROR: Android NDK not found." >&2
    echo "  Set ANDROID_NDK_HOME/ANDROID_NDK_ROOT or pass --ndk /path/to/ndk." >&2
    exit 1
fi

ANDROID_NDK_VALUE="$(cd "$ANDROID_NDK_VALUE" && pwd)"
ANDROID_TOOLCHAIN_FILE="$ANDROID_NDK_VALUE/build/cmake/android.toolchain.cmake"
if [[ ! -f "$ANDROID_TOOLCHAIN_FILE" ]]; then
    echo "ERROR: Android CMake toolchain not found: $ANDROID_TOOLCHAIN_FILE" >&2
    exit 1
fi

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
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DTERMIN_PLATFORM_ANDROID=ON \
    -DTERMIN_USE_CCACHE="$TERMIN_USE_CCACHE"

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

if [[ $INSTALL -eq 1 ]]; then
    cmake --install "$BUILD_DIR"
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
