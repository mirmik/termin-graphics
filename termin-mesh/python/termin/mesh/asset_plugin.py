"""Compatibility re-export for mesh asset plugins.

Canonical module: :mod:`termin.default_assets.mesh.asset_plugin`.
"""

from termin.default_assets.mesh.asset_plugin import (
    MeshAssetPlugin,
    MeshImportPlugin,
    MeshRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_mesh_asset_plugin,
    register_mesh_import_plugin,
    register_mesh_runtime_plugin,
)

__all__ = [
    "MeshAssetPlugin",
    "MeshImportPlugin",
    "MeshRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_mesh_asset_plugin",
    "register_mesh_import_plugin",
    "register_mesh_runtime_plugin",
]
