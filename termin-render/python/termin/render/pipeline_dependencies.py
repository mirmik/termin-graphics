"""Helpers for finding asset dependencies in pipeline graph JSON."""

from __future__ import annotations

from typing import Iterable

from tcbase import log


def material_pass_materials(graph_data: dict | None) -> set[str]:
    """Return material asset names referenced by MaterialPass nodes/passes."""
    if graph_data is None:
        return set()

    result: set[str] = set()

    nodes = graph_data.get("nodes", [])
    if isinstance(nodes, list):
        for node in nodes:
            if not isinstance(node, dict):
                continue
            node_type = str(node.get("type", ""))
            pass_class = str(node.get("pass_class", ""))
            graph_type = str(node.get("graph_type", ""))
            if node_type != "MaterialPass" and pass_class != "MaterialPass" and graph_type != "MaterialPass":
                continue
            params = node.get("params", {})
            if not isinstance(params, dict):
                continue
            material_name = params.get("material")
            if isinstance(material_name, str) and material_name:
                result.add(material_name)

    passes = graph_data.get("passes", [])
    if isinstance(passes, list):
        for pass_data in passes:
            if not isinstance(pass_data, dict):
                continue
            pass_type = str(pass_data.get("type", ""))
            pass_class = str(pass_data.get("pass_class", ""))
            if pass_type != "MaterialPass" and pass_class != "MaterialPass":
                continue

            for field_name in ("params", "data"):
                field_data = pass_data.get(field_name, {})
                if not isinstance(field_data, dict):
                    continue
                material_name = field_data.get("material")
                if isinstance(material_name, str) and material_name:
                    result.add(material_name)

    return result


def uses_material_names(graph_data: dict | None, material_names: Iterable[str]) -> bool:
    """Return true if graph_data references any material in material_names."""
    names = set(material_names)
    if not names:
        return False
    return bool(material_pass_materials(graph_data) & names)


def refresh_loaded_materials_for_shader(
    resource_manager,
    shader_name: str,
    shader_uuid: str,
    program,
) -> set[str]:
    """Rebuild loaded materials that use shader_name after shader hot-reload."""
    if program is None:
        return set()

    from termin.render.shader_asset import update_material_shader

    updated: set[str] = set()
    for material_name, material_asset in resource_manager._material_assets.items():
        if not material_asset.is_loaded:
            continue

        material = material_asset.material
        if material is None:
            continue
        if material.shader_name != shader_name:
            continue

        try:
            update_material_shader(material, program, shader_name, shader_uuid)
            material_asset._bump_version()
            resource_manager.materials[material_name] = material
            updated.add(material_name)
        except Exception:
            log.error(
                f"[ShaderAssetPlugin] Failed to refresh material '{material_name}' "
                f"after shader reload '{shader_name}'",
                exc_info=True,
            )

    if updated:
        names = ", ".join(sorted(updated))
        log.info(f"[ShaderAssetPlugin] Refreshed materials after shader reload '{shader_name}': {names}")

    return updated


def reload_pipelines_for_material_dependencies(resource_manager, material_names: set[str]) -> None:
    """Reload loaded pipeline assets whose MaterialPass nodes use material_names."""
    if not material_names:
        return

    reloaded_pipelines: list[str] = []
    for pipeline_name, pipeline_asset in resource_manager._pipeline_registry.assets.items():
        if not pipeline_asset.uses_material_names(material_names):
            continue
        if pipeline_asset.reload():
            reloaded_pipelines.append(pipeline_name)
        else:
            log.error(
                f"[ShaderAssetPlugin] Failed to reload pipeline '{pipeline_name}' "
                f"after material dependency update"
            )

    reloaded_scene_pipelines: list[str] = []
    for pipeline_name, pipeline_asset in resource_manager._scene_pipeline_registry.assets.items():
        if not pipeline_asset.uses_material_names(material_names):
            continue
        if pipeline_asset.reload():
            reloaded_scene_pipelines.append(pipeline_name)
        else:
            log.error(
                f"[ShaderAssetPlugin] Failed to reload scene pipeline '{pipeline_name}' "
                f"after material dependency update"
            )

    if reloaded_pipelines:
        names = ", ".join(sorted(reloaded_pipelines))
        log.info(f"[ShaderAssetPlugin] Reloaded material-dependent pipelines: {names}")
    if reloaded_scene_pipelines:
        names = ", ".join(sorted(reloaded_scene_pipelines))
        log.info(f"[ShaderAssetPlugin] Reloaded material-dependent scene pipelines: {names}")
