import pytest
import numpy as np
from tcbase._geom_native import Affine3d, Ray3, SrgbColor, Vec3
from tgfx import PointCloudStyle
from tmesh import Mesh3

from termin.visual_scene import (
    PointerEventKind3D,
    PrimitiveItemRef3D,
    SceneInteraction3D,
    StaticMeshItemRef3D,
    tc_visual_scene_create,
    tc_visual_scene3d_create,
    tc_visual_scene3d_destroy,
    tc_visual_scene_destroy,
)


def test_item_refs_are_non_owning_direct_views():
    scene = tc_visual_scene_create()
    root = scene.create_group()
    item = scene.create_rect(
        (1.0, 2.0, 30.0, 20.0),
        SrgbColor(0.2, 0.4, 0.8, 1.0),
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
        SrgbColor(1.0, 0.5, 0.2, 1.0),
        parent=root,
    )
    assert root.destroy()
    assert not root.valid
    assert not old.valid
    replacement = scene.create_rect(
        (0.0, 0.0, 1.0, 1.0),
        SrgbColor(1.0, 1.0, 1.0, 1.0),
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


def test_hit_region_rect_has_bounds_without_painting():
    scene = tc_visual_scene_create()
    item = scene.create_hit_region_rect((2.0, 3.0, 30.0, 20.0))

    assert item.type_name == "termin.visual.HitRegion2D"
    assert item.local_bounds == pytest.approx((2.0, 3.0, 32.0, 23.0))

    tc_visual_scene_destroy(scene)


def test_text_accepts_optional_literal_coverage_gamma():
    scene = tc_visual_scene_create()
    inherited = scene.create_text(
        "inherited",
        (0.0, 0.0),
        14.0,
        SrgbColor(1.0, 1.0, 1.0, 1.0),
        (0.0, 0.0, 80.0, 20.0),
    )
    overridden = scene.create_text(
        "overridden",
        (0.0, 20.0),
        14.0,
        SrgbColor(1.0, 1.0, 1.0, 1.0),
        (0.0, 20.0, 80.0, 20.0),
        coverage_gamma=1.25,
    )
    assert inherited.valid
    assert overridden.valid

    with pytest.raises(ValueError, match="invalid TextItem2D state"):
        scene.create_text(
            "invalid",
            (0.0, 40.0),
            14.0,
            SrgbColor(1.0, 1.0, 1.0, 1.0),
            (0.0, 40.0, 80.0, 20.0),
            coverage_gamma=0.0,
        )

    tc_visual_scene_destroy(scene)


def _triangle_mesh(x=0.0):
    return Mesh3(
        vertices=np.asarray(
            [[x, -1.0, -1.0], [x, 1.0, -1.0], [x, 0.0, 1.0]],
            dtype=np.float32,
        ),
        triangles=np.asarray([[0, 1, 2]], dtype=np.uint32),
        name="binding-triangle",
    )


def _x_ray(y=0.0):
    return Ray3(Vec3(0.0, y, 0.0), Vec3(1.0, 0.0, 0.0))


def test_scene3d_refs_preserve_exact_affine_and_generation_lifetime():
    scene = tc_visual_scene3d_create()
    root = scene.create_group()
    primitive = scene.create_primitive(
        [(2.0, -1.0, -1.0), (2.0, 1.0, -1.0), (2.0, 0.0, 1.0)],
        [(0, 1, 2)],
        colors=[SrgbColor(1.0, 0.0, 0.0, 1.0)] * 3,
        triangle_parts=[42],
        parent=root,
    )
    root.local_transform = Affine3d.scaling(2.0, 3.0, 4.0)
    primitive.local_transform = Affine3d.from_translation(Vec3(1.0, 2.0, 3.0))

    assert isinstance(primitive, PrimitiveItemRef3D)
    assert primitive.parent == root
    assert root.children == [primitive]
    assert np.allclose(
        primitive.world_transform.as_matrix(),
        (root.local_transform @ primitive.local_transform).as_matrix(),
    )
    assert primitive.local_bounds == pytest.approx((2.0, -1.0, -1.0, 2.0, 1.0, 1.0))
    assert primitive.world_bounds == pytest.approx((6.0, 3.0, 8.0, 6.0, 9.0, 16.0))

    assert root.destroy()
    assert not root.valid
    assert not primitive.valid
    replacement = scene.create_group()
    assert replacement.valid
    assert not primitive.valid

    tc_visual_scene3d_destroy(scene)
    with pytest.raises(ReferenceError):
        _ = replacement.world_transform


def test_scene3d_builtins_copy_resources_and_choose_nearest_hit():
    scene = tc_visual_scene3d_create()
    primitive = scene.create_primitive(
        [(2.0, -1.0, -1.0), (2.0, 1.0, -1.0), (2.0, 0.0, 1.0)],
        [(0, 1, 2)],
        triangle_parts=[7],
    )
    authored_mesh = _triangle_mesh(4.0)
    mesh = scene.create_static_mesh(authored_mesh)
    cloud_style = PointCloudStyle()
    cloud_style.size_px = 5.0
    cloud = scene.create_point_cloud(
        [(6.0, 0.0, 0.0)],
        colors=[SrgbColor(0.0, 1.0, 0.0, 1.0)],
        size_scales=[2.0],
        style=cloud_style,
        pick_radius=0.25,
    )

    authored_mesh.translate(100.0, 0.0, 0.0)
    hit = scene.hit_test(_x_ray())
    assert hit.item == primitive
    assert hit.distance == pytest.approx(2.0)
    assert hit.part == 7

    with pytest.raises(ValueError, match="finite indexed triangle geometry"):
        primitive.set_geometry([(9.0, 0.0, 0.0)], [(0, 1, 2)])
    assert scene.hit_test(_x_ray()).item == primitive

    primitive.enabled = False
    hit = scene.hit_test(_x_ray())
    assert isinstance(hit.item, StaticMeshItemRef3D)
    assert hit.item == mesh
    assert hit.distance == pytest.approx(4.0)

    mesh.set_mesh(_triangle_mesh(5.0))
    mesh.enabled = False
    hit = scene.hit_test(_x_ray())
    assert hit.item == cloud
    assert hit.part == 1
    assert cloud.pick_radius == pytest.approx(0.25)
    with pytest.raises(ValueError, match="finite non-empty point data"):
        cloud.set_points([])
    assert scene.hit_test(_x_ray()).item == cloud
    cloud.pick_radius = 0.5
    cloud.set_points([(7.0, 0.0, 0.0)])

    tc_visual_scene3d_destroy(scene)


def test_scene3d_rejects_cross_scene_parent_and_invalid_mutation():
    first = tc_visual_scene3d_create()
    second = tc_visual_scene3d_create()
    parent = first.create_group()

    with pytest.raises(ValueError, match="another TcVisualScene3D"):
        second.create_group(parent)
    with pytest.raises(ValueError, match="finite indexed triangle geometry"):
        first.create_primitive([(1.0, 0.0, 0.0)], [(0, 1, 2)])

    tc_visual_scene3d_destroy(second)
    tc_visual_scene3d_destroy(first)


def test_scene3d_interaction_routes_capture_actions_and_callback_failures():
    scene = tc_visual_scene3d_create()
    item = scene.create_primitive(
        [(2.0, -1.0, -1.0), (2.0, 1.0, -1.0), (2.0, 0.0, 1.0)],
        [(0, 1, 2)],
        triangle_parts=[99],
    )
    interaction = SceneInteraction3D()
    actions = []
    fallbacks = []
    interaction.set_action_handler(item, actions.append)
    interaction.set_fallback_handler(fallbacks.append)

    down = interaction.route(scene, 3, PointerEventKind3D.Down, _x_ray(), button=1)
    assert down.target == item
    assert down.captured == item
    assert interaction.captured(scene, 3) == item
    up = interaction.route(scene, 3, PointerEventKind3D.Up, _x_ray(), button=1)
    assert up.action.part == 99
    assert actions[0].target == item
    assert not up.callback_failed
    assert interaction.captured(scene, 3) is None

    miss = interaction.route(scene, 4, PointerEventKind3D.Move, _x_ray(y=20.0))
    assert miss.used_fallback
    assert len(fallbacks) == 1

    def fail(_event):
        raise RuntimeError("python action failed")

    interaction.set_action_handler(item, fail)
    interaction.route(scene, 5, PointerEventKind3D.Down, _x_ray())
    failed = interaction.route(scene, 5, PointerEventKind3D.Up, _x_ray())
    assert failed.callback_failed

    tc_visual_scene3d_destroy(scene)
