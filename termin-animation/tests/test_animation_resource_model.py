import uuid

from termin.animation import clip_from_glb
from termin.animation._animation_native import (
    TcAnimationClip,
    tc_animation_declare,
    tc_animation_ensure_loaded,
    tc_animation_is_loaded,
)
from tcbase import clear_resource_loader, set_resource_loader
from termin.glb.loader import GLBAnimationChannel, GLBAnimationClip


def _glb_clip(name: str = "Walk") -> GLBAnimationClip:
    channel = GLBAnimationChannel(
        node_index=0,
        node_name="Root",
        pos_keys=[(0.0, [0.0, 0.0, 0.0]), (1.0, [1.0, 0.0, 0.0])],
        rot_keys=[(0.0, [0.0, 0.0, 0.0, 1.0])],
        scale_keys=[(0.0, [1.0, 1.0, 1.0])],
    )
    return GLBAnimationClip(name=name, channels=[channel], duration=1.0)


def test_clip_from_glb_fills_declared_animation_uuid() -> None:
    animation_uuid = str(uuid.uuid4())
    declared = tc_animation_declare(animation_uuid, "Walk")

    assert declared.is_valid
    assert not tc_animation_is_loaded(declared)

    clip = clip_from_glb(_glb_clip(), animation_uuid)

    assert clip.is_valid
    assert clip.uuid == animation_uuid
    assert tc_animation_is_loaded(clip)
    assert clip.channel_count == 1
    assert TcAnimationClip.from_uuid(animation_uuid).uuid == animation_uuid


def test_declared_animation_process_loader_populates_same_resource() -> None:
    animation_uuid = str(uuid.uuid4())
    declared = tc_animation_declare(animation_uuid, "Walk")

    def load_animation(resource_uuid: str) -> bool:
        assert resource_uuid == animation_uuid
        clip = clip_from_glb(_glb_clip(), resource_uuid)
        return clip.is_valid

    set_resource_loader(load_animation)
    try:
        assert tc_animation_ensure_loaded(declared)
        assert declared.uuid == animation_uuid
        assert tc_animation_is_loaded(declared)
        assert declared.channel_count == 1
    finally:
        clear_resource_loader()
