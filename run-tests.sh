#!/bin/bash
# Run all repo tests: C/C++ first, then Python.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDITOR_SMOKE=1
CPP_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --no-editor-smoke)
            EDITOR_SMOKE=0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS] [run-tests-cpp options]"
            echo ""
            echo "Vulkan is enabled by default for C/C++ tests."
            echo "Use --no-vulkan only for OpenGL/legacy compatibility checks."
            echo ""
            echo "Options:"
            echo "  --no-editor-smoke  Skip editor-process module hot reload smoke tests"
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

if ! bash "$SCRIPT_DIR/run-tests-cpp.sh" "${CPP_ARGS[@]}"; then
    failures+=("C/C++")
fi

if ! bash "$SCRIPT_DIR/run-tests-python.sh"; then
    failures+=("Python")
fi

run_editor_smoke() {
    local name="$1"
    local script="$2"

    echo ""
    echo "----------------------------------------"
    echo "  $name"
    echo "----------------------------------------"

    if [[ ! -x "$script" ]]; then
        echo "ERROR: smoke script is not executable: $script" >&2
        return 1
    fi

    "$script"
}

if [[ "$EDITOR_SMOKE" -eq 1 ]]; then
    echo ""
    echo "========================================"
    echo "  Editor smoke tests"
    echo "========================================"

    if ! run_editor_smoke "Python module hot reload smoke" "$SCRIPT_DIR/scripts/smoke-python-module-hot-reload"; then
        failures+=("Editor smoke: Python module hot reload")
    fi

    if ! run_editor_smoke "C++ module cascade hot reload smoke" "$SCRIPT_DIR/scripts/smoke-cpp-module-cascade-hot-reload"; then
        failures+=("Editor smoke: C++ module cascade hot reload")
    fi
else
    echo ""
    echo "========================================"
    echo "  Editor smoke tests skipped"
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
