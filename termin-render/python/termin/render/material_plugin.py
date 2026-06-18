"""Compatibility re-export for material asset plugins.

Canonical module: :mod:`termin.default_assets.render.material_plugin`.
"""

from termin.default_assets.render.material_plugin import (
    MaterialAssetPlugin,
    MaterialImportPlugin,
    MaterialRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_material_asset_plugin,
    register_material_import_plugin,
    register_material_runtime_plugin,
)

__all__ = [
    "MaterialAssetPlugin",
    "MaterialImportPlugin",
    "MaterialRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_material_asset_plugin",
    "register_material_import_plugin",
    "register_material_runtime_plugin",
]
