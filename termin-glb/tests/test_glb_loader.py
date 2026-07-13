import json
import struct

import numpy as np
import pytest

from termin.default_assets.resource_manager import DefaultResourceManager
from termin.glb.asset import GLBAsset
from termin.glb.instantiator import _glb_mesh_to_tc_mesh
from termin.glb.loader import (
    GLBAnimationChannel,
    GLBAnimationClip,
    GLBNodeData,
    GLBSceneData,
    GLBSkinData,
    _build_scene_data,
    _read_accessor,
    apply_blender_z_up_fix,
    load_glb_file,
    load_glb_file_normalized,
    normalize_glb_scale,
)


def _write_triangle_gltf(tmp_path):
    positions = struct.pack(
        "<9f",
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
    )
    normals = struct.pack(
        "<9f",
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
    )
    uvs = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    indices = struct.pack("<3H", 0, 1, 2)
    payload = positions + normals + uvs + indices

    bin_path = tmp_path / "triangle.bin"
    bin_path.write_bytes(payload)
    image_path = tmp_path / "triangle.png"
    image_path.write_bytes(b"\x89PNG\r\n\x1a\n")

    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "TriangleNode"}],
        "materials": [
            {
                "name": "TriangleMaterial",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.25,
                    "roughnessFactor": 0.75,
                    "metallicRoughnessTexture": {"index": 0},
                },
                "normalTexture": {"index": 0, "scale": 0.5},
                "occlusionTexture": {"index": 0},
                "emissiveTexture": {"index": 0},
                "emissiveFactor": [0.1, 0.2, 0.3],
            }
        ],
        "meshes": [
            {
                "name": "TriangleMesh",
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": 0,
                            "NORMAL": 1,
                            "TEXCOORD_0": 2,
                        },
                        "indices": 3,
                        "material": 0,
                    }
                ],
            }
        ],
        "textures": [{"source": 0}],
        "images": [{"uri": "triangle.png", "mimeType": "image/png"}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals)},
            {"buffer": 0, "byteOffset": len(positions) + len(normals), "byteLength": len(uvs)},
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs),
                "byteLength": len(indices),
            },
        ],
        "buffers": [{"uri": "triangle.bin", "byteLength": len(payload)}],
    }

    gltf_path = tmp_path / "triangle.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")
    return gltf_path


def test_load_gltf_with_external_bin_and_texture(tmp_path):
    gltf_path = _write_triangle_gltf(tmp_path)

    scene_data = load_glb_file(gltf_path)

    assert len(scene_data.meshes) == 1
    assert scene_data.meshes[0].name == "TriangleMesh"
    assert scene_data.meshes[0].vertices.shape == (3, 3)
    assert scene_data.meshes[0].uvs is not None
    assert scene_data.meshes[0].uvs.shape == (3, 2)
    assert len(scene_data.textures) == 1
    assert scene_data.textures[0].index == 0
    assert scene_data.textures[0].name == "Texture_0"
    assert scene_data.textures[0].source_path == gltf_path.parent / "triangle.png"
    assert scene_data.textures[0].data.startswith(b"\x89PNG")
    assert len(scene_data.materials) == 1
    material = scene_data.materials[0]
    assert material.base_color_texture == 0
    assert material.metallic_factor == 0.25
    assert material.roughness_factor == 0.75
    assert material.metallic_roughness_texture == 0
    assert material.normal_texture == 0
    assert material.normal_scale == 0.5
    assert material.occlusion_texture == 0
    assert material.emissive_texture == 0
    assert material.emissive_factor.tolist() == pytest.approx([0.1, 0.2, 0.3])


def test_load_gltf_webp_texture_extension_from_buffer_view(tmp_path):
    image_bytes = b"RIFF\x0c\x00\x00\x00WEBPVP8 "
    bin_path = tmp_path / "texture.bin"
    bin_path.write_bytes(image_bytes)

    gltf = {
        "asset": {"version": "2.0"},
        "extensionsUsed": ["EXT_texture_webp"],
        "materials": [
            {
                "name": "WebPMaterial",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                },
            },
        ],
        "textures": [
            {
                "extensions": {"EXT_texture_webp": {"source": 0}},
                "sampler": 0,
            },
        ],
        "images": [
            {
                "bufferView": 0,
                "mimeType": "image/webp",
                "name": "EmbeddedWebP",
            },
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(image_bytes)},
        ],
        "buffers": [{"uri": "texture.bin", "byteLength": len(image_bytes)}],
    }
    gltf_path = tmp_path / "webp_texture.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")

    scene_data = load_glb_file(gltf_path)

    assert len(scene_data.textures) == 1
    assert scene_data.textures[0].index == 0
    assert scene_data.textures[0].name == "EmbeddedWebP"
    assert scene_data.textures[0].mime_type == "image/webp"
    assert scene_data.textures[0].data == image_bytes
    assert scene_data.materials[0].base_color_texture == 0


def test_root_node_animation_tracks_follow_pose_corrections():
    scene_data = GLBSceneData()
    scene_data.root_nodes = [0]
    scene_data.nodes = [
        GLBNodeData(
            name="Armature",
            children=[1],
            mesh_index=None,
            translation=np.array([0.0, 0.0, 0.0], dtype=np.float32),
            rotation=np.array([0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32),
            scale=np.array([0.1, 0.1, 0.1], dtype=np.float32),
        ),
        GLBNodeData(
            name="Base",
            children=[],
            mesh_index=None,
            translation=np.array([0.0, 1.0, 0.0], dtype=np.float32),
            rotation=np.array([-0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32),
            scale=np.array([1.0, 1.0, 1.0], dtype=np.float32),
        ),
    ]
    scene_data.skins = [
        GLBSkinData(
            name="Skin",
            joint_node_indices=[1],
            inverse_bind_matrices=np.eye(4, dtype=np.float32).reshape(1, 4, 4),
        )
    ]
    scene_data.animations = [
        GLBAnimationClip(
            name="Idle",
            channels=[
                GLBAnimationChannel(
                    node_index=0,
                    node_name="Armature",
                    pos_keys=[[0.0, np.array([0.0, -1.7, -0.4], dtype=np.float32)]],
                    rot_keys=[[0.0, np.array([0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32)]],
                    scale_keys=[[0.0, np.array([0.1, 0.1, 0.1], dtype=np.float32)]],
                ),
                GLBAnimationChannel(
                    node_index=1,
                    node_name="Base",
                    pos_keys=[[0.0, np.array([0.0, 1.0, 0.0], dtype=np.float32)]],
                    rot_keys=[[0.0, np.array([-0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32)]],
                    scale_keys=[[0.0, np.array([1.0, 1.0, 1.0], dtype=np.float32)]],
                ),
            ],
            duration=0.0,
        )
    ]

    apply_blender_z_up_fix(scene_data)
    normalize_glb_scale(scene_data)

    root_node = scene_data.nodes[0]
    root_channel = scene_data.animations[0].channels[0]
    np.testing.assert_allclose(root_node.rotation, [0.0, 0.0, 0.0, 1.0], atol=1e-6)
    np.testing.assert_allclose(root_node.scale, [1.0, 1.0, 1.0], atol=1e-6)
    np.testing.assert_allclose(root_channel.rot_keys[0][1], root_node.rotation, atol=1e-6)
    np.testing.assert_allclose(root_channel.scale_keys[0][1], root_node.scale, atol=1e-6)
    np.testing.assert_allclose(root_channel.pos_keys[0][1], [0.0, -1.7, -0.4], atol=1e-6)
    np.testing.assert_allclose(scene_data.animations[0].channels[1].pos_keys[0][1], [0.0, 0.0, 0.1], atol=1e-6)


def test_load_gltf_multi_primitive_mesh_as_submeshes(tmp_path):
    positions = struct.pack(
        "<18f",
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        2.0, 0.0, 0.0,
        3.0, 0.0, 0.0,
        2.0, 1.0, 0.0,
    )
    normals = struct.pack("<18f", *([0.0, 0.0, 1.0] * 6))
    uvs = struct.pack("<12f", *([0.0, 0.0] * 6))
    indices_a = struct.pack("<3H", 0, 1, 2)
    indices_b = struct.pack("<3H", 3, 4, 5)
    payload = positions + normals + uvs + indices_a + indices_b

    bin_path = tmp_path / "multi.bin"
    bin_path.write_bytes(payload)

    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "MultiNode"}],
        "materials": [{"name": "Red"}, {"name": "Blue"}],
        "meshes": [
            {
                "name": "MultiMesh",
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 3,
                        "material": 0,
                    },
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                        "indices": 4,
                        "material": 1,
                    },
                ],
            }
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 6, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 6, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 6, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals)},
            {"buffer": 0, "byteOffset": len(positions) + len(normals), "byteLength": len(uvs)},
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs),
                "byteLength": len(indices_a),
            },
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs) + len(indices_a),
                "byteLength": len(indices_b),
            },
        ],
        "buffers": [{"uri": "multi.bin", "byteLength": len(payload)}],
    }
    gltf_path = tmp_path / "multi.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")

    scene_data = load_glb_file(gltf_path)

    assert len(scene_data.meshes) == 1
    mesh = scene_data.meshes[0]
    assert mesh.name == "MultiMesh"
    assert mesh.vertices.shape == (6, 3)
    assert mesh.indices.tolist() == [0, 1, 2, 3, 4, 5]
    assert len(mesh.submeshes) == 2
    assert mesh.submeshes[0].first_index == 0
    assert mesh.submeshes[0].index_count == 3
    assert mesh.submeshes[0].material_index == 0
    assert mesh.submeshes[0].material_slot == 0
    assert mesh.submeshes[1].first_index == 3
    assert mesh.submeshes[1].index_count == 3
    assert mesh.submeshes[1].material_index == 1
    assert mesh.submeshes[1].material_slot == 1
    assert scene_data.mesh_index_map[0] == [0]


def test_load_gltf_preserves_indexed_vertices_with_exact_dedup(tmp_path):
    positions = struct.pack(
        "<12f",
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 1.0, 0.0,
    )
    normals = struct.pack("<12f", *([0.0, 0.0, 1.0] * 4))
    uvs = struct.pack(
        "<8f",
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0,
    )
    indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
    payload = positions + normals + uvs + indices

    (tmp_path / "quad.bin").write_bytes(payload)
    gltf = {
        "asset": {"version": "2.0"},
        "meshes": [{
            "name": "IndexedQuad",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
            }],
        }],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals)},
            {"buffer": 0, "byteOffset": len(positions) + len(normals), "byteLength": len(uvs)},
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs),
                "byteLength": len(indices),
            },
        ],
        "buffers": [{"uri": "quad.bin", "byteLength": len(payload)}],
    }
    gltf_path = tmp_path / "quad.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")

    scene_data = load_glb_file(gltf_path)
    mesh = scene_data.meshes[0]

    assert mesh.vertices.shape == (4, 3)
    assert mesh.indices.tolist() == [0, 1, 2, 0, 2, 3]
    assert mesh.submeshes[0].index_count == 6


def test_load_gltf_exact_dedup_preserves_uv_seams(tmp_path):
    positions = struct.pack(
        "<9f",
        0.0, 0.0, 0.0,
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
    )
    normals = struct.pack("<9f", *([0.0, 0.0, 1.0] * 3))
    uvs = struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 1.0, 1.0)
    indices = struct.pack("<3H", 0, 1, 2)
    payload = positions + normals + uvs + indices

    (tmp_path / "seam.bin").write_bytes(payload)
    gltf = {
        "asset": {"version": "2.0"},
        "meshes": [{
            "name": "Seam",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
            }],
        }],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
            {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals)},
            {"buffer": 0, "byteOffset": len(positions) + len(normals), "byteLength": len(uvs)},
            {
                "buffer": 0,
                "byteOffset": len(positions) + len(normals) + len(uvs),
                "byteLength": len(indices),
            },
        ],
        "buffers": [{"uri": "seam.bin", "byteLength": len(payload)}],
    }
    gltf_path = tmp_path / "seam.gltf"
    gltf_path.write_text(json.dumps(gltf), encoding="utf-8")

    scene_data = load_glb_file(gltf_path)
    mesh = scene_data.meshes[0]

    assert mesh.vertices.shape == (3, 3)
    assert mesh.indices.tolist() == [0, 1, 2]


def test_glb_mesh_to_tc_mesh_generates_tangent_layout(tmp_path):
    scene_data = load_glb_file(_write_triangle_gltf(tmp_path))
    tc_mesh = _glb_mesh_to_tc_mesh(scene_data.meshes[0], "pytest-glb-tangent-layout")

    assert tc_mesh.is_valid
    mesh_data = tc_mesh.mesh
    assert mesh_data.layout.find("tangent") is not None
    assert mesh_data.stride == 48
    vertices = np.asarray(mesh_data.get_vertices_buffer()).reshape((-1, 12))
    np.testing.assert_allclose(vertices[:, 8:11], np.array([[1.0, 0.0, 0.0]] * 3))
    np.testing.assert_allclose(vertices[:, 11], np.array([1.0, 1.0, 1.0]))


def test_glb_asset_loads_gltf_from_source_path(tmp_path):
    DefaultResourceManager._reset_for_testing()
    rm = DefaultResourceManager.instance()
    gltf_path = _write_triangle_gltf(tmp_path)
    asset = GLBAsset(scene_data=None, name="triangle", source_path=gltf_path)
    asset.set_resource_manager(rm)

    assert asset.ensure_loaded()
    assert asset.scene_data is not None
    assert len(asset.scene_data.meshes) == 1


def _matrix_to_gltf_values(matrix: np.ndarray) -> list[float]:
    return matrix.T.reshape(-1).tolist()


def _matrix_from_node_trs(node: GLBNodeData) -> np.ndarray:
    x, y, z, w = node.rotation
    rotation = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float32)
    matrix = np.eye(4, dtype=np.float32)
    matrix[:3, :3] = rotation @ np.diag(node.scale)
    matrix[:3, 3] = node.translation
    return matrix


def test_load_gltf_decomposes_column_major_matrix_before_coordinate_conversion(tmp_path):
    rotation_z_90 = np.array([
        [0.0, -1.0, 0.0],
        [1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0],
    ], dtype=np.float32)
    source_matrix = np.eye(4, dtype=np.float32)
    source_matrix[:3, :3] = rotation_z_90 @ np.diag([2.0, 3.0, 4.0])
    source_matrix[:3, 3] = [1.0, 2.0, 3.0]
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"matrix": _matrix_to_gltf_values(source_matrix)}],
    }
    path = tmp_path / "matrix-node.gltf"
    path.write_text(json.dumps(gltf), encoding="utf-8")

    raw_node = load_glb_file(path).nodes[0]
    np.testing.assert_allclose(_matrix_from_node_trs(raw_node), source_matrix, atol=1e-5)

    converted_node = load_glb_file_normalized(path).nodes[0]
    coordinate_conversion = np.array([
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 0.0, -1.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ], dtype=np.float32)
    expected_converted_matrix = coordinate_conversion @ source_matrix @ coordinate_conversion.T
    np.testing.assert_allclose(
        _matrix_from_node_trs(converted_node), expected_converted_matrix, atol=1e-5
    )


@pytest.mark.parametrize("scale", [[-2.0, 3.0, 4.0], [0.0, 3.0, 4.0]])
def test_load_gltf_decomposes_reflected_and_zero_scale_matrix_nodes(scale):
    source_matrix = np.eye(4, dtype=np.float32)
    source_matrix[:3, :3] = np.diag(scale)
    source_matrix[:3, 3] = [4.0, 5.0, 6.0]

    scene_data = _build_scene_data({
        "nodes": [{"matrix": _matrix_to_gltf_values(source_matrix)}],
    }, [])

    np.testing.assert_allclose(_matrix_from_node_trs(scene_data.nodes[0]), source_matrix, atol=1e-5)


@pytest.mark.parametrize(
    ("node", "message"),
    [
        ({"matrix": [1.0] * 16, "translation": [0.0, 0.0, 0.0]}, "both matrix and TRS"),
        ({"matrix": [1.0, 0.0, 0.0, 0.0] * 4}, "matrix must be affine"),
        ({"matrix": [1.0, 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]}, "contains shear"),
    ],
)
def test_load_gltf_rejects_nonrepresentable_matrix_nodes(node, message):
    with pytest.raises(ValueError, match=message):
        _build_scene_data({"nodes": [node]}, [])


def test_load_gltf_rejects_matrix_animation_target():
    with pytest.raises(ValueError, match="must not be an animation target"):
        _build_scene_data({
            "nodes": [{"matrix": _matrix_to_gltf_values(np.eye(4, dtype=np.float32))}],
            "animations": [{"channels": [{"target": {"node": 0, "path": "translation"}}]}],
        }, [])


def test_load_gltf_normalizes_quantized_attributes_without_normalizing_joints_or_indices():
    positions = struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    normals = struct.pack("<12b", *([-128, 0, 127, 99] * 3))
    uvs = struct.pack("<6H", 0, 65535, 32768, 16384, 65535, 0)
    joints = struct.pack("<12B", *(range(12)))
    weights = struct.pack("<12B", *([0, 64, 128, 255] * 3))
    indices = struct.pack("<3H", 0, 1, 2)
    chunks = [positions, normals, uvs, joints, weights, indices]
    payload = b"".join(chunks)
    offsets = []
    offset = 0
    for chunk in chunks:
        offsets.append(offset)
        offset += len(chunk)

    gltf = {
        "asset": {"version": "2.0"},
        "extensionsUsed": ["KHR_mesh_quantization"],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4},
            "indices": 5,
        }]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5120, "count": 3, "type": "VEC3", "normalized": True},
            {"bufferView": 2, "componentType": 5123, "count": 3, "type": "VEC2", "normalized": True},
            {"bufferView": 3, "componentType": 5121, "count": 3, "type": "VEC4"},
            {"bufferView": 4, "componentType": 5121, "count": 3, "type": "VEC4", "normalized": True},
            {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offset, "byteLength": len(chunk), **({"byteStride": 4} if index == 1 else {})}
            for index, (offset, chunk) in enumerate(zip(offsets, chunks, strict=True))
        ],
    }

    mesh = _build_scene_data(gltf, [payload]).meshes[0]
    np.testing.assert_allclose(mesh.normals[0], [-1.0, 0.0, 1.0], atol=1e-6)
    np.testing.assert_allclose(mesh.uvs[0], [0.0, 1.0], atol=1e-6)
    np.testing.assert_allclose(mesh.joint_weights[0], [0.0, 64.0 / 255.0, 128.0 / 255.0, 1.0], atol=1e-6)
    assert mesh.normals.dtype == np.float32
    assert mesh.uvs.dtype == np.float32
    assert mesh.joint_weights.dtype == np.float32
    assert mesh.joint_indices.dtype == np.uint32
    assert mesh.joint_indices[2].tolist() == [8, 9, 10, 11]
    assert mesh.indices.dtype == np.uint32
    assert mesh.indices.tolist() == [0, 1, 2]


def test_read_accessor_rejects_sparse_data_without_buffer_view():
    with pytest.raises(ValueError, match="sparse accessor 0 is not supported"):
        _read_accessor({"accessors": [{"sparse": {}}]}, [], 0)
