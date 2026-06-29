from termin.editor_core.resource_manager import ResourceManager
from termin.glb.scene_animation_repair import repair_glb_animation_player_clip_refs
from termin_assets import PreLoadResult, set_resource_manager_factory


def _register_glb(
    rm: ResourceManager,
    *,
    name: str,
    glb_uuid: str,
    skeleton_uuid: str,
    animations: dict[str, str],
) -> None:
    rm.register_file(
        PreLoadResult(
            resource_type="glb",
            path=f"/tmp/{name}.glb",
            content=None,
            uuid=glb_uuid,
            spec_data={
                "uuid": glb_uuid,
                "resources": {
                    "skeletons": {"skeleton": skeleton_uuid},
                    "animations": animations,
                },
            },
        )
    )


def test_repair_glb_animation_player_clip_refs_uses_sibling_skeleton_owner() -> None:
    rm = ResourceManager()
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
        set_resource_manager_factory(ResourceManager.instance)
