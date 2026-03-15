#!/bin/bash
# Run Python test suites across projects.
# Assumes SDK and Python packages are already installed, typically via:
#   ./build-sdk-bindings.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
PYTHON_BIN="${PYTHON_BIN:-$(command -v python3 || command -v python)}"

if [[ -z "${PYTHON_BIN}" ]]; then
    echo "python3 not found"
    exit 1
fi

export LD_LIBRARY_PATH="${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="${SDK_PREFIX}/lib/python:${SCRIPT_DIR}/termin/install/lib/python:${SCRIPT_DIR}/diffusion-editor${PYTHONPATH:+:$PYTHONPATH}"

echo ""
echo "========================================"
echo "  Python tests"
echo "========================================"

cd "$SCRIPT_DIR"

"${PYTHON_BIN}" -m pytest termin-base/tests/python/ -v
"${PYTHON_BIN}" -c "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())"
"${PYTHON_BIN}" -m pytest termin-graphics/tests/python/ -v
"${PYTHON_BIN}" -m pytest termin-gui/python/tests/ -v
"${PYTHON_BIN}" -m pytest termin-nodegraph/tests/ -v
"${PYTHON_BIN}" -m pytest termin/tests/ -v
"${PYTHON_BIN}" -m pytest diffusion-editor/tests/ -v

echo ""
echo "========================================"
echo "  Python tests finished"
echo "========================================"
