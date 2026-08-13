#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CORE_SDK="${TERMIN_CORE_SDK:-}"
arguments=("$@")
for ((index=0; index<${#arguments[@]}; index++)); do
    case "${arguments[$index]}" in
        --core-sdk) CORE_SDK="${arguments[$((index + 1))]:-}" ;;
        --core-sdk=*) CORE_SDK="${arguments[$index]#--core-sdk=}" ;;
    esac
done
if [[ -z "$CORE_SDK" || "$CORE_SDK" != /* ]]; then
    echo "ERROR: pass an absolute installed Core SDK path with --core-sdk" >&2
    exit 1
fi

cmake -S "$SCRIPT_DIR/termin-render-core" -B "$SCRIPT_DIR/build/test/render-core" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$SCRIPT_DIR/sdk;$CORE_SDK" \
    -DTERMIN_RENDER_CORE_BUILD_TESTS=ON
cmake --build "$SCRIPT_DIR/build/test/render-core" --parallel
LD_LIBRARY_PATH="$SCRIPT_DIR/sdk/lib:$CORE_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ctest --test-dir "$SCRIPT_DIR/build/test/render-core" --output-on-failure
