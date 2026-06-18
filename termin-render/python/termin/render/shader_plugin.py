"""Compatibility re-export for shader asset plugins.

Canonical module: :mod:`termin.default_assets.render.shader_plugin`.
"""

from termin.default_assets.render.shader_plugin import (
    ShaderAssetPlugin,
    ShaderImportPlugin,
    ShaderRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_shader_asset_plugin,
    register_shader_import_plugin,
    register_shader_runtime_plugin,
)

__all__ = [
    "ShaderAssetPlugin",
    "ShaderImportPlugin",
    "ShaderRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_shader_asset_plugin",
    "register_shader_import_plugin",
    "register_shader_runtime_plugin",
]
