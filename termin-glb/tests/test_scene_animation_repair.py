from types import SimpleNamespace

from termin.animation.asset import AnimationClipAsset
from termin.glb.asset import GLBAsset
from termin.glb.scene_animation_repair import repair_glb_animation_player_clip_refs
from termin.skeleton.asset import SkeletonAsset
from termin_assets import AssetRegistry, AssetRuntimeManager, set_resource_manager_factory


class _ResourceManager(AssetRuntimeManager):
    def __init__(self) -> None:
        super().__init__()
        self._skeleton_assets = AssetRegistry(
            asset_class=SkeletonAsset,
            asset_store=self._asset_store,
            data_from_asset=lambda asset: asset.cached_data,
        )
        self._animation_assets = AssetRegistry(
            asset_class=AnimationClipAsset,
            asset_store=self._asset_store,
            data_from_asset=lambda asset: asset.cached_data,
        )
        self.register_runtime_asset_registry("skeleton", self._skeleton_assets)
        self.register_runtime_asset_registry("animation_clip", self._animation_assets)

    def get_skeleton_asset_by_uuid(self, uuid: str) -> SkeletonAsset | None:
        return self._skeleton_assets.get_asset_by_uuid(uuid)


def _register_glb(
    rm: _ResourceManager,
    *,
    name: str,
    glb_uuid: str,
    skeleton_uuid: str,
    animations: dict[str, str],
) -> GLBAsset:
    glb_asset = GLBAsset(name=name, source_path=f"/tmp/{name}.glb", uuid=glb_uuid)
    glb_asset.set_resource_manager(rm)
    glb_asset.parse_spec(
        {
            "uuid": glb_uuid,
            "resources": {
                "skeletons": {"skeleton": skeleton_uuid},
                "animations": animations,
            },
        }
    )
    return glb_asset


def _node(name: str, children: list[int]) -> SimpleNamespace:
    return SimpleNamespace(name=name, children=children)


def _install_scene_data(glb_asset: GLBAsset) -> None:
    glb_asset.set_runtime_data(
        SimpleNamespace(
            nodes=[
                _node("Root", [1, 2]),
                _node("Hip", [3]),
                _node("Mesh", []),
                _node("Hand", []),
            ],
            root_nodes=[0],
            skins=[SimpleNamespace(joint_node_indices=[1, 3])],
        ),
        loaded=True,
    )


def _character_scene(*, node_targets: object = None) -> dict:
    player_data = {"clips": []}
    if node_targets is not None:
        player_data["node_targets"] = node_targets
    return {
        "entities": [
            {
                "uuid": "model",
                "name": "Character",
                "children": [
                    {
                        "uuid": "root",
                        "name": "Root",
                        "children": [
                            {
                                "uuid": "hip",
                                "name": "Hip",
                                "children": [
                                    {"uuid": "hand", "name": "Hand", "children": []}
                                ],
                            },
                            {"uuid": "mesh", "name": "Mesh", "children": []},
                        ],
                    }
                ],
                "components": [
                    {
                        "type": "SkeletonController",
                        "data": {
                            "skeleton": {"uuid": "skeleton-uuid"},
                            "bone_entities": [{"uuid": "hip"}, {"uuid": "hand"}],
                        },
                    },
                    {"type": "AnimationPlayer", "data": player_data},
                ],
            }
        ]
    }


def test_repair_glb_animation_player_clip_refs_uses_sibling_skeleton_owner() -> None:
    rm = _ResourceManager()
    _register_glb(
        rm,
        name="Arthur",
        glb_uuid="arthur-glb-uuid",
        skeleton_uuid="arthur-skeleton-uuid",
        animations={
            "Idle": "arthur-idle-uuid",
            "Walk": "arthur-walk-uuid",
        },
    )
    _register_glb(
        rm,
        name="CorpGuard",
        glb_uuid="corpguard-glb-uuid",
        skeleton_uuid="corpguard-skeleton-uuid",
        animations={
            "Idle": "corpguard-idle-uuid",
            "Walk": "corpguard-walk-uuid",
        },
    )

    scene_data = {
        "entities": [
            {
                "name": "CorpGuard",
                "components": [
                    {
                        "type": "SkeletonController",
                        "data": {
                            "skeleton": {
                                "uuid": "corpguard-skeleton-uuid",
                                "name": "CorpGuard_skeleton",
                            },
                        },
                    },
                    {
                        "type": "AnimationPlayer",
                        "data": {
                            "node_targets": [{"uuid": "already-complete"}],
                            "clips": [
                                {
                                    "uuid": "arthur-idle-uuid",
                                    "name": "Idle",
                                    "type": "uuid",
                                },
                                {
                                    "uuid": "arthur-walk-uuid",
                                    "name": "Walk",
                                    "type": "uuid",
                                },
                                {
                                    "uuid": "arthur-only-uuid",
                                    "name": "ArthurOnly",
                                    "type": "uuid",
                                },
                            ],
                        },
                    },
                ],
            }
        ],
    }

    repaired = repair_glb_animation_player_clip_refs(scene_data, rm)

    clips = scene_data["entities"][0]["components"][1]["data"]["clips"]
    assert repaired == 2
    assert clips[0]["uuid"] == "corpguard-idle-uuid"
    assert clips[1]["uuid"] == "corpguard-walk-uuid"
    assert clips[2]["uuid"] == "arthur-only-uuid"


def test_repair_glb_animation_player_clip_refs_without_manager_returns_zero() -> None:
    set_resource_manager_factory(None)

    try:
        assert repair_glb_animation_player_clip_refs({"entities": []}) == 0
    finally:
        set_resource_manager_factory(None)


def test_repair_reconstructs_missing_node_targets_in_glb_node_order() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    scene_data = _character_scene()

    repaired = repair_glb_animation_player_clip_refs(scene_data, rm)

    player_data = scene_data["entities"][0]["components"][1]["data"]
    assert repaired == 1
    assert player_data["node_targets"] == [
        {"uuid": "root"},
        {"uuid": "hip"},
        {"uuid": "mesh"},
        {"uuid": "hand"},
    ]


def test_repair_reconstructs_empty_node_targets() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    scene_data = _character_scene(node_targets=[])

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 1


def test_repair_preserves_hole_for_structural_node_omitted_by_old_importer() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    glb_asset.set_runtime_data(
        SimpleNamespace(
            nodes=[
                _node("Armature", [1, 2]),
                _node("Hip", [3]),
                _node("Mesh", []),
                _node("Hand", []),
            ],
            root_nodes=[0],
            skins=[SimpleNamespace(joint_node_indices=[1, 3])],
        ),
        loaded=True,
    )
    scene_data = _character_scene(node_targets=[])
    wrapper = scene_data["entities"][0]
    promoted_children = wrapper["children"][0]["children"]
    wrapper["children"] = promoted_children

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 1
    player_data = wrapper["components"][1]["data"]
    assert player_data["node_targets"] == [
        {},
        {"uuid": "hip"},
        {"uuid": "mesh"},
        {"uuid": "hand"},
    ]


def test_repair_preserves_hole_for_leaf_omitted_by_old_importer() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    scene_data = _character_scene(node_targets=[])
    root = scene_data["entities"][0]["children"][0]
    root["children"] = [child for child in root["children"] if child["name"] != "Mesh"]

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 1
    player_data = scene_data["entities"][0]["components"][1]["data"]
    assert player_data["node_targets"] == [
        {"uuid": "root"},
        {"uuid": "hip"},
        {},
        {"uuid": "hand"},
    ]


def test_repair_keeps_complete_node_targets_unchanged() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    original = [{"uuid": "preserved"}]
    scene_data = _character_scene(node_targets=original.copy())

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 0
    player_data = scene_data["entities"][0]["components"][1]["data"]
    assert player_data["node_targets"] == original


def test_repair_rejects_ambiguous_serialized_children_without_partial_update() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    scene_data = _character_scene(node_targets=[])
    root = scene_data["entities"][0]["children"][0]
    root["children"].append({"uuid": "mesh-duplicate", "name": "Mesh", "children": []})

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 0
    player_data = scene_data["entities"][0]["components"][1]["data"]
    assert player_data["node_targets"] == []


def test_repair_rejects_incomplete_bone_anchors_without_partial_update() -> None:
    rm = _ResourceManager()
    glb_asset = _register_glb(
        rm,
        name="Character",
        glb_uuid="glb-uuid",
        skeleton_uuid="skeleton-uuid",
        animations={},
    )
    _install_scene_data(glb_asset)
    scene_data = _character_scene(node_targets=[])
    skeleton_data = scene_data["entities"][0]["components"][0]["data"]
    skeleton_data["bone_entities"] = [{"uuid": "hip"}]

    assert repair_glb_animation_player_clip_refs(scene_data, rm) == 0
    player_data = scene_data["entities"][0]["components"][1]["data"]
    assert player_data["node_targets"] == []
