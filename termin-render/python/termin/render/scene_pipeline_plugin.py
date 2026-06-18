"""Scene pipeline asset plugins."""

from __future__ import annotations

from typing import TYPE_CHECKING

from tcbase import log

if TYPE_CHECKING:
    from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult


class ScenePipelineImportPlugin:
    """Import-side plugin for scene pipeline files."""

    type_id = "scene_pipeline"
    extensions = {".scene_pipeline"}
    priority = 5

    def preload(self, path: str) -> "PreLoadResult | None":
        from termin_assets import PreLoadResult, read_spec_file

        try:
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
        except Exception:
            log.error(f"[ScenePipelineImportPlugin] Failed to read {path}", exc_info=True)
            return None

        spec_data = read_spec_file(path)
        uuid = spec_data.get("uuid") if spec_data else None
        return PreLoadResult(
            resource_type=self.type_id,
            path=path,
            content=content,
            uuid=uuid,
            spec_data=spec_data,
        )


class ScenePipelineRuntimePlugin:
    """Runtime-side plugin for scene pipeline registration and reload."""

    type_id = "scene_pipeline"

    def register(self, context: "AssetContext", result: "PreLoadResult") -> None:
        from termin.render.scene_pipeline_asset import ScenePipelineAsset

        rm = context.resource_manager
        name = context.name
        uuid = result.uuid
        if uuid:
            asset = rm.get_runtime_asset_by_uuid(self.type_id, uuid)
            if isinstance(asset, ScenePipelineAsset):
                rm.register_runtime_asset(self.type_id, name, asset, source_path=result.path, uuid=uuid)
                if result.content:
                    asset.load_from_content(result.content, result.spec_data)
                return

        asset = rm.get_or_create_runtime_asset(
            self.type_id,
            name=name,
            source_path=result.path,
            uuid=uuid,
        )
        if result.content:
            asset.load_from_content(result.content, result.spec_data)

    def reload(self, context: "AssetContext", result: "PreLoadResult") -> None:
        rm = context.resource_manager
        asset = rm.get_runtime_asset(self.type_id, context.name)
        if asset is None:
            return

        if not asset.is_loaded:
            return

        if not asset.should_reload_from_file():
            return

        if result.content:
            asset.load_from_content(result.content, result.spec_data)
        else:
            asset.reload()


class ScenePipelineAssetPlugin(ScenePipelineImportPlugin, ScenePipelineRuntimePlugin):
    """Compatibility combined scene pipeline plugin."""


def register_scene_pipeline_import_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_import(ScenePipelineImportPlugin())


def register_scene_pipeline_runtime_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_runtime(ScenePipelineRuntimePlugin())


def register_scene_pipeline_asset_plugin(registry: "AssetTypeRegistry") -> None:
    register_scene_pipeline_import_plugin(registry)
    register_scene_pipeline_runtime_plugin(registry)
