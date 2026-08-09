import os
from pathlib import Path
import struct

import numpy as np
import pytest

from termin.glb import GLBSceneData, NativeStaticMeshDocument
from termin.glb.instantiator import _plan_texture_imports
from termin.image import decode_rgba8


_GEOMETRY_ONLY = Path(
    os.environ.get(
        "TERMIN_GLB_GEOMETRY_REFERENCE",
        "/home/mirmik/project/Hunyuan3D-Omni-test/"
        "pixal3d_shape_stages_experiment/front_seed42_1536/shape_1536.glb",
    )
)
_TEXTURED = Path(
    os.environ.get(
        "TERMIN_GLB_TEXTURED_REFERENCE",
        "/home/mirmik/project/Hunyuan3D-Omni-test/"
        "pixal3d_full_1536_experiment/character_full_1536.glb",
    )
)
_ARTHUR = Path(
    os.environ.get(
        "TERMIN_GLB_ARTHUR_REFERENCE",
        "/home/mirmik/project/chronosquad-termin/Models/Arthur/ArthurDecimated.glb",
    )
)
_FNV_OFFSET = 14695981039346656037
_FNV_PRIME = 1099511628211
_MISSING_INDEX = 0xFFFFFFFFFFFFFFFF


def _fnv1a(data: bytes, value: int = _FNV_OFFSET) -> int:
    for byte in data:
        value ^= byte
        value = value * _FNV_PRIME & 0xFFFFFFFFFFFFFFFF
    return value


def _texture_index(view) -> int:
    return _MISSING_INDEX if view is None else view.texture_index


def _require_reference(path: Path) -> None:
    if not path.is_file():
        pytest.skip(f"local GLB reference fixture is unavailable: {path}")


def test_native_geometry_only_pixal3d_reference():
    _require_reference(_GEOMETRY_ONLY)
    document = NativeStaticMeshDocument(_GEOMETRY_ONLY)

    assert sum(mesh.vertex_count for mesh in document.meshes) == 2_823_922
    assert sum(mesh.index_count for mesh in document.meshes) == 17_280_924
    assert len(document.meshes) == 1

    mesh, diagnostics = document.build_mesh_with_diagnostics(
        0,
        "reference-pixal3d-geometry-only",
        convert_to_z_up=False,
    )
    assert mesh.stride == 12
    assert diagnostics.payload_bytes == 103_010_760
    assert diagnostics.payload_hash == 5_462_061_671_071_880_564


def test_native_textured_pixal3d_geometry_reference():
    _require_reference(_TEXTURED)
    document = NativeStaticMeshDocument(_TEXTURED)

    assert sum(mesh.vertex_count for mesh in document.meshes) == 753_518
    assert sum(mesh.index_count for mesh in document.meshes) == 2_901_741

    geometry_hash = _FNV_OFFSET
    payload_bytes = 0
    for mesh_index, mesh_info in enumerate(document.meshes):
        mesh, diagnostics = document.build_mesh_with_diagnostics(
            mesh_index,
            f"reference-pixal3d-textured-{mesh_index}",
            name=mesh_info.name,
            convert_to_z_up=False,
        )
        assert mesh.stride == 48
        payload_bytes += diagnostics.payload_bytes
        geometry_hash = diagnostics.payload_hash ^ (geometry_hash * _FNV_PRIME & 0xFFFFFFFFFFFFFFFF)

    assert payload_bytes == 47_775_828
    assert geometry_hash == 2_595_364_662_792_861_569

    image_hash = _FNV_OFFSET
    image_bytes = 0
    for image_info in document.images:
        payload = document.image_payload(image_info.index)
        image_hash = _fnv1a(payload, image_hash)
        image_bytes += len(payload)

    material_hash = _FNV_OFFSET
    for material in document.materials:
        material_hash = _fnv1a(struct.pack("<4f", *material.base_color_factor), material_hash)
        material_hash = _fnv1a(struct.pack("<f", material.metallic_factor), material_hash)
        material_hash = _fnv1a(struct.pack("<f", material.roughness_factor), material_hash)
        material_hash = _fnv1a(
            struct.pack(
                "<5Q",
                _texture_index(material.base_color_texture),
                _texture_index(material.metallic_roughness_texture),
                _texture_index(material.normal_texture),
                _texture_index(material.occlusion_texture),
                _texture_index(material.emissive_texture),
            ),
            material_hash,
        )
        material_hash = _fnv1a(struct.pack("<3f", *material.emissive_factor), material_hash)
        material_hash = _fnv1a(
            struct.pack(
                "<3I",
                material.alpha_mode,
                int(material.double_sided),
                int(material.unlit),
            ),
            material_hash,
        )
        material_hash = _fnv1a(struct.pack("<f", material.alpha_cutoff), material_hash)
        material_hash = _fnv1a(struct.pack("<f", material.ior), material_hash)
        material_hash = _fnv1a(struct.pack("<f", material.specular_factor), material_hash)
        material_hash = _fnv1a(
            struct.pack("<3f", *material.specular_color_factor), material_hash
        )

    texture_hash = _FNV_OFFSET
    for texture in document.textures:
        texture_hash = _fnv1a(
            struct.pack(
                "<2Q",
                texture.image_index,
                _MISSING_INDEX if texture.sampler_index is None else texture.sampler_index,
            ),
            texture_hash,
        )

    assert image_bytes == 3_604_320
    assert image_hash == 6_822_446_291_040_907_706
    assert material_hash == 765_218_557_742_926_444
    assert texture_hash == 3_171_725_606_409_677_908

    materials, textures = document.build_material_texture_data()
    scene = GLBSceneData()
    scene.materials = list(materials)
    scene.textures = list(textures)
    imports = _plan_texture_imports(scene)
    assert [(item.encoding, item.texture_indices) for item in imports] == [
        ("srgb", (0,)),
        ("linear", (1,)),
    ]
    assert [texture.mime_type for texture in textures] == ["image/webp", "image/webp"]
    decoded_sizes = []
    for texture in textures:
        decoded = decode_rgba8(texture.data, texture.name)
        decoded_sizes.append((decoded.width, decoded.height, decoded.channels))
    assert decoded_sizes == [(4096, 4096, 4), (4096, 4096, 4)]


def test_native_arthur_skinned_geometry_and_bulk_rig_reference():
    _require_reference(_ARTHUR)
    document = NativeStaticMeshDocument(_ARTHUR)

    assert len(document.meshes) == 20
    assert all(mesh.skinned for mesh in document.meshes)
    assert sum(mesh.vertex_count for mesh in document.meshes) == 118_693
    assert sum(mesh.index_count for mesh in document.meshes) == 573_963

    geometry_hash = _FNV_OFFSET
    payload_bytes = 0
    for mesh_index, mesh_info in enumerate(document.meshes):
        mesh, diagnostics = document.build_mesh_with_diagnostics(
            mesh_index,
            f"reference-arthur-{mesh_index}",
            name=mesh_info.name,
            convert_to_z_up=False,
        )
        assert mesh.stride == 80
        payload_bytes += diagnostics.payload_bytes
        geometry_hash = diagnostics.payload_hash ^ (
            geometry_hash * _FNV_PRIME & 0xFFFFFFFFFFFFFFFF
        )

    assert payload_bytes == 11_791_292
    assert geometry_hash == 14_695_474_819_576_827_200

    rig = document.rig_data()
    assert len(rig["nodes"]) == 101
    assert [index for index, node in enumerate(rig["nodes"]) if node["default_scene_root"]] == [100]
    assert len(rig["skins"]) == 1
    assert len(rig["skins"][0]["joints"]) == 80
    assert rig["skins"][0]["skeleton_node_index"] is None
    assert len(rig["animations"]) == 53
    assert rig["sampler_count"] == 12_702
    assert rig["channel_count"] == 12_702
    assert rig["rig_hash"] == 11_954_369_855_452_468_884

    interpolation_counts: dict[int, int] = {}
    path_counts: dict[int, int] = {}
    values = np.frombuffer(rig["values"], dtype=np.float32)
    input_sample_count = 0
    output_float_count = 0
    nonuniform_scale_values = 0
    for animation in rig["animations"]:
        for sampler in animation["samplers"]:
            interpolation = sampler["interpolation"]
            interpolation_counts[interpolation] = interpolation_counts.get(interpolation, 0) + 1
            input_sample_count += sampler["time_count"]
            output_float_count += sampler["value_count"]
        for channel in animation["channels"]:
            path = channel["target_path"]
            path_counts[path] = path_counts.get(path, 0) + 1
            if path != 3:  # cgltf_animation_path_type_scale
                continue
            sampler = animation["samplers"][channel["sampler_index"]]
            start = sampler["value_first"]
            scale_values = values[start : start + sampler["value_count"]].reshape(
                -1, sampler["components"]
            )
            nonuniform_scale_values += int(
                np.count_nonzero(
                    np.any(np.abs(scale_values - scale_values[:, :1]) > 1.0e-6, axis=1)
                )
            )

    assert interpolation_counts == {0: 8_910, 1: 3_792}
    assert path_counts == {1: 4_234, 2: 4_234, 3: 4_234}
    assert input_sample_count == 162_247
    assert output_float_count == 628_924
    assert nonuniform_scale_values == 683

    published_track_count = 0
    found_step_boundary = False
    found_nonuniform_scale = False
    for animation_index, animation in enumerate(rig["animations"]):
        clip = document.build_animation_clip(
            animation_index,
            f"reference-arthur-animation-{animation_index}",
            convert_to_z_up=False,
        )
        assert clip.track_count == len(animation["channels"])
        published_track_count += clip.track_count
        if found_step_boundary and found_nonuniform_scale:
            continue
        for track_index, track in enumerate(clip.tracks):
            if (
                not found_step_boundary
                and track["interpolation"] == "step"
                and len(track["times"]) >= 2
            ):
                before = np.nextafter(track["times"][1], track["times"][0])
                assert clip.sample_track(track_index, before) == pytest.approx(
                    track["values"][: track["components"]]
                )
                assert clip.sample_track(track_index, track["times"][1]) == pytest.approx(
                    track["values"][track["components"] : 2 * track["components"]]
                )
                found_step_boundary = True
            if not found_nonuniform_scale and track["path"] == "scale":
                scale_values = np.asarray(track["values"], dtype=np.float64).reshape(-1, 3)
                candidates = np.flatnonzero(
                    np.any(np.abs(scale_values - scale_values[:, :1]) > 1.0e-6, axis=1)
                )
                if candidates.size:
                    key_index = int(candidates[0])
                    assert clip.sample_track(
                        track_index, track["times"][key_index]
                    ) == pytest.approx(scale_values[key_index])
                    found_nonuniform_scale = True
            if found_step_boundary and found_nonuniform_scale:
                break

    assert published_track_count == 12_702
    assert found_step_boundary
    assert found_nonuniform_scale

    raw_prepared = document.prepared_rig_data(convert_to_z_up=False)
    converted = document.prepared_rig_data(convert_to_z_up=True)
    conversion = np.asarray(
        [[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]],
        dtype=np.float64,
    )
    assert converted["root_nodes"] == (100,)
    assert np.allclose(
        converted["skins"][0]["inverse_bind_matrices"],
        conversion
        @ raw_prepared["skins"][0]["inverse_bind_matrices"]
        @ conversion.T,
    )

    skeleton = document.build_skeleton(
        0,
        "reference-arthur-skeleton",
        convert_to_z_up=True,
    )
    assert skeleton.bone_count == 80
    assert skeleton.root_count >= 1
    assert len(skeleton.bones) == 80
    joints = converted["skins"][0]["joints"]
    joint_to_bone = {node_index: index for index, node_index in enumerate(joints)}
    for bone_index, node_index in enumerate(joints):
        ancestor = converted["nodes"][node_index]["parent_index"]
        while ancestor is not None and ancestor not in joint_to_bone:
            ancestor = converted["nodes"][ancestor]["parent_index"]
        expected_parent = -1 if ancestor is None else joint_to_bone[ancestor]
        assert skeleton.bones[bone_index]["parent_index"] == expected_parent
        assert skeleton.bones[bone_index]["bind_scale"] == pytest.approx(
            converted["nodes"][node_index]["scale"]
        )

    blender_prepared = document.prepared_rig_data(
        convert_to_z_up=True,
        blender_z_up_fix=True,
    )
    assert np.allclose(
        blender_prepared["skins"][0]["inverse_bind_matrices"],
        converted["skins"][0]["inverse_bind_matrices"],
    )
    root_joint = joints[0]
    converted_joint = converted["nodes"][root_joint]
    blender_joint = blender_prepared["nodes"][root_joint]
    assert blender_joint["translation"] == pytest.approx(
        (
            converted_joint["translation"][0],
            -converted_joint["translation"][2],
            converted_joint["translation"][1],
        )
    )

    root_joint_animation = next(
        index
        for index, animation in enumerate(rig["animations"])
        if any(
            channel["target_node_index"] == root_joint
            for channel in animation["channels"]
        )
    )
    ordinary_clip = document.build_animation_clip(
        root_joint_animation,
        "reference-arthur-root-joint-ordinary",
        convert_to_z_up=True,
    )
    blender_clip = document.build_animation_clip(
        root_joint_animation,
        "reference-arthur-root-joint-blender",
        convert_to_z_up=True,
        blender_z_up_fix=True,
    )
    track_index = next(
        index
        for index, track in enumerate(ordinary_clip.tracks)
        if track["target_node_index"] == root_joint
    )
    ordinary_track = ordinary_clip.tracks[track_index]
    blender_track = blender_clip.tracks[track_index]
    ordinary_values = np.asarray(ordinary_track["values"], dtype=np.float64).reshape(
        -1, ordinary_track["components"]
    )
    expected_values = ordinary_values.copy()
    if ordinary_track["path"] == "translation":
        expected_values[:, 1] = -ordinary_values[:, 2]
        expected_values[:, 2] = ordinary_values[:, 1]
    elif ordinary_track["path"] == "scale":
        expected_values[:, 1] = ordinary_values[:, 2]
        expected_values[:, 2] = ordinary_values[:, 1]
    else:
        x2, y2, z2, w2 = ordinary_values.T
        half = 0.70710678
        expected_values = np.column_stack(
            (
                half * x2 + half * w2,
                half * y2 - half * z2,
                half * z2 + half * y2,
                half * w2 - half * x2,
            )
        )
    assert blender_track["values"] == pytest.approx(expected_values.reshape(-1))
