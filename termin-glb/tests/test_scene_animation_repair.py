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
) -> None:
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
