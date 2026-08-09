import uuid

import pytest

from termin.skeleton import TcSkeleton


def _bone(name: str, parent_index: int = -1, *, translation=(0.0, 0.0, 0.0)):
    return {
        "name": name,
        "parent_index": parent_index,
        "inverse_bind_matrix": [
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
        ],
        "bind_translation": translation,
        "bind_rotation": (0.0, 0.0, 0.0, 1.0),
        "bind_scale": (1.0, 1.0, 1.0),
    }


def test_bulk_bones_replace_complete_payload_and_roots() -> None:
    skeleton = TcSkeleton.create("Bulk", str(uuid.uuid4()))
    root = _bone("Root")
    root["inverse_bind_matrix"][12:15] = [-4.0, -5.0, -6.0]
    skeleton.set_bones([root, _bone("Child", 0, translation=(1.0, 2.0, 3.0))])

    assert skeleton.bone_count == 2
    assert skeleton.root_count == 1
    assert skeleton.bones[0]["inverse_bind_matrix"][12:15] == pytest.approx(
        (-4.0, -5.0, -6.0)
    )
    assert skeleton.bones[1]["name"] == "Child"
    assert skeleton.bones[1]["parent_index"] == 0
    assert skeleton.bones[1]["bind_translation"] == pytest.approx((1.0, 2.0, 3.0))


@pytest.mark.parametrize(
    "invalid",
    [
        [_bone("Self", 0)],
        [_bone("A", 1), _bone("B", 0)],
        [_bone("Missing", 4)],
    ],
)
def test_bulk_bone_replacement_is_transactional(invalid) -> None:
    skeleton = TcSkeleton.create("Rollback", str(uuid.uuid4()))
    skeleton.set_bones([_bone("Original")])
    version = skeleton.version

    with pytest.raises(RuntimeError, match="previous payload was preserved"):
        skeleton.set_bones(invalid)

    assert skeleton.version == version
    assert skeleton.bone_count == 1
    assert skeleton.bones[0]["name"] == "Original"
