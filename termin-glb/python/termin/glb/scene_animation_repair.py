"""Scene data repairs for animation resource references."""

from __future__ import annotations

from typing import Any

from tcbase import log


def extract_scene_data(data: object) -> dict[str, Any] | None:
    """Extract the canonical scene object from supported scene file layouts."""
    if not isinstance(data, dict):
        return None

    scene_data = data.get("scene")
    if isinstance(scene_data, dict):
        return scene_data

    scenes = data.get("scenes")
    if isinstance(scenes, list) and scenes:
        first_scene = scenes[0]
        if isinstance(first_scene, dict):
            return first_scene

    if "entities" in data or "uuid" in data:
        return data

    return None


def repair_glb_animation_player_clip_refs(
    scene_data: dict[str, Any],
    resource_manager: Any | None = None,
) -> int:
    """Repair AnimationPlayer clip UUIDs using the sibling GLB skeleton owner.

    Older GLB imports registered all animation child assets by local names such
    as ``Walk``. When multiple character GLBs shared animation names, scenes
    could save valid but wrong clip UUIDs. The reliable owner is the sibling
    SkeletonController: its SkeletonAsset keeps the embedded parent GLB, whose
    animation child map is still keyed by local clip names.
    """
    if resource_manager is None:
        try:
            from termin_assets import get_resource_manager

            resource_manager = get_resource_manager()
        except Exception as exc:
            log.warn(f"[SceneAnimationRepair] ResourceManager factory unavailable: {exc}")
            return 0
        if resource_manager is None:
            log.warn("[SceneAnimationRepair] ResourceManager unavailable")
            return 0

    return _repair_entity_tree(scene_data.get("entities"), resource_manager)


def _repair_entity_tree(entities: object, resource_manager: Any) -> int:
    if not isinstance(entities, list):
        return 0

    repaired = 0
    for entity in entities:
        if not isinstance(entity, dict):
            continue
        repaired += _repair_entity(entity, resource_manager)
        repaired += _repair_entity_tree(entity.get("children"), resource_manager)
    return repaired


def _repair_entity(entity: dict[str, Any], resource_manager: Any) -> int:
    components = entity.get("components")
    if not isinstance(components, list):
        return 0

    skeleton_uuid = _entity_skeleton_uuid(components)
    if not skeleton_uuid:
        return 0

    glb_asset = _glb_asset_for_skeleton(resource_manager, skeleton_uuid)
    if glb_asset is None:
        return 0

    animation_assets = glb_asset.get_animation_assets()
    if not animation_assets:
        return 0

    repaired = 0
    for component in components:
        if not isinstance(component, dict) or component.get("type") != "AnimationPlayer":
            continue
        data = component.get("data")
        if not isinstance(data, dict):
            continue
        clips = data.get("clips")
        if not isinstance(clips, list):
            continue

        for clip in clips:
            if not isinstance(clip, dict):
                continue
            clip_name = clip.get("name")
            if not isinstance(clip_name, str) or not clip_name:
                continue
            animation_asset = animation_assets.get(clip_name)
            if animation_asset is None:
                continue
            expected_uuid = animation_asset.uuid
            current_uuid = clip.get("uuid")
            if current_uuid == expected_uuid:
                continue
            entity_name = entity.get("name")
            log.warn(
                "[SceneAnimationRepair] repaired AnimationPlayer clip "
                f"entity='{entity_name}' clip='{clip_name}' "
                f"uuid={current_uuid} -> {expected_uuid} glb='{glb_asset.name}'"
            )
            clip["uuid"] = expected_uuid
            clip["type"] = "uuid"
            repaired += 1

    return repaired


def _entity_skeleton_uuid(components: list[object]) -> str | None:
    for component in components:
        if not isinstance(component, dict) or component.get("type") != "SkeletonController":
            continue
        data = component.get("data")
        if not isinstance(data, dict):
            continue
        skeleton_ref = data.get("skeleton")
        if not isinstance(skeleton_ref, dict):
            continue
        skeleton_uuid = skeleton_ref.get("uuid")
        if isinstance(skeleton_uuid, str) and skeleton_uuid:
            return skeleton_uuid
    return None


def _glb_asset_for_skeleton(resource_manager: Any, skeleton_uuid: str) -> Any | None:
    skeleton_asset = resource_manager.get_skeleton_asset_by_uuid(skeleton_uuid)
    if skeleton_asset is None:
        return None

    parent = skeleton_asset.embedded_parent
    if parent is None:
        return None

    try:
        from termin.glb.asset import GLBAsset
    except Exception as exc:
        log.warn(f"[SceneAnimationRepair] GLBAsset unavailable: {exc}")
        return None

    if not isinstance(parent, GLBAsset):
        return None
    return parent


__all__ = ["extract_scene_data", "repair_glb_animation_player_clip_refs"]
