#!/bin/bash
# Install the pinned Slang compiler and register it in common Termin settings.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CORE_SDK="${TERMIN_CORE_SDK:-}"
arguments=()
while (( $# > 0 )); do
    case "$1" in
        --core-sdk)
            if (( $# < 2 )); then
                echo "ERROR: --core-sdk requires a path" >&2
                exit 1
            fi
            CORE_SDK="$2"
            shift 2
            ;;
        --core-sdk=*)
            CORE_SDK="${1#--core-sdk=}"
            shift
            ;;
        *)
            arguments+=("$1")
            shift
            ;;
    esac
done

if [[ -n "${TERMIN_PYTHON:-}" ]]; then
    PYTHON_EXECUTABLE="$TERMIN_PYTHON"
elif [[ -n "$CORE_SDK" && "$CORE_SDK" == /* ]]; then
    PYTHON_EXECUTABLE="$CORE_SDK/bin/termin_python"
else
    echo "ERROR: pass the installed Core SDK with --core-sdk or set TERMIN_PYTHON" >&2
    exit 1
fi

if [[ ! -x "$PYTHON_EXECUTABLE" ]]; then
    echo "ERROR: Core SDK Python is missing: $PYTHON_EXECUTABLE" >&2
    exit 1
fi

exec "$PYTHON_EXECUTABLE" "$SCRIPT_DIR/scripts/install_slang_toolchain.py" "${arguments[@]}"
