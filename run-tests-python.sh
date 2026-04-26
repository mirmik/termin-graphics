#!/bin/bash
# Run Python test suites across projects.
# Assumes SDK and Python packages are already installed, typically via:
#   ./build-sdk-bindings.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
PYTHON_BIN="${PYTHON_BIN:-$(command -v python3 || command -v python)}"

if [[ -z "${PYTHON_BIN}" ]]; then
    echo "python3 not found"
    exit 1
fi

export LD_LIBRARY_PATH="${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="${SDK_PREFIX}/lib/python:${SCRIPT_DIR}/termin-app/install/lib/python:${SCRIPT_DIR}/diffusion-editor${PYTHONPATH:+:$PYTHONPATH}"

echo ""
echo "========================================"
echo "  Python tests"
echo "========================================"

cd "$SCRIPT_DIR"

failures=()

run_suite() {
    local name="$1"
    shift

    echo ""
    echo "----------------------------------------"
    echo "  $name"
    echo "----------------------------------------"

    if ! "$@"; then
        failures+=("$name")
    fi
}

run_suite "termin-base python" \
    "${PYTHON_BIN}" -m pytest termin-base/tests/python/ -v

run_suite "termin-modules import smoke" \
    "${PYTHON_BIN}" -c "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())"

run_suite "termin-mesh python" \
    "${PYTHON_BIN}" -m pytest termin-mesh/tests/python/ -v

run_suite "termin-graphics python" \
    "${PYTHON_BIN}" -m pytest termin-graphics/tests/python/ -v

run_suite "termin-gui python" \
    "${PYTHON_BIN}" -m pytest termin-gui/python/tests/ -v

run_suite "termin-nodegraph python" \
    "${PYTHON_BIN}" -m pytest termin-nodegraph/tests/ -v

run_suite "termin-app python" \
    "${PYTHON_BIN}" -m pytest termin-app/tests/ -v

run_suite "diffusion-editor python" \
    "${PYTHON_BIN}" -m pytest diffusion-editor/tests/ -v

if (( ${#failures[@]} > 0 )); then
    echo ""
    echo "========================================"
    echo "  Python test failures"
    echo "========================================"
    printf '  - %s\n' "${failures[@]}"
    exit 1
fi

echo ""
echo "========================================"
echo "  Python tests finished"
echo "========================================"
