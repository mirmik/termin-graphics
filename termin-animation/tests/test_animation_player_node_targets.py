import pytest

from termin.animation import TcAnimationClip
from termin.animation_components import AnimationPlayer
from termin.geombase import Vec3
from termin.scene import Entity


def test_animation_player_applies_non_bone_node_channel():
    node = Entity(name="Armature")
    clip = TcAnimationClip.create("RootMove", "pytest-animation-node-target")
    clip.set_tps(1.0)
    clip.set_loop(False)
    clip.set_channels([
        {
            "target_name": "Armature",
            "translation_keys": [
                (0.0, Vec3(0.0, 0.0, 0.0)),
                (1.0, Vec3(1.0, 2.0, 3.0)),
            ],
            "rotation_keys": [],
            "scale_keys": [],
        }
    ])

    player = AnimationPlayer()
    player.node_targets = [node]
    player.add_clip(clip)
    player.set_current("RootMove")

    player.update_bones_at_time(1.0)

    position = node.global_pose()["lin"]
    assert tuple(position) == pytest.approx((1.0, 2.0, 3.0))
