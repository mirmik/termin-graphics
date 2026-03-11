#!/bin/bash
# Run all repo tests: C/C++ first, then Python.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            echo "Usage: $0 [--debug]"
            exit 0
            ;;
    esac
done

bash "$SCRIPT_DIR/run-tests-cpp.sh" "$@"
bash "$SCRIPT_DIR/run-tests-python.sh"
