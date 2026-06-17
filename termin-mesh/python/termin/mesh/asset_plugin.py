"""Mesh asset plugin."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult


class MeshImportPlugin:
    """Import-side plugin for standalone mesh source files."""

    type_id = "mesh"
    extensions = {".stl", ".obj"}
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


class MeshRuntimePlugin:
    """Runtime-side plugin handling standalone mesh asset registration and reload."""

    type_id = "mesh"

    def register(self, context: "AssetContext", result: "PreLoadResult") -> None:
        from termin.mesh.asset import MeshAsset

        rm = context.resource_manager
        name = context.name
        if rm.get_runtime_asset(self.type_id, name) is not None:
            return

        asset = None
        if result.uuid:
            candidate = rm.get_runtime_asset_by_uuid(self.type_id, result.uuid)
            if isinstance(candidate, MeshAsset):
                asset = candidate

        if asset is None:
            asset = MeshAsset(
                mesh_data=None,
                name=name,
                source_path=result.path,
                uuid=result.uuid,
            )

        asset.parse_spec(result.spec_data)
        rm.register_runtime_asset(self.type_id, name, asset, source_path=result.path, uuid=result.uuid)

    def reload(self, context: "AssetContext", result: "PreLoadResult") -> None:
        rm = context.resource_manager
        asset = rm.get_runtime_asset(self.type_id, context.name)
        if asset is None:
            return

        if not asset.is_loaded:
            return

        if not asset.should_reload_from_file():
            return

        asset.parse_spec(result.spec_data)
        asset.reload()
        asset.delete_gpu()


class MeshAssetPlugin(MeshImportPlugin, MeshRuntimePlugin):
    """Compatibility combined mesh plugin."""


def create_import_plugin() -> MeshImportPlugin:
    return MeshImportPlugin()


def create_runtime_plugin() -> MeshRuntimePlugin:
    return MeshRuntimePlugin()


def register_mesh_import_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_import(MeshImportPlugin())


def register_mesh_runtime_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_runtime(MeshRuntimePlugin())


def register_mesh_asset_plugin(registry: "AssetTypeRegistry") -> None:
    register_mesh_import_plugin(registry)
    register_mesh_runtime_plugin(registry)
