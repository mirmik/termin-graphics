import uuid

import numpy as np
import tmesh
import pytest


def test_vertex_layout_building():
    layout = tmesh.TcVertexLayout()
    ok1 = layout.add("position", 3, tmesh.TcAttribType.FLOAT32, 0)
    ok2 = layout.add("normal", 3, tmesh.TcAttribType.FLOAT32, 1)
    ok3 = layout.add("uv", 2, tmesh.TcAttribType.FLOAT32, 2)

    assert ok1 and ok2 and ok3
    assert layout.attrib_count == 3
    assert layout.stride == 32

    uv = layout.find("uv")
    assert uv is not None
    assert uv["offset"] == 24


def test_vertex_layout_can_request_shader_owned_input_locations():
    layout = tmesh.TcVertexLayout()
    assert layout.use_shader_input_locations == 0

    layout.use_shader_input_locations = 1
    assert layout.use_shader_input_locations == 1


def test_skinned_layout_matches_gpu_contract():
    layout = tmesh.TcVertexLayout.skinned()
    assert layout.attrib_count == 6
    assert layout.stride == 80

    tangent = layout.find("tangent")
    joints = layout.find("joints")
    weights = layout.find("weights")

    assert tangent is not None
    assert tangent["location"] == 3
    assert joints is not None
    assert joints["location"] == 6
    assert weights is not None
    assert weights["location"] == 7


def test_mesh3_from_numpy_arrays():
    vertices = np.array(
        [
            [0.0, 0.0, 0.0],
            [1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
        ],
        dtype=np.float32,
    )
    triangles = np.array([[0, 1, 2]], dtype=np.uint32)

    mesh = tmesh.Mesh3(vertices=vertices, triangles=triangles, name="tri")
    assert mesh.is_valid()
    assert mesh.vertex_count == 3
    assert mesh.triangle_count == 1
    assert mesh.name == "tri"


def test_mesh_registry_set_data_smoke():
    mesh_uuid = f"pytest-{uuid.uuid4()}"
    handle = tmesh.tc_mesh_get_or_create(mesh_uuid)
    assert handle.is_valid
    assert tmesh.tc_mesh_contains(handle.uuid)

    layout = tmesh.TcVertexLayout.pos_normal_uv()
    vertices = np.array(
        [
            0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
            1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0,
            0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0,
        ],
        dtype=np.float32,
    )
    indices = np.array([0, 1, 2], dtype=np.uint32)

    ok = tmesh.tc_mesh_set_data(handle, vertices, 3, layout, indices, "pytest-mesh")
    assert ok
    assert handle.vertex_count == 3
    assert handle.index_count == 3
    assert tmesh.tc_mesh_count() >= 1

    all_info = tmesh.tc_mesh_get_all_info()
    assert any(info["uuid"] == handle.uuid for info in all_info)


def _cube_tc_mesh():
    h = 1.5
    z0 = 0.5
    z1 = 3.5
    vertices = np.array(
        [
            [-h, -h, z0],
            [h, -h, z0],
            [h, h, z0],
            [-h, h, z0],
            [-h, -h, z1],
            [h, -h, z1],
            [h, h, z1],
            [-h, h, z1],
        ],
        dtype=np.float32,
    )
    triangles = np.array(
        [
            [4, 5, 6],
            [4, 6, 7],
            [0, 2, 1],
            [0, 3, 2],
            [1, 2, 6],
            [1, 6, 5],
            [3, 7, 6],
            [3, 6, 2],
            [0, 4, 7],
            [0, 7, 3],
            [0, 1, 5],
            [0, 5, 4],
        ],
        dtype=np.uint32,
    )
    mesh = tmesh.Mesh3(vertices=vertices, triangles=triangles, name="surface-edge-cube")
    return tmesh.TcMesh.from_mesh3(mesh, f"surface-edge-cube-{uuid.uuid4()}")


def _cube_tc_mesh_with_unshared_triangle_vertices():
    shared = _cube_tc_mesh()
    vertices = shared.vertices
    triangles = shared.triangles
    expanded_vertices = []
    expanded_triangles = []
    for tri in triangles:
        base = len(expanded_vertices)
        expanded_vertices.extend([
            vertices[int(tri[0])],
            vertices[int(tri[1])],
            vertices[int(tri[2])],
        ])
        expanded_triangles.append([base, base + 1, base + 2])

    mesh = tmesh.Mesh3(
        vertices=np.asarray(expanded_vertices, dtype=np.float32),
        triangles=np.asarray(expanded_triangles, dtype=np.uint32),
        name="surface-edge-cube-unshared",
    )
    return tmesh.TcMesh.from_mesh3(mesh, f"surface-edge-cube-unshared-{uuid.uuid4()}")


def _box_tc_mesh_unshared(width: float, depth: float, height: float):
    x0 = -width * 0.5
    x1 = width * 0.5
    y0 = -depth * 0.5
    y1 = depth * 0.5
    z0 = 0.5
    z1 = z0 + height

    faces = [
        ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)),
        ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)),
        ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)),
        ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)),
        ((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)),
        ((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)),
    ]

    vertices = []
    triangles = []
    for quad in faces:
        base = len(vertices)
        vertices.extend([quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]])
        triangles.append([base, base + 1, base + 2])
        triangles.append([base + 3, base + 4, base + 5])

    mesh = tmesh.Mesh3(
        vertices=np.asarray(vertices, dtype=np.float32),
        triangles=np.asarray(triangles, dtype=np.uint32),
        name="surface-edge-wall-box-unshared",
    )
    return tmesh.TcMesh.from_mesh3(mesh, f"surface-edge-wall-box-unshared-{uuid.uuid4()}")


@pytest.mark.parametrize(
    "point,expected_edge",
    [
        ((1.3, -1.0, 3.5), (1.5, -1.0, 3.5)),
        ((1.3, 0.0, 3.5), (1.5, 0.0, 3.5)),
        ((1.3, 1.0, 3.5), (1.5, 1.0, 3.5)),
        ((-1.3, -1.0, 3.5), (-1.5, -1.0, 3.5)),
        ((-1.3, 0.0, 3.5), (-1.5, 0.0, 3.5)),
        ((-1.3, 1.0, 3.5), (-1.5, 1.0, 3.5)),
        ((-1.0, 1.3, 3.5), (-1.0, 1.5, 3.5)),
        ((0.0, 1.3, 3.5), (0.0, 1.5, 3.5)),
        ((1.0, 1.3, 3.5), (1.0, 1.5, 3.5)),
        ((-1.0, -1.3, 3.5), (-1.0, -1.5, 3.5)),
        ((0.0, -1.3, 3.5), (0.0, -1.5, 3.5)),
        ((1.0, -1.3, 3.5), (1.0, -1.5, 3.5)),
    ],
)
def test_surface_edge_query_finds_symmetric_top_cube_edges(point, expected_edge):
    mesh = _cube_tc_mesh()

    edge = mesh.find_surface_edge(
        start_triangle=0,
        point=point,
        normal=(0.0, 0.0, 1.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx(expected_edge, abs=1e-6)
    assert edge["distance"] == pytest.approx(0.2, abs=1e-6)


def test_surface_edge_query_ignores_unshared_internal_diagonal():
    mesh = _cube_tc_mesh_with_unshared_triangle_vertices()

    edge = mesh.find_surface_edge(
        start_triangle=0,
        point=(0.1, 0.0, 3.5),
        normal=(0.0, 0.0, 1.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((1.5, 0.0, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(1.4, abs=1e-6)


def test_surface_edge_query_finds_nearest_edge_on_vertical_surface():
    mesh = _cube_tc_mesh()

    edge = mesh.find_surface_edge(
        start_triangle=4,
        point=(1.5, 0.0, 2.5),
        normal=(1.0, 0.0, 0.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((1.5, 0.0, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(1.0, abs=1e-6)


def test_surface_edge_query_finds_short_edge_on_long_box_top_face():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_surface_edge(
        start_triangle=0,
        point=(-9.8, 0.0, 3.5),
        normal=(0.0, 0.0, 1.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((-10.0, 0.0, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.2, abs=1e-6)


def test_surface_edge_query_uses_metric_for_distance():
    mesh = _cube_tc_mesh()

    edge = mesh.find_surface_edge(
        start_triangle=0,
        point=(0.9, 1.3, 3.5),
        normal=(0.0, 0.0, 1.0),
        metric=(0.1, 1.0, 1.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((1.5, 1.3, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.06, abs=1e-6)


def test_surface_edge_aligned_query_uses_metric_for_direction_filter():
    mesh = _cube_tc_mesh()

    edge = mesh.find_surface_edge_aligned(
        start_triangle=0,
        point=(0.9, 1.3, 3.5),
        normal=(0.0, 0.0, 1.0),
        edge_direction=(0.0, 1.0, 0.0),
        max_angle_degrees=10.0,
        metric=(0.1, 1.0, 1.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((1.5, 1.3, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.06, abs=1e-6)


def test_surface_edge_query_finds_short_vertical_edge_on_long_box_wall_face():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_surface_edge(
        start_triangle=4,
        point=(-9.8, -0.5, 2.0),
        normal=(0.0, -1.0, 0.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((-10.0, -0.5, 2.0), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.2, abs=1e-6)
    assert edge["side"] == -1


def test_surface_edge_query_prefers_near_horizontal_edge_over_far_wall_end():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_surface_edge(
        start_triangle=4,
        point=(-5.0, -0.5, 3.3),
        normal=(0.0, -1.0, 0.0),
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((-5.0, -0.5, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.2, abs=1e-6)


def test_surface_edge_aligned_query_filters_by_vertical_edge_direction():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_surface_edge_aligned(
        start_triangle=4,
        point=(-5.0, -0.5, 3.3),
        normal=(0.0, -1.0, 0.0),
        edge_direction=(0.0, 0.0, 1.0),
        max_angle_degrees=10.0,
    )

    assert edge is not None
    assert edge["point"] == pytest.approx((-10.0, -0.5, 3.3), abs=1e-6)
    assert edge["distance"] == pytest.approx(5.0, abs=1e-6)
    assert edge["side"] == -1


def test_surface_edge_aligned_query_rejects_mismatching_edge_direction():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_surface_edge_aligned(
        start_triangle=4,
        point=(-5.0, -0.5, 3.3),
        normal=(0.0, -1.0, 0.0),
        edge_direction=(0.0, 1.0, 0.0),
        max_angle_degrees=10.0,
    )

    assert edge is None


def test_nearest_surface_edge_query_does_not_require_start_triangle():
    mesh = _box_tc_mesh_unshared(width=20.0, depth=1.0, height=3.0)

    edge = mesh.find_nearest_surface_edge(point=(-9.8, 0.0, 3.5))

    assert edge is not None
    assert edge["point"] == pytest.approx((-10.0, 0.0, 3.5), abs=1e-6)
    assert edge["distance"] == pytest.approx(0.2, abs=1e-6)
