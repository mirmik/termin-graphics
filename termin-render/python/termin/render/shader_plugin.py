"""Shader asset plugins."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult


class ShaderImportPlugin:
    """Import-side plugin for shader source files."""

    type_id = "shader"
    extensions = {".shader"}
    priority = 0

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


class ShaderRuntimePlugin:
    """Runtime-side plugin for shader registration and reload."""

    type_id = "shader"

    def register(self, context: "AssetContext", result: "PreLoadResult") -> None:
        from termin.render.shader_asset import ShaderAsset

        rm = context.resource_manager
        name = context.name
        if name in rm._shader_assets:
            return

        asset = None
        if result.uuid:
            candidate = rm._assets_by_uuid.get(result.uuid)
            if isinstance(candidate, ShaderAsset):
                asset = candidate

        if asset is None:
            asset = ShaderAsset(
                program=None,
                name=name,
                source_path=result.path,
                uuid=result.uuid,
            )

        asset.parse_spec(result.spec_data)
        rm._assets_by_uuid[asset.uuid] = asset
        rm._shader_assets[name] = asset

    def reload(self, context: "AssetContext", result: "PreLoadResult") -> None:
        rm = context.resource_manager
        name = context.name
        asset = rm._shader_assets.get(name)
        if asset is None:
            return

        if not asset.is_loaded:
            return

        if not asset.should_reload_from_file():
            return

        old_program = asset.program

        asset.parse_spec(result.spec_data)
        asset.reload()

        if asset.program is None:
            return

        rm.shaders[name] = asset.program
        from termin.render.shader_interface import compare_shader_interface

        interface_change = compare_shader_interface(old_program, asset.program)
        if not interface_change.material_changed:
            return

        from termin.render.pipeline_dependencies import (
            refresh_loaded_materials_for_shader,
            reload_pipelines_for_material_dependencies,
        )

        material_names = refresh_loaded_materials_for_shader(rm, name, asset.uuid, asset.program)
        if material_names and interface_change.graph_inputs_changed:
            reload_pipelines_for_material_dependencies(rm, material_names)


class ShaderAssetPlugin(ShaderImportPlugin, ShaderRuntimePlugin):
    """Compatibility combined shader plugin."""


def register_shader_import_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_import(ShaderImportPlugin())


def register_shader_runtime_plugin(registry: "AssetTypeRegistry") -> None:
    registry.register_runtime(ShaderRuntimePlugin())


def register_shader_asset_plugin(registry: "AssetTypeRegistry") -> None:
    register_shader_import_plugin(registry)
    register_shader_runtime_plugin(registry)
