#!/bin/bash
# Run Python test suites across projects.
#
# By default, auto-activates .venv/ (if present) and auto-detects TERMIN_SDK,
# so no manual setup is needed after ./setup-test-venv.sh.
#
# Flags:
#   --no-venv    Don't auto-activate .venv; use PYTHON_BIN / system Python as-is
#   test paths   Run only selected pytest targets after environment setup
#   --help, -h   Show this help

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NO_VENV=0
PYTEST_TARGETS=()

for arg in "$@"; do
    case "$arg" in
        --no-venv) NO_VENV=1 ;;
        --help|-h)
            echo "Usage: $0 [--no-venv] [pytest-target ...]"
            echo ""
            echo "  (no flags)  Auto-activate .venv/ if present, auto-detect TERMIN_SDK"
            echo "  --no-venv   Skip auto-activation; use PYTHON_BIN or system Python"
            echo "  pytest-target"
            echo "              Run only selected pytest target(s), e.g. termin-app/tests/test_game_mode_model.py"
            exit 0
            ;;
        --*) echo "Unknown option: $arg" >&2; exit 1 ;;
        *) PYTEST_TARGETS+=("$arg") ;;
    esac
done

# --- Auto-activate venv ---
if [[ $NO_VENV -eq 0 && -f "$SCRIPT_DIR/.venv/bin/activate" ]]; then
    echo "Activating venv: $SCRIPT_DIR/.venv"
    source "$SCRIPT_DIR/.venv/bin/activate"
fi

# --- Python binary ---
if [[ -z "${PYTHON_BIN:-}" ]]; then
    PYTHON_BIN="$(command -v python3 || command -v python || true)"
fi
if [[ -z "${PYTHON_BIN:-}" ]]; then
    echo "python3 not found"
    exit 1
fi
echo "Python: $PYTHON_BIN"

# --- TERMIN_SDK ---
if [[ -z "${TERMIN_SDK:-}" ]]; then
    if [[ -d "$SCRIPT_DIR/sdk/lib" ]]; then
        export TERMIN_SDK="$SCRIPT_DIR/sdk"
    elif [[ -d "/opt/termin/lib" ]]; then
        export TERMIN_SDK="/opt/termin"
    fi
fi
if [[ -n "${TERMIN_SDK:-}" ]]; then
    echo "TERMIN_SDK: $TERMIN_SDK"
fi

# --- LD_LIBRARY_PATH ---
SDK_PREFIX="${SDK_PREFIX:-$SCRIPT_DIR/sdk}"
export LD_LIBRARY_PATH="${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# --- PYTHONPATH ---
# With venv + editable installs, all termin packages are already importable.
# Adding SDK package paths would override them with bundled SDK copies.
if [[ $NO_VENV -eq 0 && -d "$SCRIPT_DIR/.venv" ]]; then
    export PYTHONPATH="${PYTHONPATH:-}"
else
    BUNDLED_SITE_PACKAGES=""
    for site_dir in "$SDK_PREFIX"/lib/python3.*/site-packages; do
        if [[ -d "$site_dir" ]]; then
            BUNDLED_SITE_PACKAGES="$site_dir"
            break
        fi
    done
    if [[ -n "$BUNDLED_SITE_PACKAGES" ]]; then
        export PYTHONPATH="${BUNDLED_SITE_PACKAGES}:${SCRIPT_DIR}/termin-app/install/lib/python${PYTHONPATH:+:$PYTHONPATH}"
    else
        export PYTHONPATH="${SCRIPT_DIR}/termin-app/install/lib/python${PYTHONPATH:+:$PYTHONPATH}"
    fi
fi

# --- Run tests ---
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

if (( ${#PYTEST_TARGETS[@]} > 0 )); then
    run_suite "selected python" \
        "${PYTHON_BIN}" -m pytest "${PYTEST_TARGETS[@]}" -v
else
run_suite "termin-build-tools python" \
    "${PYTHON_BIN}" -m pytest termin-build-tools/tests/ -v

run_suite "termin-base python" \
    "${PYTHON_BIN}" -m pytest termin-base/tests/python/ -v

run_suite "termin-modules import smoke" \
    "${PYTHON_BIN}" -c "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())"

run_suite "termin-mesh python" \
    "${PYTHON_BIN}" -m pytest termin-mesh/tests/python/ -v

run_suite "termin-csg python" \
    "${PYTHON_BIN}" -m pytest termin-csg/tests/ -v

run_suite "termin-graphics python" \
    "${PYTHON_BIN}" -m pytest termin-graphics/tests/python/ -v

run_suite "termin-gui python" \
    "${PYTHON_BIN}" -m pytest termin-gui/python/tests/ -v

run_suite "termin-nodegraph python" \
    "${PYTHON_BIN}" -m pytest termin-nodegraph/tests/ -v

run_suite "termin-qopt python" \
    "${PYTHON_BIN}" -m pytest termin-qopt/tests/ -v

run_suite "termin-pga python" \
    "${PYTHON_BIN}" -m pytest termin-pga/tests/ -v

run_suite "termin-app python" \
    "${PYTHON_BIN}" -m pytest termin-app/tests/ -v
fi

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
