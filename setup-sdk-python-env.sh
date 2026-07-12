#!/bin/bash
# Create checkout-local test tooling and a source overlay over bundled SDK Python.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_ROOT="${TERMIN_TEST_ENV:-$SCRIPT_DIR/build/python-envs/test}"
TOOLS_SITE="$ENV_ROOT/site-packages"
TOOLS_REQUIREMENTS="$SCRIPT_DIR/build-system/python-test-requirements.txt"
TOOLS_STAMP="$ENV_ROOT/python-test-requirements.txt"
OVERLAY_MANIFEST="$ENV_ROOT/overlay.json"
SDK_ROOT="${TERMIN_SDK:-$SCRIPT_DIR/sdk}"
SDK_PYTHON="$SDK_ROOT/bin/termin_python"
FORCE=0

for arg in "$@"; do
    case "$arg" in
        --force|-f) FORCE=1 ;;
        --help|-h)
            echo "Usage: $0 [--force]"
            echo "Creates build/python-envs/test using bundled SDK Python."
            exit 0
            ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if [[ ! -x "$SDK_PYTHON" ]]; then
    echo "ERROR: isolated SDK Python launcher is missing: $SDK_PYTHON" >&2
    echo "Run ./build-sdk.sh --no-wheels first." >&2
    exit 1
fi

BOOTSTRAP_PYTHON="${PYTHON_BOOTSTRAP:-$(command -v python3 || command -v python || true)}"
if [[ -z "$BOOTSTRAP_PYTHON" ]]; then
    echo "ERROR: bootstrap Python was not found." >&2
    exit 1
fi

if [[ $FORCE -eq 1 ]]; then
    rm -rf "$TOOLS_SITE"
fi
mkdir -p "$TOOLS_SITE"

if [[ $FORCE -eq 1 || ! -d "$TOOLS_SITE/ruff" || ! -f "$TOOLS_STAMP" ]] \
    || ! cmp -s "$TOOLS_REQUIREMENTS" "$TOOLS_STAMP"; then
    echo "Installing test-only tools into: $TOOLS_SITE"
    "$BOOTSTRAP_PYTHON" -I -m pip install \
        --no-deps \
        --ignore-installed \
        --upgrade \
        --target "$TOOLS_SITE" \
        -r "$TOOLS_REQUIREMENTS"
    cp "$TOOLS_REQUIREMENTS" "$TOOLS_STAMP"
else
    echo "Test-only tools are up to date: $TOOLS_SITE"
fi

echo "Generating checkout overlay: $OVERLAY_MANIFEST"
"$SDK_PYTHON" -m termin_build.python_overlay \
    --repo-root "$SCRIPT_DIR" \
    --sdk-root "$SDK_ROOT" \
    --output "$OVERLAY_MANIFEST" \
    --extra-site "$TOOLS_SITE"

echo "SDK-backed Python test environment is ready."
echo "Run: ./run-tests-python.sh"
