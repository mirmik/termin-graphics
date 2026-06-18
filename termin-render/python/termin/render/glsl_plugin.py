"""Compatibility re-export for GLSL asset plugins.

Canonical module: :mod:`termin.default_assets.render.glsl_plugin`.
"""

from termin.default_assets.render.glsl_plugin import (
    GlslAssetPlugin,
    GlslImportPlugin,
    GlslRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_glsl_asset_plugin,
    register_glsl_import_plugin,
    register_glsl_runtime_plugin,
)

__all__ = [
    "GlslAssetPlugin",
    "GlslImportPlugin",
    "GlslRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_glsl_asset_plugin",
    "register_glsl_import_plugin",
    "register_glsl_runtime_plugin",
]
