"""Scene data repairs for GLB-owned animation players."""

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
    """Repair GLB AnimationPlayer references using the sibling skeleton owner.

    Older GLB imports registered all animation child assets by local names such
    as ``Walk``. When multiple character GLBs shared animation names, scenes
    could save valid but wrong clip UUIDs. The reliable owner is the sibling
    SkeletonController: its SkeletonAsset keeps the embedded parent GLB, whose
    animation child map is still keyed by local clip names.

    Native animation tracks address targets by exact glTF node index. Scenes
    saved before that contract have an absent or empty ``node_targets`` table.
    It is reconstructed from the serialized entity hierarchy, anchored by the
    exact skin joint order in ``SkeletonController.bone_entities``.
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

    skeleton_data = _entity_skeleton_data(components)
    if skeleton_data is None:
        return 0
    skeleton_uuid = _skeleton_uuid(skeleton_data)
    if skeleton_uuid is None:
        return 0

    glb_asset = _glb_asset_for_skeleton(resource_manager, skeleton_uuid)
    if glb_asset is None:
        return 0

    repaired = 0
    for component in components:
        if not isinstance(component, dict) or component.get("type") != "AnimationPlayer":
            continue
        data = component.get("data")
        if not isinstance(data, dict):
            continue
        repaired += int(
            _repair_node_targets(
                entity,
                data,
                skeleton_data,
                glb_asset,
                skeleton_uuid,
            )
        )

        animation_assets = glb_asset.get_animation_assets()
        if not animation_assets:
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


def _entity_skeleton_data(components: list[object]) -> dict[str, Any] | None:
    for component in components:
        if not isinstance(component, dict) or component.get("type") != "SkeletonController":
            continue
        data = component.get("data")
        if isinstance(data, dict):
            return data
    return None


def _skeleton_uuid(skeleton_data: dict[str, Any]) -> str | None:
    skeleton_ref = skeleton_data.get("skeleton")
    if not isinstance(skeleton_ref, dict):
        return None
    skeleton_uuid = skeleton_ref.get("uuid")
    if isinstance(skeleton_uuid, str) and skeleton_uuid:
        return skeleton_uuid
    return None


def _repair_node_targets(
    entity: dict[str, Any],
    player_data: dict[str, Any],
    skeleton_data: dict[str, Any],
    glb_asset: Any,
    skeleton_uuid: str,
) -> bool:
    current_targets = player_data.get("node_targets")
    if isinstance(current_targets, list) and current_targets:
        return False
    if current_targets is not None and not isinstance(current_targets, list):
        _log_target_repair_error(entity, glb_asset, "node_targets is not a list")
        return False

    bone_refs = skeleton_data.get("bone_entities")
    if not isinstance(bone_refs, list) or not bone_refs:
        _log_target_repair_error(
            entity,
            glb_asset,
            "SkeletonController has no ordered bone_entities anchors",
        )
        return False

    try:
        if not glb_asset.ensure_loaded():
            raise ValueError("GLB asset could not be loaded")
        scene_data = glb_asset.cached_data
        if scene_data is None:
            raise ValueError("GLB asset has no loaded scene data")
        skin_index = _skin_index_for_skeleton(glb_asset, skeleton_uuid)
        if not 0 <= skin_index < len(scene_data.skins):
            raise ValueError(
                f"skin index {skin_index} is outside loaded skin count {len(scene_data.skins)}"
            )
        skin = scene_data.skins[skin_index]
        target_entities = _reconstruct_node_targets(
            entity,
            scene_data.nodes,
            scene_data.root_nodes,
            skin.joint_node_indices,
            bone_refs,
        )
    except Exception as exc:
        _log_target_repair_error(entity, glb_asset, str(exc))
        return False

    player_data["node_targets"] = [
        {} if target is None else {"uuid": target["uuid"]}
        for target in target_entities
    ]
    mapped_count = sum(target is not None for target in target_entities)
    missing = [
        f"{index}:{scene_data.nodes[index].name!r}"
        for index, target in enumerate(target_entities)
        if target is None
    ]
    missing_summary = ""
    if missing:
        displayed = missing[:8]
        suffix = "..." if len(missing) > len(displayed) else ""
        missing_summary = f" missing=[{', '.join(displayed)}{suffix}]"
    log.warn(
        "[SceneAnimationRepair] reconstructed AnimationPlayer node_targets "
        f"entity='{entity.get('name')}' mapped={mapped_count}/{len(target_entities)} "
        f"glb='{glb_asset.name}'{missing_summary}"
    )
    return True


def _skin_index_for_skeleton(glb_asset: Any, skeleton_uuid: str) -> int:
    matches: list[int] = []
    for key, asset in glb_asset.get_skeleton_assets().items():
        if asset.uuid != skeleton_uuid:
            continue
        if key == "skeleton":
            matches.append(0)
            continue
        prefix = "skeleton_"
        suffix = key[len(prefix) :] if key.startswith(prefix) else ""
        if not suffix.isdigit():
            raise ValueError(f"invalid GLB skeleton resource key {key!r}")
        matches.append(int(suffix))
    if len(matches) != 1:
        raise ValueError(
            f"expected one GLB skin for skeleton UUID {skeleton_uuid!r}, found {len(matches)}"
        )
    return matches[0]


def _reconstruct_node_targets(
    wrapper: dict[str, Any],
    nodes: list[Any],
    root_nodes: list[int],
    joint_node_indices: list[int],
    bone_refs: list[object],
) -> list[dict[str, Any] | None]:
    if len(bone_refs) != len(joint_node_indices):
        raise ValueError(
            "bone_entities count does not match skin joint count: "
            f"{len(bone_refs)} != {len(joint_node_indices)}"
        )

    wrapper_uuid = _required_entity_uuid(wrapper, "GLB wrapper")
    entities_by_uuid: dict[str, dict[str, Any]] = {}
    parents_by_uuid: dict[str, str] = {}
    _index_serialized_children(
        wrapper,
        wrapper_uuid,
        entities_by_uuid,
        parents_by_uuid,
    )
    node_parents = _node_parent_indices(nodes)
    reachable = _reachable_node_indices(nodes, root_nodes)

    targets: list[dict[str, Any] | None] = [None] * len(nodes)
    entity_to_node: dict[str, int] = {}

    def bind(node_index: int, serialized: dict[str, Any], reason: str) -> None:
        if not 0 <= node_index < len(nodes):
            raise ValueError(f"{reason} node index {node_index} is out of range")
        entity_uuid = _required_entity_uuid(serialized, reason)
        expected_name = nodes[node_index].name
        actual_name = serialized.get("name")
        if actual_name != expected_name:
            raise ValueError(
                f"{reason} name mismatch for node {node_index}: "
                f"GLB={expected_name!r}, scene={actual_name!r}"
            )
        existing = targets[node_index]
        if existing is not None and existing is not serialized:
            raise ValueError(f"conflicting scene entities for GLB node {node_index}")
        existing_node = entity_to_node.get(entity_uuid)
        if existing_node is not None and existing_node != node_index:
            raise ValueError(
                f"scene entity {entity_uuid!r} maps to GLB nodes "
                f"{existing_node} and {node_index}"
            )
        targets[node_index] = serialized
        entity_to_node[entity_uuid] = node_index

    for joint_offset, node_index in enumerate(joint_node_indices):
        bone_ref = bone_refs[joint_offset]
        if not isinstance(bone_ref, dict):
            raise ValueError(f"bone_entities[{joint_offset}] is not an entity reference")
        bone_uuid = bone_ref.get("uuid")
        if not isinstance(bone_uuid, str) or not bone_uuid:
            raise ValueError(f"bone_entities[{joint_offset}] has no UUID")
        bone_entity = entities_by_uuid.get(bone_uuid)
        if bone_entity is None:
            raise ValueError(
                f"bone_entities[{joint_offset}] UUID {bone_uuid!r} is not below the GLB wrapper"
            )
        bind(node_index, bone_entity, f"bone_entities[{joint_offset}]")

    # Exact joint UUIDs anchor both trees. Walk every anchor upwards, binding
    # serialized parents to matching glTF ancestors. Very old imports omitted
    # structural nodes such as ``Armature`` and promoted their children; those
    # skipped glTF indices deliberately remain empty in the target table.
    for joint_index in joint_node_indices:
        node_index = joint_index
        serialized = targets[node_index]
        if serialized is None:
            raise ValueError(f"joint node {node_index} has no scene anchor")
        while True:
            serialized_uuid = _required_entity_uuid(serialized, "anchored entity")
            serialized_parent_uuid = parents_by_uuid.get(serialized_uuid)
            parent_index = node_parents[node_index]
            if serialized_parent_uuid == wrapper_uuid:
                break
            if serialized_parent_uuid is None:
                raise ValueError(
                    f"serialized hierarchy ends before GLB parent of node {node_index}"
                )
            serialized_parent = entities_by_uuid[serialized_parent_uuid]
            serialized_parent_name = serialized_parent.get("name")
            while (
                parent_index is not None
                and nodes[parent_index].name != serialized_parent_name
            ):
                parent_index = node_parents[parent_index]
            if parent_index is None:
                raise ValueError(
                    f"serialized parent {serialized_parent_name!r} of node {node_index} "
                    "has no matching GLB ancestor"
                )
            bind(parent_index, serialized_parent, f"ancestor of node {node_index}")
            node_index = parent_index
            serialized = serialized_parent

    wrapper_children = _serialized_children(wrapper)
    for root_index in root_nodes:
        _map_node_subtree(
            root_index,
            wrapper,
            wrapper_children,
            nodes,
            targets,
            entity_to_node,
            bind,
        )
    if not any(targets[index] is not None for index in reachable):
        raise ValueError("no reachable GLB nodes could be mapped to serialized entities")
    return targets


def _map_node_subtree(
    node_index: int,
    serialized_parent: dict[str, Any],
    serialized_children: list[dict[str, Any]],
    nodes: list[Any],
    targets: list[dict[str, Any] | None],
    entity_to_node: dict[str, int],
    bind: Any,
) -> int:
    existing = targets[node_index]
    if existing is not None:
        if not any(child is existing for child in serialized_children):
            raise ValueError(
                f"mapped node {node_index} is not below its expected serialized parent"
            )
        mapped_entity = existing
        mapped_here = 1
    else:
        node_name = nodes[node_index].name
        candidates = []
        for child in serialized_children:
            child_uuid = _required_entity_uuid(
                child, f"child of {serialized_parent.get('name')!r}"
            )
            if child_uuid in entity_to_node:
                continue
            if child.get("name") == node_name:
                candidates.append(child)
        if len(candidates) > 1:
            raise ValueError(
                f"expected at most one unused child named {node_name!r} for GLB "
                f"node {node_index}, found {len(candidates)}"
            )
        if candidates:
            bind(node_index, candidates[0], f"child node {node_index}")
            mapped_entity = candidates[0]
            mapped_here = 1
        else:
            mapped_entity = serialized_parent
            mapped_here = 0

    child_parent = mapped_entity
    child_entities = _serialized_children(child_parent)
    mapped_descendants = 0
    for child_index in nodes[node_index].children:
        mapped_descendants += _map_node_subtree(
            child_index,
            child_parent,
            child_entities,
            nodes,
            targets,
            entity_to_node,
            bind,
        )
    return mapped_here + mapped_descendants


def _node_parent_indices(nodes: list[Any]) -> list[int | None]:
    parents: list[int | None] = [None] * len(nodes)
    for parent_index, node in enumerate(nodes):
        for child_index in node.children:
            if not isinstance(child_index, int) or not 0 <= child_index < len(nodes):
                raise ValueError(
                    f"node {parent_index} has invalid child index {child_index!r}"
                )
            if parents[child_index] is not None:
                raise ValueError(f"GLB node {child_index} has multiple parents")
            parents[child_index] = parent_index
    return parents


def _reachable_node_indices(nodes: list[Any], root_nodes: list[int]) -> set[int]:
    reachable: set[int] = set()
    visiting: set[int] = set()

    def visit(node_index: int) -> None:
        if not isinstance(node_index, int) or not 0 <= node_index < len(nodes):
            raise ValueError(f"invalid root or child node index {node_index!r}")
        if node_index in visiting:
            raise ValueError(f"cycle detected at GLB node {node_index}")
        if node_index in reachable:
            return
        visiting.add(node_index)
        for child_index in nodes[node_index].children:
            visit(child_index)
        visiting.remove(node_index)
        reachable.add(node_index)

    for root_index in root_nodes:
        visit(root_index)
    return reachable


def _index_serialized_children(
    parent: dict[str, Any],
    parent_uuid: str,
    entities_by_uuid: dict[str, dict[str, Any]],
    parents_by_uuid: dict[str, str],
) -> None:
    for child in _serialized_children(parent):
        child_uuid = _required_entity_uuid(child, "serialized child entity")
        if child_uuid in entities_by_uuid:
            raise ValueError(f"duplicate serialized entity UUID {child_uuid!r}")
        entities_by_uuid[child_uuid] = child
        parents_by_uuid[child_uuid] = parent_uuid
        _index_serialized_children(child, child_uuid, entities_by_uuid, parents_by_uuid)


def _serialized_children(entity: dict[str, Any]) -> list[dict[str, Any]]:
    children = entity.get("children")
    if children is None:
        return []
    if not isinstance(children, list) or any(not isinstance(child, dict) for child in children):
        raise ValueError(f"entity {entity.get('name')!r} has malformed children")
    return children


def _required_entity_uuid(entity: dict[str, Any], context: str) -> str:
    entity_uuid = entity.get("uuid")
    if not isinstance(entity_uuid, str) or not entity_uuid:
        raise ValueError(f"{context} has no UUID")
    return entity_uuid


def _log_target_repair_error(entity: dict[str, Any], glb_asset: Any, reason: str) -> None:
    log.error(
        "[SceneAnimationRepair] failed to reconstruct AnimationPlayer node_targets "
        f"entity='{entity.get('name')}' glb='{glb_asset.name}': {reason}"
    )


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
