#!/bin/bash
# Run all repo tests: C/C++ first, then Python.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            echo "Usage: $0 [--debug]"
            exit 0
            ;;
    esac
done

failures=()

if ! bash "$SCRIPT_DIR/run-tests-cpp.sh" "$@"; then
    failures+=("C/C++")
fi

if ! bash "$SCRIPT_DIR/run-tests-python.sh"; then
    failures+=("Python")
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
