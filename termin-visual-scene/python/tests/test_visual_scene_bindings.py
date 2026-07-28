import gc
import json

import pytest

from termin.visual_scene import VisualScene2D


def test_item_refs_are_non_owning_and_inspection_is_detached():
    scene = VisualScene2D()
    root = scene.create_group()
    item = scene.create_rect(
        1.0,
        2.0,
        30.0,
        20.0,
        (0.2, 0.4, 0.8, 1.0),
        root,
    )
    item.set_transform(1.0, 0.25, -0.1, 1.0, 12.0, 18.0)
    item.set_opacity(0.5)

    snapshot = item.snapshot()
    inspection = scene.inspection()
    assert snapshot["type"] == "termin.visual.Rect2D"
    assert snapshot["world_transform"] == pytest.approx(
        (1.0, 0.25, -0.1, 1.0, 12.0, 18.0)
    )
    assert inspection["items"][0]["children"] == [1]
    assert inspection["items"][1]["parent_index"] == 0

    item.set_visible(False)
    assert snapshot["visible"] is True
    assert inspection["items"][1]["visible"] is True

    del scene
    gc.collect()
    assert not item.valid
    assert not root.valid
    with pytest.raises(ReferenceError):
        item.snapshot()


def test_serialization_restore_and_stale_generation_handles():
    scene = VisualScene2D()
    root = scene.create_group()
    old = scene.create_ellipse(
        2.0, 3.0, 8.0, 9.0, (1.0, 0.5, 0.2, 1.0), root)
    document = scene.serialize_json()
    parsed = json.loads(document)
    assert parsed["schema"] == "termin.visual_scene.2d"
    assert "scene_id" not in document
    assert "generation" not in document

    assert old.destroy_leaf()
    assert not old.valid
    replacement = scene.create_rect(
        0.0, 0.0, 1.0, 1.0, (1.0, 1.0, 1.0, 1.0), root)
    assert replacement.valid
    assert not old.valid

    restored = VisualScene2D()
    assert restored.restore_json(document)
    assert restored.size == 2
    assert restored.inspection()["items"][1]["type"] == (
        "termin.visual.Ellipse2D"
    )
    assert not restored.restore_json(document)


def test_cross_scene_parent_is_rejected():
    first = VisualScene2D()
    second = VisualScene2D()
    parent = first.create_group()

    with pytest.raises(ValueError, match="another VisualScene2D"):
        second.create_group(parent)
