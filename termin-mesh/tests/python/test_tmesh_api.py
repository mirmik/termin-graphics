import uuid

import numpy as np
import tmesh


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
