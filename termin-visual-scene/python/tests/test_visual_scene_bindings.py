import pytest

from termin.visual_scene import (
    tc_visual_scene_create,
    tc_visual_scene_destroy,
)


def test_item_refs_are_non_owning_direct_views():
    scene = tc_visual_scene_create()
    root = scene.create_group()
    item = scene.create_rect(
        (1.0, 2.0, 30.0, 20.0),
        (0.2, 0.4, 0.8, 1.0),
        parent=root,
    )
    item.set_transform(1.0, 0.25, -0.1, 1.0, 12.0, 18.0)
    item.opacity = 0.5

    assert item.type_name == "termin.visual.Rect2D"
    assert item.world_transform == pytest.approx(
        (1.0, 0.25, -0.1, 1.0, 12.0, 18.0)
    )
    assert len(root.children) == 1
    assert item.parent == root
    assert item.opacity == pytest.approx(0.5)

    item.visible = False
    assert item.visible is False
    assert item.effective_visible is False

    tc_visual_scene_destroy(scene)
    assert not item.valid
    assert not root.valid
    with pytest.raises(ReferenceError):
        _ = item.world_transform


def test_destroy_subtree_invalidates_refs_and_generation_handles():
    scene = tc_visual_scene_create()
    root = scene.create_group()
    old = scene.create_ellipse(
        (2.0, 3.0, 8.0, 9.0),
        (1.0, 0.5, 0.2, 1.0),
        parent=root,
    )
    assert root.destroy()
    assert not root.valid
    assert not old.valid
    replacement = scene.create_rect(
        (0.0, 0.0, 1.0, 1.0),
        (1.0, 1.0, 1.0, 1.0),
    )
    assert replacement.valid
    assert not old.valid
    assert scene.size == 1
    tc_visual_scene_destroy(scene)


def test_cross_scene_parent_is_rejected():
    first = tc_visual_scene_create()
    second = tc_visual_scene_create()
    parent = first.create_group()

    with pytest.raises(ValueError, match="another TcVisualScene"):
        second.create_group(parent)
    tc_visual_scene_destroy(second)
    tc_visual_scene_destroy(first)
