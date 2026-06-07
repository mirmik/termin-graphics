#!/bin/bash
# Shared Python package list for SDK installs and wheelhouse export.
#
# The order is topological: each package appears after its local Termin
# dependencies. Keep this file as the single source of truth for shell scripts
# that install or build Termin Python packages.

_TERMIN_PACKAGE_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_TERMIN_PACKAGE_REPO_ROOT="$(cd "$_TERMIN_PACKAGE_SCRIPT_DIR/.." && pwd)"
_TERMIN_PACKAGE_PYTHON="${PYTHON_BIN:-}"
if [[ -z "$_TERMIN_PACKAGE_PYTHON" ]]; then
    _TERMIN_PACKAGE_PYTHON="$(command -v python3 || command -v python || true)"
fi
if [[ -z "$_TERMIN_PACKAGE_PYTHON" ]]; then
    echo "ERROR: python3 not found; cannot load build-system/packages.json" >&2
    return 1 2>/dev/null || exit 1
fi

mapfile -t TERMIN_PYTHON_PACKAGES < <(
    PYTHONPATH="$_TERMIN_PACKAGE_REPO_ROOT/termin-build-tools${PYTHONPATH:+:$PYTHONPATH}" \
        "$_TERMIN_PACKAGE_PYTHON" -m termin_build.package_manifest \
        --repo-root "$_TERMIN_PACKAGE_REPO_ROOT" --list
)

unset _TERMIN_PACKAGE_SCRIPT_DIR
unset _TERMIN_PACKAGE_REPO_ROOT
unset _TERMIN_PACKAGE_PYTHON

termin_clear_python_package_build_caches() {
    local repo_root="$1"
    local pkg
    for pkg in "${TERMIN_PYTHON_PACKAGES[@]}"; do
        rm -rf "$repo_root/$pkg"/build/lib.* \
               "$repo_root/$pkg"/build/bdist.* \
               "$repo_root/$pkg"/*.egg-info 2>/dev/null || true
    done
}
