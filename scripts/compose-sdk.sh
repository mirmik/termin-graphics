#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_SDK="${TERMIN_CORE_SDK:-}"
OUTPUT="$SCRIPT_DIR/sdk-complete"

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
        --core-sdk=*) CORE_SDK="${1#--core-sdk=}"; shift ;;
        --output)
            if (( $# < 2 )); then
                echo "ERROR: --output requires a path" >&2
                exit 1
            fi
            OUTPUT="$2"
            shift 2
            ;;
        --output=*) OUTPUT="${1#--output=}"; shift ;;
        --sdl|--no-sdl|--vulkan|--no-vulkan|--opengl|--no-opengl) shift ;;
        *)
            echo "ERROR: unsupported compose argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$CORE_SDK" || "$CORE_SDK" != /* ]]; then
    echo "ERROR: pass an absolute installed Core SDK path with --core-sdk" >&2
    exit 1
fi
if [[ "$OUTPUT" != /* ]]; then
    OUTPUT="$SCRIPT_DIR/$OUTPUT"
fi
if [[ ! -x "$CORE_SDK/bin/termin_python" ]]; then
    echo "ERROR: installed Core Python launcher is missing: $CORE_SDK/bin/termin_python" >&2
    exit 1
fi

exec "$CORE_SDK/bin/termin_python" -I -m termin_build.sdk \
    compose-sdk \
    --base-sdk "$CORE_SDK" \
    --layer-sdk "$SCRIPT_DIR/sdk" \
    --output "$OUTPUT"
