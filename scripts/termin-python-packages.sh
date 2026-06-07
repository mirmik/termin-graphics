#!/bin/bash
# Shared Python package list for SDK installs and wheelhouse export.
#
# The order is topological: each package appears after its local Termin
# dependencies. Keep this file as the single source of truth for shell scripts
# that install or build Termin Python packages.

TERMIN_PYTHON_PACKAGES=(
    termin-build-tools
    termin-nanobind-sdk
    termin-base
    termin-assets
    termin-mesh
    termin-graphics
    termin-materials
    termin-gui
    termin-inspect
    termin-scene
    termin-display
    termin-csg
    termin-modules
    termin-components/termin-components-kinematic
    termin-lighting
    termin-components/termin-components-mesh
    termin-input
    termin-collision
    termin-render
    termin-components/termin-components-render
    termin-components/termin-components-foliage
    termin-render-passes
    termin-navmesh
    termin-qopt
    termin-pga
    termin-physics
    termin-engine
    termin-skeleton
    termin-animation
    termin-nodegraph
    termin-app
    tcplot
)

termin_clear_python_package_build_caches() {
    local repo_root="$1"
    local pkg
    for pkg in "${TERMIN_PYTHON_PACKAGES[@]}"; do
        rm -rf "$repo_root/$pkg"/build/lib.* \
               "$repo_root/$pkg"/build/bdist.* \
               "$repo_root/$pkg"/*.egg-info 2>/dev/null || true
    done
}
