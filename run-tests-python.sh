#!/bin/bash
# Run Python test suites across projects.
#
# Auto-activates .venv/ and auto-detects TERMIN_SDK, so no manual setup is
# needed after ./setup-test-venv.sh.
#
# Flags:
#   --full       Include pytest tests marked full
#   test paths   Run only selected pytest targets after environment setup;
#                selected runs skip the repo-wide Python lint suite.
#   --help, -h   Show this help

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTEST_TARGETS=()
FULL=0

for arg in "$@"; do
    case "$arg" in
        --full)
            FULL=1
            ;;
        --no-venv)
            echo "--no-venv is no longer supported; run ./setup-test-venv.sh first." >&2
            exit 1
            ;;
        --help|-h)
            echo "Usage: $0 [pytest-target ...]"
            echo ""
            echo "  (no flags)  Activate .venv/, auto-detect TERMIN_SDK, run working tests"
            echo "  --full      Include pytest tests marked full"
            echo "  pytest-target"
            echo "              Run only selected pytest target(s), e.g. termin-app/tests/test_game_mode_model.py"
            echo "              Selected runs skip the repo-wide Python lint suite."
            exit 0
            ;;
        --*) echo "Unknown option: $arg" >&2; exit 1 ;;
        *) PYTEST_TARGETS+=("$arg") ;;
    esac
done

# --- Activate venv ---
if [[ ! -f "$SCRIPT_DIR/.venv/bin/activate" ]]; then
    echo "ERROR: test .venv is missing." >&2
    echo "Run ./setup-test-venv.sh before ./run-tests-python.sh." >&2
    exit 1
fi
echo "Activating venv: $SCRIPT_DIR/.venv"
source "$SCRIPT_DIR/.venv/bin/activate"

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
# Editable installs in .venv are the source of truth for Python tests. Do not
# prepend SDK site-packages here: stale installed SDK packages can shadow the
# checkout and hide source changes.
export PYTHONPATH="${PYTHONPATH:-}"

# --- Run tests ---
echo ""
echo "========================================"
if [[ "$FULL" -eq 1 ]]; then
    echo "  Python tests (full)"
else
    echo "  Python tests (working set)"
fi
echo "========================================"

cd "$SCRIPT_DIR"

failures=()
PYTEST_MARK_ARGS=()
if [[ "$FULL" -eq 0 ]]; then
    PYTEST_MARK_ARGS=(-m "not full")
fi

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
        "${PYTHON_BIN}" -m pytest "${PYTEST_MARK_ARGS[@]}" "${PYTEST_TARGETS[@]}" -v
else
    TEST_PROFILE="pr"
    if [[ "$FULL" -eq 1 ]]; then
        TEST_PROFILE="linux-full"
    fi

    if ! PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
        "${PYTHON_BIN}" -m termin_build.repository_control \
        --repo-root "$SCRIPT_DIR" run "$TEST_PROFILE" \
        --platform linux --python "$PYTHON_BIN"; then
        failures+=("manifest Python suites")
    fi

run_suite "termin-modules import smoke" \
    "${PYTHON_BIN}" -c "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())"

run_suite "Python lint" \
    bash "$SCRIPT_DIR/run-lint-python.sh"
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
