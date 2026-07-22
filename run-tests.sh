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

TEST_BUILD_TYPE="Release"
for arg in "${CPP_ARGS[@]}"; do
    if [[ "$arg" == "--debug" || "$arg" == "-d" ]]; then
        TEST_BUILD_TYPE="Debug"
    fi
done
TEST_SHADERC="${BUILD_DIR:-$SCRIPT_DIR/build/$TEST_BUILD_TYPE-tests}/bin/termin_shaderc"
if [[ ! -x "$TEST_SHADERC" ]]; then
    echo "ERROR: test-built termin_shaderc is missing: $TEST_SHADERC" >&2
    failures+=("termin_shaderc provenance")
elif ! TERMIN_SHADERC="$TEST_SHADERC" bash "$SCRIPT_DIR/run-tests-python.sh" "${PYTHON_ARGS[@]}"; then
    failures+=("Python")
fi

if [[ "$FULL" -eq 1 && "$NO_EDITOR_SMOKE" -eq 0 ]]; then
    echo ""
    echo "========================================"
    echo "  Editor smoke tests"
    echo "========================================"

    PROCESS_SMOKE_ROOT="$SCRIPT_DIR/build/process-smoke/editor-smoke"
    PROCESS_SMOKE_PLAN="$PROCESS_SMOKE_ROOT/expected.json"
    PROCESS_SMOKE_REPORT="$PROCESS_SMOKE_ROOT/execution-manifest.json"
    mkdir -p "$PROCESS_SMOKE_ROOT"
    PROCESS_PYTHON="$(command -v python3 || command -v python || true)"
    if [[ -z "$PROCESS_PYTHON" ]]; then
        echo "ERROR: Python is required for process-smoke repository control" >&2
        failures+=("Editor smoke")
    elif ! PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
        "$PROCESS_PYTHON" -m termin_build.repository_control \
            --repo-root "$SCRIPT_DIR" plan editor-smoke --platform linux --json \
            > "$PROCESS_SMOKE_PLAN"; then
        failures+=("Editor smoke plan")
    else
        PROCESS_EXIT=0
        PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
            "$PROCESS_PYTHON" -m termin_build.repository_control \
                --repo-root "$SCRIPT_DIR" run editor-smoke \
                --platform linux \
                --executor process-smoke \
                --capability editor \
                --configuration "$TEST_BUILD_TYPE" \
                --process-log-dir "$PROCESS_SMOKE_ROOT/logs" \
                --report-output "$PROCESS_SMOKE_REPORT" || PROCESS_EXIT=$?
        VERIFY_EXIT=0
        PYTHONPATH="$SCRIPT_DIR/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
            "$PROCESS_PYTHON" -m termin_build.repository_control \
                --repo-root "$SCRIPT_DIR" verify-suite-execution \
                --plan "$PROCESS_SMOKE_PLAN" \
                --manifest "$PROCESS_SMOKE_REPORT" \
                --executor process-smoke || VERIFY_EXIT=$?
        if [[ "$PROCESS_EXIT" -ne 0 || "$VERIFY_EXIT" -ne 0 ]]; then
            failures+=("Editor smoke")
        fi
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
