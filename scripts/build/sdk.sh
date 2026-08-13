#!/bin/bash
# Build the SDK through the shared Python orchestrator.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

CORE_SDK="${TERMIN_CORE_SDK:-}"
arguments=("$@")
for ((index=0; index<${#arguments[@]}; index++)); do
    case "${arguments[$index]}" in
        --core-sdk)
            if (( index + 1 >= ${#arguments[@]} )); then
                echo "ERROR: --core-sdk requires a path" >&2
                exit 1
            fi
            CORE_SDK="${arguments[$((index + 1))]}"
            ;;
        --core-sdk=*) CORE_SDK="${arguments[$index]#--core-sdk=}" ;;
        --profile|--profile=*)
            echo "ERROR: termin-graphics is one product and has no SDK profile switch" >&2
            exit 1
            ;;
    esac
done
if [[ -z "$CORE_SDK" || "$CORE_SDK" != /* ]]; then
    echo "ERROR: pass an absolute installed Core SDK path with --core-sdk" >&2
    exit 1
fi
export TERMIN_CORE_SDK="$CORE_SDK"
if [[ ! -x "$CORE_SDK/bin/termin_python" ]]; then
    echo "ERROR: installed Core Python launcher is missing: $CORE_SDK/bin/termin_python" >&2
    exit 1
fi
CORE_PYTHON_SITE="$CORE_SDK/lib/python3.14t/site-packages"
if [[ ! -d "$CORE_PYTHON_SITE/termin_build" ]]; then
    echo "ERROR: installed Core build frontend is missing: $CORE_PYTHON_SITE/termin_build" >&2
    exit 1
fi
export PYTHONPATH="$CORE_PYTHON_SITE${PYTHONPATH:+:$PYTHONPATH}"
export TERMIN_CORE_BUILD_FRONTEND="$CORE_PYTHON_SITE"

HOST_PYTHON="${TERMIN_BUILD_HOST_PYTHON:-}"
if [[ -z "$HOST_PYTHON" ]]; then
    HOST_PYTHON="$(command -v python3 || command -v python || true)"
fi
if [[ -z "$HOST_PYTHON" ]]; then
    echo "ERROR: host Python is required to prepare the pinned SDK toolchain" >&2
    exit 1
fi

exec "$HOST_PYTHON" -I "$SCRIPT_DIR/scripts/run-core-build-frontend.py" \
    --repo-root "$SCRIPT_DIR" build "$@"
