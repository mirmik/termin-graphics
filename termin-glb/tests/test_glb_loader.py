import json
import struct

import numpy as np
import pytest

from termin.default_assets.resource_manager import DefaultResourceManager
from termin.glb.asset import GLBAsset
from termin.glb.instantiator import _glb_mesh_to_tc_mesh
from termin.glb.loader import load_glb_file


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
