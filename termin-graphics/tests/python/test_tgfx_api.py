import uuid

import numpy as np
import tgfx


def test_basic_types_and_render_state():
    c = tgfx.Color4.red()
    assert c.r == 1.0
    assert c.g == 0.0
    assert c.b == 0.0
    assert c.a == 1.0

    rs = tgfx.RenderState.opaque()
    assert rs.depth_test is True
    assert rs.depth_write is True


def test_vertex_layout_building():
    layout = tgfx.TcVertexLayout()
    ok1 = layout.add("position", 3, tgfx.TcAttribType.FLOAT32, 0)
    ok2 = layout.add("normal", 3, tgfx.TcAttribType.FLOAT32, 1)
    ok3 = layout.add("uv", 2, tgfx.TcAttribType.FLOAT32, 2)

    assert ok1 and ok2 and ok3
    assert layout.attrib_count == 3
    assert layout.stride == 32

    uv = layout.find("uv")
    assert uv is not None
    assert uv["offset"] == 24


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

    mesh = tgfx.Mesh3(vertices=vertices, triangles=triangles, name="tri")
    assert mesh.is_valid()
    assert mesh.vertex_count == 3
    assert mesh.triangle_count == 1
    assert mesh.name == "tri"


def test_mesh_registry_set_data_smoke():
    mesh_uuid = f"pytest-{uuid.uuid4()}"
    handle = tgfx.tc_mesh_get_or_create(mesh_uuid)
    assert handle.is_valid
    assert tgfx.tc_mesh_contains(handle.uuid)

    layout = tgfx.TcVertexLayout.pos_normal_uv()
    vertices = np.array(
        [
            0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
            1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0,
            0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0,
        ],
        dtype=np.float32,
    )
    indices = np.array([0, 1, 2], dtype=np.uint32)

    ok = tgfx.tc_mesh_set_data(handle, vertices, 3, layout, indices, "pytest-mesh")
    assert ok
    assert handle.vertex_count == 3
    assert handle.index_count == 3
    assert tgfx.tc_mesh_count() >= 1

    all_info = tgfx.tc_mesh_get_all_info()
    assert any(info["uuid"] == handle.uuid for info in all_info)
