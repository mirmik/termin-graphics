"""Compatibility re-export for render texture asset plugins.

Canonical module: :mod:`termin.default_assets.render.texture_plugin`.
"""

from termin.default_assets.render.texture_plugin import (
    TextureAssetPlugin,
    TextureImportPlugin,
    TextureRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_texture_asset_plugin,
    register_texture_import_plugin,
    register_texture_runtime_plugin,
)

__all__ = [
    "TextureAssetPlugin",
    "TextureImportPlugin",
    "TextureRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_texture_asset_plugin",
    "register_texture_import_plugin",
    "register_texture_runtime_plugin",
]
