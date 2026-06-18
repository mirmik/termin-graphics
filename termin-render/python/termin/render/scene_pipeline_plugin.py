"""Compatibility re-export for scene pipeline asset plugins.

Canonical module: :mod:`termin.default_assets.render.scene_pipeline_plugin`.
"""

from termin.default_assets.render.scene_pipeline_plugin import (
    ScenePipelineAssetPlugin,
    ScenePipelineImportPlugin,
    ScenePipelineRuntimePlugin,
    create_import_plugin,
    create_runtime_plugin,
    register_scene_pipeline_asset_plugin,
    register_scene_pipeline_import_plugin,
    register_scene_pipeline_runtime_plugin,
)

__all__ = [
    "ScenePipelineAssetPlugin",
    "ScenePipelineImportPlugin",
    "ScenePipelineRuntimePlugin",
    "create_import_plugin",
    "create_runtime_plugin",
    "register_scene_pipeline_asset_plugin",
    "register_scene_pipeline_import_plugin",
    "register_scene_pipeline_runtime_plugin",
]
