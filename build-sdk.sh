#!/bin/bash
# Build the SDK into ./sdk/ using the dedicated stage scripts:
#   1. build-sdk-cpp.sh      — C/C++ libraries
#   2. build-sdk-bindings.sh — Python bindings (nanobind)
#   3. build-sdk-csharp.sh   — C# bindings
#   4. install-pip-packages.sh --target sdk/lib/python3.*/site-packages
#      — populate bundled Python's site-packages from the thin pip packages
#
# To install pip packages into your own user Python environment, run separately:
#   ./install-pip-packages.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug, -d       Debug build"
            echo "  --clean, -c       Clean build directories first"
            echo "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
            echo "  --help, -h        Show this help"
            exit 0
            ;;
    esac
done

echo ""
echo "========================================"
echo "  Stage 1/4: C/C++ libraries"
echo "========================================"
echo ""
"$SCRIPT_DIR/build-sdk-cpp.sh" "$@"

echo ""
echo "========================================"
echo "  Stage 2/4: Python bindings (nanobind)"
echo "========================================"
echo ""
"$SCRIPT_DIR/build-sdk-bindings.sh" "$@"

echo ""
echo "========================================"
echo "  Stage 3/4: C# bindings"
echo "========================================"
echo ""
bash "$SCRIPT_DIR/build-sdk-csharp.sh" "$@"

echo ""
echo "========================================"
echo "  Stage 4/4: Populate bundled Python site-packages"
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

SDK_DIR="$SCRIPT_DIR/sdk"
DUPES=0

# Check that no .so files are duplicated within sdk/,
# excluding csharp/runtimes/ (NuGet requires its own copies).
declare -A LIB_SEEN
while IFS= read -r so_path; do
    lib_name=$(basename "$so_path")
    [[ -L "$so_path" ]] && continue
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
echo "  All done!"
echo "========================================"
