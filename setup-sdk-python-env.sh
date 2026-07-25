#!/bin/bash
# Create checkout-local test tooling and a source overlay over bundled SDK Python.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_ROOT="${TERMIN_TEST_ENV:-$SCRIPT_DIR/build/python-envs/test}"
TOOLS_SITE="$ENV_ROOT/site-packages"
TOOLS_REQUIREMENTS="$SCRIPT_DIR/build-system/python-test-requirements.txt"
OVERLAY_MANIFEST="$ENV_ROOT/overlay.json"
SDK_ROOT="${TERMIN_SDK:-$SCRIPT_DIR/sdk}"
SDK_PYTHON="$SDK_ROOT/bin/termin_python"
BUILD_TOOLS_ROOT="$SCRIPT_DIR/termin-build-tools"
PYTHON_BUILD_ENV="${TERMIN_PYTHON_BUILD_ENV:-$SCRIPT_DIR/build/python-runtime/build-env}"
TEST_TOOLS_PYTHON="${TERMIN_TEST_TOOLS_PYTHON:-$PYTHON_BUILD_ENV/bin/python}"
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
    echo "Run ./build-sdk.sh first." >&2
    exit 1
fi

if [[ ! -x "$TEST_TOOLS_PYTHON" ]]; then
    echo "ERROR: pinned SDK Python build frontend is missing: $TEST_TOOLS_PYTHON" >&2
    echo "Run ./build-sdk.sh first." >&2
    exit 1
fi

ENVIRONMENT_BOOTSTRAP='import sys; sys.path.insert(0, sys.argv.pop(1)); from termin_build.python_test_environment import main; raise SystemExit(main())'
PREPARE_ARGS=(
    prepare
    --environment-root "$ENV_ROOT"
    --requirements "$TOOLS_REQUIREMENTS"
    --installer-python "$TEST_TOOLS_PYTHON"
)
if [[ $FORCE -eq 1 ]]; then
    PREPARE_ARGS+=(--force)
fi
"$SDK_PYTHON" -c "$ENVIRONMENT_BOOTSTRAP" "$BUILD_TOOLS_ROOT" \
    "${PREPARE_ARGS[@]}"

echo "Generating checkout overlay: $OVERLAY_MANIFEST"
OVERLAY_BOOTSTRAP='import sys; sys.path.insert(0, sys.argv.pop(1)); from termin_build.python_overlay import main; raise SystemExit(main())'
"$SDK_PYTHON" -c "$OVERLAY_BOOTSTRAP" "$BUILD_TOOLS_ROOT" \
    --repo-root "$SCRIPT_DIR" \
    --sdk-root "$SDK_ROOT" \
    --output "$OVERLAY_MANIFEST" \
    --extra-site "$TOOLS_SITE"

echo "SDK-backed Python test environment is ready."
echo "Run: ./run-tests-python.sh"
