#!/bin/bash
# Run repo tests: working set by default, full set on request.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FULL=0
NO_EDITOR_SMOKE=0
CPP_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --full)
            FULL=1
            ;;
        --no-editor-smoke)
            NO_EDITOR_SMOKE=1
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS] [run-tests-cpp options]"
            echo ""
            echo "By default this runs the working test set: no window tests,"
            echo "no editor-process smoke tests, and no pytest tests marked full."
            echo ""
            echo "Vulkan is enabled by default for C/C++ tests."
            echo "Use --no-vulkan only for OpenGL/legacy compatibility checks."
            echo ""
            echo "Options:"
            echo "  --full             Include window tests, full pytest tests, and editor smoke tests"
            echo "  --no-editor-smoke  Skip editor-process smoke tests even with --full"
            echo "  --help, -h         Show this help"
            echo ""
            echo "Other options are passed through to run-tests-cpp.sh."
            exit 0
            ;;
        *)
            CPP_ARGS+=("$arg")
            ;;
    esac
done

failures=()

if [[ "$FULL" -eq 1 ]]; then
    CPP_ARGS=(--full "${CPP_ARGS[@]}")
fi

PYTHON_ARGS=()
if [[ "$FULL" -eq 1 ]]; then
    PYTHON_ARGS+=(--full)
fi

if ! bash "$SCRIPT_DIR/run-tests-cpp.sh" "${CPP_ARGS[@]}"; then
    failures+=("C/C++")
fi

if ! bash "$SCRIPT_DIR/run-tests-python.sh" "${PYTHON_ARGS[@]}"; then
    failures+=("Python")
fi

if [[ "$FULL" -eq 1 && "$NO_EDITOR_SMOKE" -eq 0 ]]; then
    echo ""
    echo "========================================"
    echo "  Editor smoke tests"
    echo "========================================"

    if ! "$SCRIPT_DIR/sdk/bin/termin_python" -m termin_build.repository_control \
        --repo-root "$SCRIPT_DIR" run editor-smoke; then
        failures+=("Editor smoke")
    fi
else
    echo ""
    echo "========================================"
    echo "  Editor smoke tests skipped (use --full to include)"
    echo "========================================"
fi

if (( ${#failures[@]} > 0 )); then
    echo ""
    echo "========================================"
    echo "  Test failures"
    echo "========================================"
    printf '  - %s\n' "${failures[@]}"
    exit 1
fi

echo ""
echo "========================================"
echo "  All tests passed"
echo "========================================"
