#!/bin/bash
# Run Python test suites across projects.
#
# Uses the isolated bundled SDK Python plus a checkout-local source overlay.
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
            echo "--no-venv is no longer supported; run ./setup-sdk-python-env.sh first." >&2
            exit 1
            ;;
        --help|-h)
            echo "Usage: $0 [pytest-target ...]"
            echo ""
            echo "  (no flags)  Use SDK Python + checkout overlay and run working tests"
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

# --- TERMIN_SDK ---
if [[ -z "${TERMIN_SDK:-}" ]]; then
    if [[ -d "$SCRIPT_DIR/sdk/lib" ]]; then
        export TERMIN_SDK="$SCRIPT_DIR/sdk"
    elif [[ -d "/opt/termin/lib" ]]; then
        export TERMIN_SDK="/opt/termin"
    fi
fi
if [[ -z "${TERMIN_SDK:-}" ]]; then
    echo "ERROR: Termin SDK was not found." >&2
    exit 1
fi
echo "TERMIN_SDK: $TERMIN_SDK"

PYTHON_BIN="${PYTHON_BIN:-$TERMIN_SDK/bin/termin_python}"
OVERLAY_MANIFEST="${TERMIN_PYTHON_OVERLAY:-$SCRIPT_DIR/build/python-envs/test/overlay.json}"
if [[ ! -x "$PYTHON_BIN" ]]; then
    echo "ERROR: SDK Python launcher is missing: $PYTHON_BIN" >&2
    echo "Run ./build-sdk.sh --no-wheels first." >&2
    exit 1
fi
if [[ ! -f "$OVERLAY_MANIFEST" ]]; then
    echo "ERROR: Python test overlay is missing: $OVERLAY_MANIFEST" >&2
    echo "Run ./setup-sdk-python-env.sh first." >&2
    exit 1
fi
PYTHON_COMMAND=("$PYTHON_BIN" --termin-overlay "$OVERLAY_MANIFEST")
echo "Python: $PYTHON_BIN"
echo "Overlay: $OVERLAY_MANIFEST"

# --- LD_LIBRARY_PATH ---
SDK_PREFIX="${SDK_PREFIX:-$TERMIN_SDK}"
export LD_LIBRARY_PATH="${SDK_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# The launcher deliberately ignores ambient Python configuration.
unset PYTHONHOME PYTHONPATH PYTHONUSERBASE

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
        "${PYTHON_COMMAND[@]}" -m pytest "${PYTEST_MARK_ARGS[@]}" "${PYTEST_TARGETS[@]}" -v
else
    TEST_PROFILE="pr"
    if [[ "$FULL" -eq 1 ]]; then
        TEST_PROFILE="linux-full"
    fi

    PLANNER_PLAN_ARGS=()
    if [[ -n "${TERMIN_TEST_PLAN:-}" ]]; then
        PLANNER_PLAN_ARGS+=(--plan-file "${TERMIN_TEST_PLAN}")
    fi
    if [[ -n "${TERMIN_TEST_EXECUTION_MANIFEST:-}" ]]; then
        PLANNER_PLAN_ARGS+=(--report-output "${TERMIN_TEST_EXECUTION_MANIFEST}")
    fi
    if ! "${PYTHON_COMMAND[@]}" -m termin_build.repository_control \
        --repo-root "$SCRIPT_DIR" run "$TEST_PROFILE" \
        --platform linux --executor pytest --python "$PYTHON_BIN" \
        --python-arg=--termin-overlay --python-arg="$OVERLAY_MANIFEST" \
        "${PLANNER_PLAN_ARGS[@]}"; then
        failures+=("manifest Python suites")
    fi

run_suite "termin-modules import smoke" \
    "${PYTHON_COMMAND[@]}" -c "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())"

run_suite "Python lint" \
    "${PYTHON_COMMAND[@]}" -m ruff check "$SCRIPT_DIR"
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
