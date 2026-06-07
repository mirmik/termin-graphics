#!/bin/bash
# Ensure required third-party submodules are initialized before configuring CMake.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <submodule-path>..." >&2
    exit 2
fi

expected_files_for() {
    case "$1" in
        termin-thirdparty/manifold)
            printf '%s\n' "CMakeLists.txt"
            ;;
        termin-thirdparty/clipper2)
            printf '%s\n' "CPP/CMakeLists.txt"
            ;;
        termin-thirdparty/guard)
            printf '%s\n' "guard_c.h" "guard_main.h"
            ;;
        termin-thirdparty/vulkan-memory-allocator)
            printf '%s\n' "include/vk_mem_alloc.h"
            ;;
        termin-thirdparty/openxr-sdk)
            printf '%s\n' "include/openxr/openxr.h"
            ;;
        termin-thirdparty/recastnavigation)
            printf '%s\n' "Recast/CMakeLists.txt" "Detour/CMakeLists.txt"
            ;;
    esac
}

submodule_ready() {
    local relative_path="$1"
    local full_path="$REPO_ROOT/$relative_path"
    local expected=()
    local expected_file

    [[ -d "$full_path" ]] || return 1

    mapfile -t expected < <(expected_files_for "$relative_path")
    if [[ ${#expected[@]} -gt 0 ]]; then
        for expected_file in "${expected[@]}"; do
            [[ -e "$full_path/$expected_file" ]] || return 1
        done
        return 0
    fi

    find "$full_path" -mindepth 1 -maxdepth 1 -print -quit | grep -q .
}

missing=()
for relative_path in "$@"; do
    relative_path="${relative_path//\\//}"
    if ! submodule_ready "$relative_path"; then
        missing+=("$relative_path")
    fi
done

if [[ ${#missing[@]} -eq 0 ]]; then
    exit 0
fi

if ! command -v git >/dev/null 2>&1; then
    echo "ERROR: required git submodules are missing and git was not found:" >&2
    printf '  - %s\n' "${missing[@]}" >&2
    exit 1
fi

echo "Initializing missing third-party submodules:"
printf '  - %s\n' "${missing[@]}"

git -C "$REPO_ROOT" submodule update --init --recursive -- "${missing[@]}"

still_missing=()
for relative_path in "${missing[@]}"; do
    if ! submodule_ready "$relative_path"; then
        still_missing+=("$relative_path")
    fi
done

if [[ ${#still_missing[@]} -gt 0 ]]; then
    echo "ERROR: required git submodules are still missing after initialization:" >&2
    printf '  - %s\n' "${still_missing[@]}" >&2
    exit 1
fi
