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

    position = node.transform.global_position
    assert tuple(position) == pytest.approx((1.0, 2.0, 3.0))


def test_animation_player_applies_bulk_track_by_exact_node_index():
    first = Entity(name="Duplicate")
    target = Entity(name="Duplicate")
    clip = TcAnimationClip.create("BulkNodeMove", "pytest-animation-bulk-node-target")
    clip.set_tps(1.0)
    clip.set_loop(False)
    clip.set_tracks([
        {
            "target_node_index": 2,
            "path": "translation",
            "interpolation": "step",
            "components": 3,
            "times": [0.0, 1.0],
            "values": [1.0, 2.0, 3.0, 7.0, 8.0, 9.0],
        },
        {
            "target_node_index": 2,
            "path": "scale",
            "interpolation": "linear",
            "components": 3,
            "times": [0.0, 1.0],
            "values": [1.0, 2.0, 3.0, 3.0, 6.0, 9.0],
        },
    ])

    player = AnimationPlayer()
    player.node_targets = [first, None, target]
    assert player.node_targets[1] is None
    player.add_clip(clip)
    player.set_current("BulkNodeMove")

    player.update_bones_at_time(0.5)

    assert tuple(first.transform.global_position) == pytest.approx((0.0, 0.0, 0.0))
    assert tuple(target.transform.global_position) == pytest.approx((1.0, 2.0, 3.0))
    assert tuple(target.transform.local_scale()) == pytest.approx((2.0, 4.0, 6.0))
