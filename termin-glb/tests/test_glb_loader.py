import json
import struct

import pytest

from termin.default_assets.resource_manager import DefaultResourceManager
from termin.glb.asset import GLBAsset
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


def test_glb_asset_loads_gltf_from_source_path(tmp_path):
    DefaultResourceManager._reset_for_testing()
    rm = DefaultResourceManager.instance()
    gltf_path = _write_triangle_gltf(tmp_path)
    asset = GLBAsset(scene_data=None, name="triangle", source_path=gltf_path)
    asset.set_resource_manager(rm)

    assert asset.ensure_loaded()
    assert asset.scene_data is not None
    assert len(asset.scene_data.meshes) == 1
