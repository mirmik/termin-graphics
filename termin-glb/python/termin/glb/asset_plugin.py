"""GLB asset plugins."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult


class GLBImportPlugin:
    """Import-side plugin for GLB/GLTF model files."""

    type_id = "glb"
    extensions = {".glb", ".gltf"}
    priority = 10

    def preload(self, path: str) -> "PreLoadResult | None":
        from termin_assets import PreLoadResult, read_spec_file

        spec_data = read_spec_file(path)
        uuid = spec_data.get("uuid") if spec_data else None
        return PreLoadResult(
            resource_type=self.type_id,
            path=path,
            content=None,
            uuid=uuid,
            spec_data=spec_data,
        )


class GLBRuntimePlugin:
    """Runtime-side plugin for GLB registration and reload."""

    type_id = "glb"

    def register(self, context: "AssetContext", result: "PreLoadResult") -> None:
        from termin.glb.asset import GLBAsset

        rm = context.resource_manager
        name = context.name
        asset = None
        candidate = rm.get_glb_asset_by_uuid(context.uuid)
        if isinstance(candidate, GLBAsset):
            asset = candidate

        if asset is None:
            asset = GLBAsset(
                scene_data=None,
                name=name,
                source_path=result.path,
                uuid=context.uuid,
            )

        asset.set_resource_manager(rm)
        asset.parse_spec(result.spec_data)
        rm.register_glb_asset(name, asset, source_path=result.path)

    def reload(self, context: "AssetContext", result: "PreLoadResult") -> None:
        rm = context.resource_manager
        asset = rm.get_runtime_asset_by_uuid(self.type_id, context.uuid)
        if asset is None:
            return

        if not asset.is_loaded:
            return

        if not asset.should_reload_from_file():
            return

        asset.parse_spec(result.spec_data)
        asset.reload()

    def unregister(self, context: "AssetContext", result: "PreLoadResult") -> None:
        context.resource_manager.unregister_runtime_asset_by_uuid(self.type_id, context.uuid)


class GLBAssetPlugin(GLBImportPlugin, GLBRuntimePlugin):
    """Compatibility combined GLB plugin."""


def create_import_plugin() -> GLBImportPlugin:
    return GLBImportPlugin()


def create_runtime_plugin() -> GLBRuntimePlugin:
    return GLBRuntimePlugin()


def register_glb_import_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_import(GLBImportPlugin())


def register_glb_runtime_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_runtime(GLBRuntimePlugin())


def register_glb_asset_plugin(registry: "AssetTypeRegistry") -> None:
    register_glb_import_plugin(registry)
    register_glb_runtime_plugin(registry)
