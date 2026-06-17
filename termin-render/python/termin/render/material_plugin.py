"""Material asset plugins."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

from tcbase import log

if TYPE_CHECKING:
    from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult


class MaterialImportPlugin:
    """Import-side plugin for material files."""

    type_id = "material"
    extensions = {".material"}
    priority = 20

    def preload(self, path: str) -> "PreLoadResult | None":
        from termin_assets import PreLoadResult

        try:
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()

            uuid = None
            try:
                data = json.loads(content)
                uuid = data.get("uuid")
            except json.JSONDecodeError:
                log.debug(f"[MaterialImportPlugin] Failed to parse JSON for UUID extraction: {path}")

            return PreLoadResult(
                resource_type=self.type_id,
                path=path,
                content=content,
                uuid=uuid,
            )

        except Exception:
            log.error(f"[MaterialImportPlugin] Failed to read {path}", exc_info=True)
            return None


class MaterialRuntimePlugin:
    """Runtime-side plugin for material registration and reload."""

    type_id = "material"

    def register(self, context: "AssetContext", result: "PreLoadResult") -> None:
        from termin.render.material_asset import MaterialAsset

        rm = context.resource_manager
        name = context.name
        if name in rm._material_assets:
            return

        asset = None
        if result.uuid:
            candidate = rm._assets_by_uuid.get(result.uuid)
            if isinstance(candidate, MaterialAsset):
                asset = candidate

        if asset is None:
            asset = MaterialAsset(
                material=None,
                name=name,
                source_path=result.path,
                uuid=result.uuid,
            )
            rm._assets_by_uuid[asset.uuid] = asset

        rm._material_assets[name] = asset
        asset.load_from_content(result.content)

        if asset.material is not None:
            rm.materials[name] = asset.material

    def reload(self, context: "AssetContext", result: "PreLoadResult") -> None:
        rm = context.resource_manager
        name = context.name
        asset = rm._material_assets.get(name)
        if asset is None:
            return

        if not asset.is_loaded:
            return

        if not asset.should_reload_from_file():
            return

        asset.reload()

        if asset.material is not None:
            rm.materials[name] = asset.material


class MaterialAssetPlugin(MaterialImportPlugin, MaterialRuntimePlugin):
    """Compatibility combined material plugin."""


def register_material_import_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_import(MaterialImportPlugin())


def register_material_runtime_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_runtime(MaterialRuntimePlugin())


def register_material_asset_plugin(registry: "AssetTypeRegistry") -> None:
    register_material_import_plugin(registry)
    register_material_runtime_plugin(registry)
