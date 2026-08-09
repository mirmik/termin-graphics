import gc
import json
from pathlib import Path
import struct

import numpy as np
import pytest

from termin.glb import GLBAsset, NativePrimitiveInfo, NativeStaticMeshDocument
from termin.glb import _glb_native
from termin.glb.instantiator import instantiate_glb
from termin.glb.native import NativeGLBSceneData
from termin.default_assets.resource_manager import DefaultResourceManager


_FIXTURES = Path(__file__).parents[2] / "termin-thirdparty" / "cgltf" / "fuzz" / "data"


def _write_glb(path: Path, document: dict, binary: bytes) -> Path:
    document["buffers"] = [{"byteLength": len(binary)}]
    json_data = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    binary_data = binary + b"\0" * (-len(binary) % 4)
    total_size = 12 + 8 + len(json_data) + 8 + len(binary_data)
    path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, total_size)
        + struct.pack("<II", len(json_data), 0x4E4F534A)
        + json_data
        + struct.pack("<II", len(binary_data), 0x004E4942)
        + binary_data
    )
    return path


def _write_bulk_animation_glb(path: Path) -> Path:
    times = struct.pack("<2f", 0.0, 1.0)
    translations = struct.pack("<6f", 1.0, 2.0, 3.0, 7.0, 8.0, 9.0)
    scales = struct.pack("<6f", 1.0, 2.0, 3.0, 3.0, 6.0, 9.0)
    binary = times + translations + scales
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(times)},
                {
                    "buffer": 0,
                    "byteOffset": len(times),
                    "byteLength": len(translations),
                },
                {
                    "buffer": 0,
                    "byteOffset": len(times) + len(translations),
                    "byteLength": len(scales),
                },
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR"},
                {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"},
                {"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC3"},
            ],
            "nodes": [{"name": "Animated"}],
            "animations": [
                {
                    "name": "ExactTracks",
                    "samplers": [
                        {"input": 0, "output": 1, "interpolation": "STEP"},
                        {"input": 0, "output": 2, "interpolation": "LINEAR"},
                    ],
                    "channels": [
                        {"sampler": 0, "target": {"node": 0, "path": "translation"}},
                        {"sampler": 1, "target": {"node": 0, "path": "scale"}},
                    ],
                }
            ],
        },
        binary,
    )


def _write_column_major_skin_glb(path: Path) -> Path:
    inverse_bind = struct.pack(
        "<16f",
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        -2.0, -3.0, -4.0, 1.0,
    )
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(inverse_bind)},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 1, "type": "MAT4"},
            ],
            "nodes": [{"name": "Joint"}],
            "skins": [{"name": "Skin", "joints": [0], "inverseBindMatrices": 0}],
        },
        inverse_bind,
    )


def _write_production_native_glb(
    path: Path, *, duplicate_animation_name: bool = False
) -> Path:
    positions = struct.pack(
        "<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0
    )
    indices = struct.pack("<3H", 0, 1, 2)
    times = struct.pack("<2f", 0.0, 1.0)
    translations = struct.pack("<6f", 0.0, 0.0, 0.0, 2.0, 3.0, 4.0)
    chunks = (positions, indices, times, translations)
    offsets = []
    binary = b""
    for chunk in chunks:
        offsets.append(len(binary))
        binary += chunk
    animation = {
        "name": "Move",
        "samplers": [{"input": 2, "output": 3, "interpolation": "STEP"}],
        "channels": [
            {
                "sampler": 0,
                "target": {"node": 1, "path": "translation"},
            }
        ],
    }
    animations = [animation]
    if duplicate_animation_name:
        animations.append(animation.copy())
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "bufferViews": [
                {
                    "buffer": 0,
                    "byteOffset": offset,
                    "byteLength": len(chunk),
                }
                for offset, chunk in zip(offsets, chunks, strict=True)
            ],
            "accessors": [
                {
                    "bufferView": 0,
                    "componentType": 5126,
                    "count": 3,
                    "type": "VEC3",
                },
                {
                    "bufferView": 1,
                    "componentType": 5123,
                    "count": 3,
                    "type": "SCALAR",
                },
                {
                    "bufferView": 2,
                    "componentType": 5126,
                    "count": 2,
                    "type": "SCALAR",
                },
                {
                    "bufferView": 3,
                    "componentType": 5126,
                    "count": 2,
                    "type": "VEC3",
                },
            ],
            "meshes": [
                {
                    "name": "Body",
                    "primitives": [
                        {
                            "attributes": {"POSITION": 0},
                            "indices": 1,
                            "mode": 4,
                        }
                    ],
                }
            ],
            "nodes": [
                {"name": "Root", "scale": [2.0, 2.0, 2.0], "children": [1]},
                {"name": "Animated", "translation": [1.0, 0.0, 0.0]},
                {"name": "MeshResource", "mesh": 0},
            ],
            "animations": animations,
        },
        binary,
    )


def _write_indexed_triangle_glb(path: Path, component_type: int | None, indices=(0, 1, 2)) -> Path:
    positions = b"".join(
        struct.pack("<4f", *position, 99.0)
        for position in ((1.0, 2.0, 3.0), (4.0, 5.0, 6.0), (7.0, 8.0, 9.0))
    )
    buffer_views = [{"buffer": 0, "byteOffset": 0, "byteLength": len(positions), "byteStride": 16}]
    accessors = [
        {
            "bufferView": 0,
            "componentType": 5126,
            "count": 3,
            "type": "VEC3",
        }
    ]
    primitive = {"attributes": {"POSITION": 0}, "mode": 4}
    binary = positions
    if component_type is not None:
        formats = {5121: "B", 5123: "H", 5125: "I"}
        index_data = struct.pack("<3" + formats[component_type], *indices)
        index_offset = len(binary)
        binary += index_data
        buffer_views.append(
            {"buffer": 0, "byteOffset": index_offset, "byteLength": len(index_data)}
        )
        accessors.append(
            {
                "bufferView": 1,
                "componentType": component_type,
                "count": 3,
                "type": "SCALAR",
            }
        )
        primitive["indices"] = 1
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": buffer_views,
            "accessors": accessors,
            "meshes": [{"name": "StridedTriangle", "primitives": [primitive]}],
        },
        binary,
    )


def _write_sparse_triangle_glb(path: Path) -> Path:
    sparse_indices = struct.pack("<3B", 0, 1, 2)
    sparse_values = struct.pack(
        "<9f", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0
    )
    binary = sparse_indices + b"\0" + sparse_values
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 3},
                {"buffer": 0, "byteOffset": 4, "byteLength": len(sparse_values)},
            ],
            "accessors": [
                {
                    "componentType": 5126,
                    "count": 3,
                    "type": "VEC3",
                    "sparse": {
                        "count": 3,
                        "indices": {"bufferView": 0, "componentType": 5121},
                        "values": {"bufferView": 1},
                    },
                }
            ],
            "meshes": [
                {
                    "name": "SparseTriangle",
                    "primitives": [{"attributes": {"POSITION": 0}, "mode": 4}],
                }
            ],
        },
        binary,
    )


def _write_normalized_tangent_glb(path: Path, *, include_tangent: bool = True) -> Path:
    positions = struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    normals = struct.pack("<9b", 127, 0, -128, 127, 0, -128, 127, 0, -128)
    uvs = struct.pack("<6H", 0, 0, 65535, 0, 0, 65535)
    tangents = struct.pack("<12b", 127, 0, 0, 127, 127, 0, 0, 127, 127, 0, 0, -128)
    binary = positions + normals + b"\0" * 3 + uvs + tangents
    attributes = {
        "POSITION": 0,
        "NORMAL": 1,
        "TEXCOORD_0": 2,
    }
    if include_tangent:
        attributes["TANGENT"] = 3
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
                {"buffer": 0, "byteOffset": 36, "byteLength": len(normals)},
                {"buffer": 0, "byteOffset": 48, "byteLength": len(uvs)},
                {"buffer": 0, "byteOffset": 60, "byteLength": len(tangents)},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                {
                    "bufferView": 1,
                    "componentType": 5120,
                    "normalized": True,
                    "count": 3,
                    "type": "VEC3",
                },
                {
                    "bufferView": 2,
                    "componentType": 5123,
                    "normalized": True,
                    "count": 3,
                    "type": "VEC2",
                },
                {
                    "bufferView": 3,
                    "componentType": 5120,
                    "normalized": True,
                    "count": 3,
                    "type": "VEC4",
                },
            ],
            "meshes": [
                {
                    "name": "NormalizedTangent",
                    "primitives": [
                        {
                            "attributes": attributes,
                            "mode": 4,
                        }
                    ],
                }
            ],
        },
        binary,
    )


def _write_multi_primitive_glb(path: Path) -> Path:
    first = struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    second = struct.pack("<9f", 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 2.0, 1.0, 0.0)
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(first)},
                {"buffer": 0, "byteOffset": len(first), "byteLength": len(second)},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            ],
            "materials": [{"name": "First"}, {"name": "Second"}],
            "meshes": [
                {
                    "name": "Multi",
                    "primitives": [
                        {"attributes": {"POSITION": 0}, "material": 0, "mode": 4},
                        {"attributes": {"POSITION": 1}, "material": 1, "mode": 4},
                    ],
                }
            ],
        },
        first + second,
    )


def _write_material_texture_glb(path: Path, *, texture_transform: bool = False) -> Path:
    positions = struct.pack("<9f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    fallback = b"PNG-fallback"
    webp = b"WEBP-selected"
    base_color_view = {"index": 0, "texCoord": 0}
    if texture_transform:
        base_color_view["extensions"] = {
            "KHR_texture_transform": {"offset": [0.25, 0.5]}
        }
    binary = positions + fallback + webp
    return _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "extensionsUsed": ["EXT_texture_webp"],
            "extensionsRequired": ["EXT_texture_webp"],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
                {
                    "buffer": 0,
                    "byteOffset": len(positions),
                    "byteLength": len(fallback),
                },
                {
                    "buffer": 0,
                    "byteOffset": len(positions) + len(fallback),
                    "byteLength": len(webp),
                },
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}
            ],
            "images": [
                {"name": "Fallback", "mimeType": "image/png", "bufferView": 1},
                {"name": "SelectedWebP", "mimeType": "image/webp", "bufferView": 2},
            ],
            "samplers": [
                {"magFilter": 9728, "minFilter": 9987, "wrapS": 33071, "wrapT": 33648}
            ],
            "textures": [
                {
                    "name": "WebPTexture",
                    "source": 0,
                    "sampler": 0,
                    "extensions": {"EXT_texture_webp": {"source": 1}},
                },
                {"name": "DefaultSampler", "source": 1},
            ],
            "materials": [
                {
                    "name": "NativePBR",
                    "pbrMetallicRoughness": {
                        "baseColorFactor": [0.1, 0.2, 0.3, 0.4],
                        "baseColorTexture": base_color_view,
                        "metallicFactor": 0.6,
                        "roughnessFactor": 0.7,
                        "metallicRoughnessTexture": {"index": 1},
                    },
                    "normalTexture": {"index": 1, "scale": 0.8},
                    "occlusionTexture": {"index": 1, "strength": 0.9},
                    "emissiveTexture": {"index": 0},
                    "emissiveFactor": [0.4, 0.5, 0.6],
                    "alphaMode": "MASK",
                    "alphaCutoff": 0.25,
                    "doubleSided": True,
                }
            ],
            "meshes": [
                {
                    "name": "MaterialTriangle",
                    "primitives": [
                        {"attributes": {"POSITION": 0}, "material": 0, "mode": 4}
                    ],
                }
            ],
        },
        binary,
    )


def test_native_backend_reports_pinned_cgltf_revision():
    info = _glb_native.backend_info()

    assert info == {
        "name": "cgltf",
        "cgltf_version": "1.15",
        "cgltf_revision": "85cd62382dfea638278962690cf515023f33ed00",
    }
    assert _glb_native.error_code_name(_glb_native.NativeErrorCode.UNSUPPORTED) == "unsupported"


def test_native_document_discovers_and_builds_box_without_python_geometry_arrays():
    document = NativeStaticMeshDocument(_FIXTURES / "Box.glb")

    assert document.meshes == (
        type(document.meshes[0])(
            name="Mesh",
            primitive_count=1,
            vertex_count=24,
            index_count=36,
            skinned=False,
            primitives=(
                NativePrimitiveInfo(
                    first_index=0,
                    index_count=36,
                    material_index=0,
                    material_slot=0,
                ),
            ),
        ),
    )

    raw = document.build_mesh(0, "pytest-native-box-raw", convert_to_z_up=False)
    converted = document.build_mesh(0, "pytest-native-box-z-up", convert_to_z_up=True)

    assert raw.vertex_count == converted.vertex_count == 24
    assert raw.index_count == converted.index_count == 36
    assert raw.submesh_count == converted.submesh_count == 1
    assert raw.stride == converted.stride == 24
    assert raw.submeshes[0].name == "Mesh/Red"

    raw_vertices = np.asarray(raw.mesh.get_vertices_buffer()).reshape(24, 6)
    converted_vertices = np.asarray(converted.mesh.get_vertices_buffer()).reshape(24, 6)
    np.testing.assert_array_equal(converted_vertices[:, 0], raw_vertices[:, 0])
    np.testing.assert_array_equal(converted_vertices[:, 1], -raw_vertices[:, 2])
    np.testing.assert_array_equal(converted_vertices[:, 2], raw_vertices[:, 1])
    np.testing.assert_array_equal(converted_vertices[:, 3], raw_vertices[:, 3])
    np.testing.assert_array_equal(converted_vertices[:, 4], -raw_vertices[:, 5])
    np.testing.assert_array_equal(converted_vertices[:, 5], raw_vertices[:, 4])


def test_native_document_errors_include_source_path(tmp_path):
    source = _FIXTURES / "Box.glb"
    truncated = tmp_path / "truncated.glb"
    truncated.write_bytes(source.read_bytes()[:32])

    with pytest.raises(RuntimeError, match="truncated\\.glb"):
        NativeStaticMeshDocument(truncated)

    with pytest.raises(RuntimeError, match="binary GLB only"):
        NativeStaticMeshDocument(_FIXTURES / "TriangleWithoutIndices.gltf")


def test_native_document_repeated_mapping_lifetime():
    for _ in range(64):
        document = NativeStaticMeshDocument(_FIXTURES / "Box.glb")
        assert document.meshes[0].vertex_count == 24
        del document
    gc.collect()


@pytest.mark.parametrize("component_type", [5121, 5123, 5125, None])
def test_native_static_mesh_supports_index_widths_nonindexed_and_stride(tmp_path, component_type):
    path = _write_indexed_triangle_glb(tmp_path / f"triangle-{component_type}.glb", component_type)
    document = NativeStaticMeshDocument(path)
    mesh = document.build_mesh(
        0,
        f"pytest-native-index-{component_type}",
        convert_to_z_up=True,
    )

    assert (mesh.vertex_count, mesh.index_count, mesh.stride) == (3, 3, 12)
    np.testing.assert_array_equal(np.asarray(mesh.mesh.get_indices_buffer()), [0, 1, 2])
    vertices = np.asarray(mesh.mesh.get_vertices_buffer()).reshape(3, 3)
    np.testing.assert_array_equal(
        vertices[:, :3],
        [[1.0, -3.0, 2.0], [4.0, -6.0, 5.0], [7.0, -9.0, 8.0]],
    )


def test_native_static_mesh_reads_sparse_position_accessor(tmp_path):
    document = NativeStaticMeshDocument(_write_sparse_triangle_glb(tmp_path / "sparse.glb"))
    mesh = document.build_mesh(0, "pytest-native-sparse", convert_to_z_up=False)

    vertices = np.asarray(mesh.mesh.get_vertices_buffer()).reshape(3, 3)
    np.testing.assert_array_equal(
        vertices[:, :3],
        [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [7.0, 8.0, 9.0]],
    )


def test_native_static_mesh_decodes_normalized_attributes_and_tangents(tmp_path):
    path = _write_normalized_tangent_glb(tmp_path / "normalized-tangent.glb")
    mesh = NativeStaticMeshDocument(path).build_mesh(
        0,
        "pytest-native-normalized-tangent",
        convert_to_z_up=False,
    )

    assert mesh.stride == 48
    vertices = np.asarray(mesh.mesh.get_vertices_buffer()).reshape(3, 12)
    np.testing.assert_allclose(vertices[:, 3:6], [[1.0, 0.0, -1.0]] * 3)
    np.testing.assert_allclose(vertices[:, 6:8], [[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]])
    np.testing.assert_allclose(vertices[:, 8:11], [[1.0, 0.0, 0.0]] * 3)
    np.testing.assert_allclose(vertices[:, 11], [1.0, 1.0, -1.0])


def test_native_static_mesh_generates_pbr_tangents_when_uvs_are_present(tmp_path):
    path = _write_normalized_tangent_glb(
        tmp_path / "generated-tangent.glb",
        include_tangent=False,
    )
    mesh = NativeStaticMeshDocument(path).build_mesh(
        0,
        "pytest-native-generated-tangent",
        convert_to_z_up=False,
    )

    assert mesh.stride == 48
    vertices = np.asarray(mesh.mesh.get_vertices_buffer()).reshape(3, 12)
    np.testing.assert_allclose(vertices[:, 8:11], [[0.0, 0.0, 1.0]] * 3)
    np.testing.assert_allclose(vertices[:, 11], [-1.0, -1.0, -1.0])


def test_native_static_mesh_preserves_primitive_sections_and_material_slots(tmp_path):
    path = _write_multi_primitive_glb(tmp_path / "multi.glb")
    document = NativeStaticMeshDocument(path)
    assert document.meshes[0].primitive_count == 2
    assert [primitive.material_index for primitive in document.meshes[0].primitives] == [0, 1]
    assert [primitive.material_slot for primitive in document.meshes[0].primitives] == [0, 1]
    assert [primitive.first_index for primitive in document.meshes[0].primitives] == [0, 3]
    mesh = document.build_mesh(0, "pytest-native-multi", convert_to_z_up=False)

    assert (mesh.vertex_count, mesh.index_count, mesh.submesh_count) == (6, 6, 2)
    np.testing.assert_array_equal(np.asarray(mesh.mesh.get_indices_buffer()), [0, 1, 2, 3, 4, 5])
    assert [section.material_slot for section in mesh.submeshes] == [0, 1]
    assert [section.name for section in mesh.submeshes] == ["Multi/First", "Multi/Second"]


def test_native_static_mesh_reports_context_for_out_of_range_index(tmp_path):
    path = _write_indexed_triangle_glb(
        tmp_path / "invalid-index.glb",
        5121,
        indices=(0, 1, 9),
    )
    with pytest.raises(
        RuntimeError,
        match=r"invalid-index\.glb: mesh\[0\] primitive\[0\] INDICES accessor index\[2\]=9",
    ):
        NativeStaticMeshDocument(path)


def test_native_material_texture_discovery_selects_webp_and_preserves_sampler(tmp_path):
    path = _write_material_texture_glb(tmp_path / "materials.glb")
    document = NativeStaticMeshDocument(path)

    assert [image.encoded_size for image in document.images] == [12, 13]
    assert document.image_payload(0) == b"PNG-fallback"
    assert document.image_payload(1) == b"WEBP-selected"
    assert document.textures[0].image_index == 1
    assert document.textures[0].selected_webp
    assert (
        document.textures[0].mag_filter,
        document.textures[0].min_filter,
        document.textures[0].wrap_s,
        document.textures[0].wrap_t,
    ) == (9728, 9987, 33071, 33648)
    assert document.textures[1].sampler_index is None
    assert (
        document.textures[1].mag_filter,
        document.textures[1].min_filter,
        document.textures[1].wrap_s,
        document.textures[1].wrap_t,
    ) == (9729, 9729, 10497, 10497)

    material = document.materials[0]
    assert material.base_color_factor == pytest.approx((0.1, 0.2, 0.3, 0.4))
    assert (material.metallic_factor, material.roughness_factor) == pytest.approx((0.6, 0.7))
    assert material.base_color_texture.texture_index == 0
    assert material.normal_texture.scale == pytest.approx(0.8)
    assert material.occlusion_texture.scale == pytest.approx(0.9)
    assert material.alpha_mode == 1
    assert material.alpha_cutoff == pytest.approx(0.25)
    assert material.double_sided

    materials, textures = document.build_material_texture_data()
    assert textures[0].data == b"WEBP-selected"
    assert textures[0].name == "SelectedWebP"
    assert textures[0].image_index == 1
    assert textures[0].sampler == {
        "magFilter": 9728,
        "minFilter": 9987,
        "wrapS": 33071,
        "wrapT": 33648,
    }
    assert materials[0].base_color_texture == 0
    assert materials[0].occlusion_strength == pytest.approx(0.9)


def test_native_material_bridge_rejects_texture_transform(tmp_path):
    path = _write_material_texture_glb(tmp_path / "texture-transform.glb", texture_transform=True)
    document = NativeStaticMeshDocument(path)

    assert document.materials[0].base_color_texture.has_transform
    with pytest.raises(RuntimeError, match="KHR_texture_transform"):
        document.build_material_texture_data()


def test_native_document_rejects_unknown_required_extension(tmp_path):
    path = tmp_path / "required-extension.glb"
    positions = struct.pack(
        "<12f",
        1.0,
        2.0,
        3.0,
        99.0,
        4.0,
        5.0,
        6.0,
        99.0,
        7.0,
        8.0,
        9.0,
        99.0,
    )
    _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "extensionsRequired": ["VENDOR_not_supported"],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(positions), "byteStride": 16}
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}
            ],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 4}]}],
        },
        positions,
    )
    with pytest.raises(RuntimeError, match="VENDOR_not_supported"):
        NativeStaticMeshDocument(path)


def test_native_document_rejects_required_basisu_without_ktx2_decoder(tmp_path):
    path = tmp_path / "required-basisu.glb"
    _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "extensionsUsed": ["KHR_texture_basisu"],
            "extensionsRequired": ["KHR_texture_basisu"],
        },
        b"",
    )

    with pytest.raises(RuntimeError, match="KHR_texture_basisu"):
        NativeStaticMeshDocument(path)


def test_native_document_ignores_optional_basisu_and_uses_core_fallback(tmp_path):
    path = tmp_path / "optional-basisu.glb"
    fallback = b"PNG-fallback"
    basisu = b"KTX2-unsupported"
    _write_glb(
        path,
        {
            "asset": {"version": "2.0"},
            "extensionsUsed": ["KHR_texture_basisu"],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(fallback)},
                {
                    "buffer": 0,
                    "byteOffset": len(fallback),
                    "byteLength": len(basisu),
                },
            ],
            "images": [
                {"name": "Fallback", "mimeType": "image/png", "bufferView": 0},
                {"name": "BasisU", "mimeType": "image/ktx2", "bufferView": 1},
            ],
            "textures": [
                {
                    "source": 0,
                    "extensions": {"KHR_texture_basisu": {"source": 1}},
                }
            ],
        },
        fallback + basisu,
    )

    document = NativeStaticMeshDocument(path)

    assert document.textures[0].image_index == 0
    assert not document.textures[0].selected_basisu
    assert document.image_payload(document.textures[0].image_index) == fallback


def test_native_animation_bridge_preserves_step_vec3_scale_and_node_index(tmp_path):
    path = _write_bulk_animation_glb(tmp_path / "bulk-animation.glb")
    document = NativeStaticMeshDocument(path)

    clip = document.build_animation_clip(
        0,
        "pytest-native-glb-bulk-animation",
        convert_to_z_up=True,
    )

    assert clip.name == "ExactTracks"
    assert clip.track_count == 2
    assert clip.tracks[0]["target_node_index"] == 0
    assert clip.tracks[0]["interpolation"] == "step"
    assert clip.sample_track(0, 0.5) == pytest.approx([1.0, -3.0, 2.0])
    assert clip.sample_track(1, 0.5) == pytest.approx([2.0, 6.0, 4.0])


def test_native_skin_keeps_column_major_inverse_bind_storage(tmp_path):
    path = _write_column_major_skin_glb(tmp_path / "column-major-skin.glb")
    document = NativeStaticMeshDocument(path)

    rig = document.rig_data()
    raw_matrix = np.frombuffer(
        rig["skins"][0]["inverse_bind_matrices"], dtype=np.float32
    )
    assert raw_matrix[12:15] == pytest.approx((-2.0, -3.0, -4.0))

    skeleton = document.build_skeleton(
        0,
        "pytest-native-column-major-skeleton",
        convert_to_z_up=False,
    )
    assert skeleton.bones[0]["inverse_bind_matrix"][12:15] == pytest.approx(
        (-2.0, -3.0, -4.0)
    )


def test_glb_asset_default_cgltf_backend_publishes_children_and_node_targets(
    tmp_path, monkeypatch
):
    import termin_assets

    class _Material:
        is_valid = True

    class _InstantiationResourceManager:
        def get_material(self, name):
            assert name == "CookTorrancePBR"
            return _Material()

        def list_runtime_asset_names(self, asset_type):
            assert asset_type == "texture"
            return []

    DefaultResourceManager._reset_for_testing()
    resource_manager = DefaultResourceManager.instance()
    path = _write_production_native_glb(tmp_path / "production-native.glb")
    asset = GLBAsset(name="production-native", source_path=path)
    asset.set_resource_manager(resource_manager)
    asset.parse_spec(
        {
            "uuid": "pytest-production-native-glb",
            "convert_to_z_up": False,
            "normalize_scale": True,
        }
    )

    assert asset.ensure_loaded()
    assert isinstance(asset.scene_data, NativeGLBSceneData)
    assert list(asset.get_mesh_assets()) == ["Body"]
    assert asset.get_mesh_assets()["Body"].data.is_valid
    assert asset.scene_data.meshes[0].submeshes[0].material_index == -1
    assert list(asset.get_animation_assets()) == ["Move"]
    clip = asset.get_animation_assets()["Move"].clip
    assert clip.track_count == 1
    assert clip.tracks[0]["target_node_index"] == 1
    assert clip.tracks[0]["interpolation"] == "step"
    assert clip.sample_track(0, 1.0) == pytest.approx((4.0, 6.0, 8.0))

    monkeypatch.setattr(
        termin_assets,
        "get_resource_manager",
        lambda: _InstantiationResourceManager(),
    )
    result = instantiate_glb(asset, name="Model")

    imported_root = result.entity.transform.children[0].entity
    animated = imported_root.transform.children[0].entity
    assert imported_root.name == "Root"
    assert tuple(imported_root.transform.local_scale()) == pytest.approx((1.0, 1.0, 1.0))
    assert animated.name == "Animated"
    assert tuple(animated.transform.local_position()) == pytest.approx((2.0, 0.0, 0.0))
    assert result.animation_player is not None
    assert result.animation_player.node_targets[1].name == "Animated"
    assert result.animation_player.node_targets[2] is None


def test_glb_asset_cgltf_backend_rejects_duplicate_animation_names(tmp_path):
    path = _write_production_native_glb(
        tmp_path / "duplicate-animation.glb", duplicate_animation_name=True
    )
    document = NativeStaticMeshDocument(path)

    with pytest.raises(
        RuntimeError,
        match=r"duplicate-animation\.glb: duplicate animation names.*'Move'",
    ):
        NativeGLBSceneData(
            document,
            convert_to_z_up=False,
            blender_z_up_fix=False,
            normalize_scale=False,
        )


def test_glb_asset_default_cgltf_failure_never_falls_back_to_python(
    tmp_path, monkeypatch
):
    import termin.glb.loader as legacy_loader

    path = tmp_path / "malformed.glb"
    path.write_bytes(b"not a GLB")
    legacy_called = False

    def fail_if_called(*_args, **_kwargs):
        nonlocal legacy_called
        legacy_called = True
        raise AssertionError("legacy GLB loader must not be called")

    monkeypatch.setattr(legacy_loader, "load_glb_file_from_buffer", fail_if_called)
    monkeypatch.setattr(legacy_loader, "load_glb_file_normalized", fail_if_called)
    asset = GLBAsset(name="malformed", source_path=path)

    assert not asset.ensure_loaded()
    assert not legacy_called
