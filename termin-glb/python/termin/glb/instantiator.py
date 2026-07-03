# termin/loaders/glb_instantiator.py
"""GLB instantiator - creates Entity hierarchy from GLBAsset."""

from __future__ import annotations

import hashlib
import re
import uuid
from pathlib import Path
from typing import TYPE_CHECKING, Dict, List, Optional, Protocol

import numpy as np

from termin.geombase import Pose3
from termin.geombase import GeneralPose3
from termin.animation import TcAnimationClip
from termin.animation_components import AnimationPlayer
from termin.skeleton import SkeletonInstance, TcSkeleton
from termin.skeleton_components import SkeletonController
from termin.scene import Entity
from termin.mesh import MeshComponent
from tmesh import TcMesh
from termin.render_components import MeshRenderer
from termin.render_components.skinned_mesh_renderer import SkinnedMeshRenderer

if TYPE_CHECKING:
    from termin.glb.asset import GLBAsset
    from termin.default_assets.mesh.asset import MeshAsset
    from termin.glb.loader import GLBSceneData, GLBMeshData, GLBMaterialData, GLBTcTexture

TEXTURE_ASSET_TYPE = "texture"


class SceneLike(Protocol):
    """Scene surface needed by GLB instantiation."""

    def create_entity(self, name: str) -> Entity:
        ...


def _add_mesh_component(entity: Entity, mesh: TcMesh) -> None:
    mesh_component = MeshComponent()
    mesh_component.mesh = mesh
    entity.add_component(mesh_component)


def _compute_vertex_normals(vertices: np.ndarray, indices: np.ndarray) -> np.ndarray:
    """Compute per-vertex normals from vertices and triangle indices."""
    normals = np.zeros_like(vertices, dtype=np.float32)
    num_tris = len(indices) // 3
    for t in range(num_tris):
        i0, i1, i2 = indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]
        v0, v1, v2 = vertices[i0], vertices[i1], vertices[i2]
        e1 = v1 - v0
        e2 = v2 - v0
        face_normal = np.cross(e1, e2)
        normals[i0] += face_normal
        normals[i1] += face_normal
        normals[i2] += face_normal
    # Normalize
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    lengths = np.where(lengths < 1e-8, 1.0, lengths)
    return normals / lengths


def _fallback_tangents(normals: np.ndarray) -> np.ndarray:
    refs = np.zeros_like(normals, dtype=np.float32)
    refs[:, 2] = 1.0
    z_aligned = np.abs(normals[:, 2]) > 0.999
    refs[z_aligned] = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    tangents = np.cross(refs, normals).astype(np.float32)
    lengths = np.linalg.norm(tangents, axis=1, keepdims=True)
    lengths = np.where(lengths < 1e-8, 1.0, lengths)
    return tangents / lengths


def _compute_vertex_tangents(
    vertices: np.ndarray,
    normals: np.ndarray,
    uvs: np.ndarray,
    indices: np.ndarray,
) -> np.ndarray:
    """Compute per-vertex tangents from indexed triangles and UVs."""
    vertex_count = len(vertices)
    tangents_1 = np.zeros((vertex_count, 3), dtype=np.float32)
    tangents_2 = np.zeros((vertex_count, 3), dtype=np.float32)

    triangle_indices = indices[: (len(indices) // 3) * 3].reshape(-1, 3)
    if len(triangle_indices) > 0:
        i0 = triangle_indices[:, 0]
        i1 = triangle_indices[:, 1]
        i2 = triangle_indices[:, 2]

        p0 = vertices[i0]
        p1 = vertices[i1]
        p2 = vertices[i2]
        uv0 = uvs[i0]
        uv1 = uvs[i1]
        uv2 = uvs[i2]

        edge1 = p1 - p0
        edge2 = p2 - p0
        duv1 = uv1 - uv0
        duv2 = uv2 - uv0

        denom = duv1[:, 0] * duv2[:, 1] - duv2[:, 0] * duv1[:, 1]
        valid = np.abs(denom) > 1e-8
        if np.any(valid):
            inv = np.zeros_like(denom, dtype=np.float32)
            inv[valid] = 1.0 / denom[valid]
            sdir = (duv2[:, 1:2] * edge1 - duv1[:, 1:2] * edge2) * inv[:, None]
            tdir = (duv1[:, 0:1] * edge2 - duv2[:, 0:1] * edge1) * inv[:, None]

            valid_i0 = i0[valid]
            valid_i1 = i1[valid]
            valid_i2 = i2[valid]
            valid_sdir = sdir[valid].astype(np.float32)
            valid_tdir = tdir[valid].astype(np.float32)
            np.add.at(tangents_1, valid_i0, valid_sdir)
            np.add.at(tangents_1, valid_i1, valid_sdir)
            np.add.at(tangents_1, valid_i2, valid_sdir)
            np.add.at(tangents_2, valid_i0, valid_tdir)
            np.add.at(tangents_2, valid_i1, valid_tdir)
            np.add.at(tangents_2, valid_i2, valid_tdir)

    tangent_xyz = tangents_1 - normals * np.sum(normals * tangents_1, axis=1, keepdims=True)
    lengths = np.linalg.norm(tangent_xyz, axis=1, keepdims=True)
    fallback = _fallback_tangents(normals)
    valid_tangent = lengths[:, 0] > 1e-8
    tangent_xyz[valid_tangent] /= lengths[valid_tangent]
    tangent_xyz[~valid_tangent] = fallback[~valid_tangent]

    handedness = np.ones((vertex_count, 1), dtype=np.float32)
    bitangent_sign = np.sum(np.cross(normals, tangent_xyz) * tangents_2, axis=1)
    handedness[bitangent_sign < 0.0, 0] = -1.0
    return np.concatenate([tangent_xyz.astype(np.float32), handedness], axis=1)


def _mesh_tangents_for_material_layout(
    glb_mesh: "GLBMeshData",
    vertices: np.ndarray,
    normals: np.ndarray,
    uvs: np.ndarray,
    indices: np.ndarray,
) -> np.ndarray | None:
    if glb_mesh.tangents is not None:
        return glb_mesh.tangents.astype(np.float32)
    if glb_mesh.uvs is None:
        return None
    return _compute_vertex_tangents(vertices, normals, uvs, indices)


def _glb_submeshes_to_tc(glb_mesh: "GLBMeshData"):
    from tmesh import TcSubmesh

    return [
        TcSubmesh(
            first_index=int(submesh.first_index),
            index_count=int(submesh.index_count),
            vertex_offset=0,
            material_slot=int(submesh.material_slot),
            name=submesh.name,
        )
        for submesh in glb_mesh.submeshes
    ]


def _glb_mesh_to_tc_mesh(glb_mesh: "GLBMeshData", uuid: str = "") -> "TcMesh":
    """Convert GLBMeshData to TcMesh directly (no intermediate Mesh3).

    Args:
        glb_mesh: Mesh data from GLB file
        uuid: Optional UUID to use for TcMesh (if empty, generates new)
    """
    from tmesh import TcMesh, TcVertexLayout

    vertices = glb_mesh.vertices.astype(np.float32)
    indices = glb_mesh.indices.astype(np.uint32).ravel()
    num_verts = len(vertices)

    # Compute normals if not provided
    if glb_mesh.normals is not None:
        normals = glb_mesh.normals.astype(np.float32)
    else:
        normals = _compute_vertex_normals(vertices, indices)

    # UVs (default to zeros)
    if glb_mesh.uvs is not None:
        uvs = glb_mesh.uvs.astype(np.float32)
    else:
        uvs = np.zeros((num_verts, 2), dtype=np.float32)

    # Tangents (optional in glTF, generated when normal+uv are available)
    tangents = _mesh_tangents_for_material_layout(glb_mesh, vertices, normals, uvs, indices)
    has_tangents = tangents is not None

    is_skinned = glb_mesh.is_skinned

    if is_skinned:
        # Skinned layout: pos(3) + normal(3) + uv(2) + tangent(4) +
        # joints(4) + weights(4) = 20 floats = 80 bytes. tangent is
        # included so PBR shaders that declare `in vec4 a_tangent (loc=3)`
        # can pair with skinned meshes without Vulkan vertex-input
        # mismatch (see tgfx_vertex_layout_skinned in tgfx_types.c).
        layout = TcVertexLayout.skinned()

        joint_indices = glb_mesh.joint_indices.astype(np.float32)  # stored as float for GPU
        joint_weights = glb_mesh.joint_weights.astype(np.float32)

        buffer = np.zeros((num_verts, 20), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs
        if has_tangents:
            buffer[:, 8:12] = tangents
        # else: zeros — fragment shader's Gram-Schmidt branch handles this.
        buffer[:, 12:16] = joint_indices
        buffer[:, 16:20] = joint_weights
    elif has_tangents:
        # Layout with tangents: pos(3) + normal(3) + uv(2) + tangent(4) = 12 floats = 48 bytes
        layout = TcVertexLayout.pos_normal_uv_tangent()

        # Build interleaved buffer
        buffer = np.zeros((num_verts, 12), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs
        buffer[:, 8:12] = tangents
    else:
        # Standard layout: pos(3) + normal(3) + uv(2) = 8 floats = 32 bytes
        layout = TcVertexLayout.pos_normal_uv()

        # Build interleaved buffer
        buffer = np.zeros((num_verts, 8), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs

    # Flatten buffer for TcMesh
    buffer_flat = buffer.ravel().astype(np.float32)

    return TcMesh.from_interleaved_with_submeshes(
        buffer_flat, num_verts, indices, layout, _glb_submeshes_to_tc(glb_mesh), glb_mesh.name, uuid
    )


def _populate_tc_mesh_from_glb(tc_mesh: TcMesh, glb_mesh: "GLBMeshData") -> bool:
    """Populate an existing declared TcMesh with data from GLBMeshData.

    Returns True if successful, False otherwise.
    """
    from tmesh import TcVertexLayout, tc_mesh_set_data, tc_mesh_set_submeshes

    vertices = glb_mesh.vertices.astype(np.float32)
    indices = glb_mesh.indices.astype(np.uint32).ravel()
    num_verts = len(vertices)

    # Compute normals if not provided
    if glb_mesh.normals is not None:
        normals = glb_mesh.normals.astype(np.float32)
    else:
        normals = _compute_vertex_normals(vertices, indices)

    # UVs (default to zeros)
    if glb_mesh.uvs is not None:
        uvs = glb_mesh.uvs.astype(np.float32)
    else:
        uvs = np.zeros((num_verts, 2), dtype=np.float32)

    # Tangents (optional in glTF, generated when normal+uv are available)
    tangents = _mesh_tangents_for_material_layout(glb_mesh, vertices, normals, uvs, indices)
    has_tangents = tangents is not None

    is_skinned = glb_mesh.is_skinned

    if is_skinned:
        # Skinned layout: pos(3) + normal(3) + uv(2) + tangent(4) +
        # joints(4) + weights(4) = 20 floats = 80 bytes. See _glb_mesh_to_tc_mesh.
        layout = TcVertexLayout.skinned()

        joint_indices = glb_mesh.joint_indices.astype(np.float32)
        joint_weights = glb_mesh.joint_weights.astype(np.float32)

        buffer = np.zeros((num_verts, 20), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs
        if has_tangents:
            buffer[:, 8:12] = tangents
        # else: zeros — fragment shader handles vec3(0) tangent.
        buffer[:, 12:16] = joint_indices
        buffer[:, 16:20] = joint_weights
    elif has_tangents:
        # Layout with tangents: pos(3) + normal(3) + uv(2) + tangent(4) = 12 floats
        layout = TcVertexLayout.pos_normal_uv_tangent()

        buffer = np.zeros((num_verts, 12), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs
        buffer[:, 8:12] = tangents
    else:
        # Standard layout: pos(3) + normal(3) + uv(2) = 8 floats
        layout = TcVertexLayout.pos_normal_uv()

        buffer = np.zeros((num_verts, 8), dtype=np.float32)
        buffer[:, 0:3] = vertices
        buffer[:, 3:6] = normals
        buffer[:, 6:8] = uvs

    buffer_flat = buffer.ravel().astype(np.float32)

    if not tc_mesh_set_data(tc_mesh, buffer_flat, num_verts, layout, indices, glb_mesh.name):
        return False
    return tc_mesh_set_submeshes(tc_mesh, _glb_submeshes_to_tc(glb_mesh))


def _glb_skin_to_tc_skeleton(skin, nodes, uuid: str = "") -> "TcSkeleton":
    """Convert GLB skin data to TcSkeleton.

    Args:
        skin: GLBSkinData from GLB file
        nodes: List of GLBNodeData
        uuid: Optional UUID to use (if empty, generates new)

    Returns:
        TcSkeleton with bone data populated
    """
    from termin.skeleton import TcSkeleton

    tc_skel = TcSkeleton.get_or_create(uuid) if uuid else TcSkeleton.create()
    if not tc_skel.is_valid:
        return tc_skel

    _populate_tc_skeleton_from_glb(tc_skel, skin, nodes)
    return tc_skel


def _populate_tc_skeleton_from_glb(tc_skel: "TcSkeleton", skin, nodes) -> bool:
    """Populate an existing declared TcSkeleton with data from GLB skin.

    Args:
        tc_skel: TcSkeleton to populate
        skin: GLBSkinData from GLB file
        nodes: List of GLBNodeData

    Returns:
        True if successful, False otherwise
    """
    if not tc_skel.is_valid:
        return False

    tc_skeleton_ptr = tc_skel.get()
    if tc_skeleton_ptr is None:
        return False

    joint_indices = skin.joint_node_indices
    inverse_bind_matrices = skin.inverse_bind_matrices
    num_joints = len(joint_indices)

    if num_joints == 0:
        return True

    # Allocate bones
    bones = tc_skel.alloc_bones(num_joints)
    if bones is None:
        return False

    # Populate each bone
    for bone_idx in range(num_joints):
        node_idx = joint_indices[bone_idx]
        node = nodes[node_idx]

        # Find parent bone index by checking which other bone's node has this node as a child
        parent_bone_idx = -1
        for other_bone_idx in range(num_joints):
            other_node_idx = joint_indices[other_bone_idx]
            other_node = nodes[other_node_idx]
            if node_idx in other_node.children:
                parent_bone_idx = other_bone_idx
                break

        # Get tc_bone pointer and populate
        bone = tc_skel.get_bone(bone_idx)
        if bone is None:
            continue

        # Set bone data via C API
        bone.name = node.name[:63]  # TC_BONE_NAME_MAX = 64
        bone.index = bone_idx
        bone.parent_index = parent_bone_idx

        # Inverse bind matrix (row-major 4x4) - must assign entire list at once
        ibm = inverse_bind_matrices[bone_idx]
        bone.inverse_bind_matrix = [float(x) for x in ibm.flat]

        # Bind pose
        bone.bind_translation = (float(node.translation[0]), float(node.translation[1]), float(node.translation[2]))
        bone.bind_rotation = (float(node.rotation[0]), float(node.rotation[1]), float(node.rotation[2]), float(node.rotation[3]))
        bone.bind_scale = (float(node.scale[0]), float(node.scale[1]), float(node.scale[2]))

    # Rebuild root indices
    tc_skel.rebuild_roots()

    # Mark as loaded
    tc_skeleton_ptr.is_loaded = True
    tc_skel.bump_version()

    return True


class _PendingSkinnedMesh:
    """Placeholder for skinned mesh that needs skeleton instance set later."""
    def __init__(self, entity: Entity, mesh: TcMesh, glb_mesh: "GLBMeshData"):
        self.entity = entity
        self.mesh = mesh
        self.glb_mesh = glb_mesh


def _stable_glb_texture_uuid(texture: "GLBTcTexture") -> str:
    digest = hashlib.sha256(texture.data).hexdigest()
    key = f"termin:gltf-texture:{texture.mime_type}:{texture.name}:{digest}"
    return str(uuid.uuid5(uuid.NAMESPACE_URL, key))


def _safe_glb_texture_name(texture: "GLBTcTexture") -> str:
    name = texture.name.strip()
    if not name:
        name = f"gltf_texture_{texture.index}"
    name = re.sub(r"[^A-Za-z0-9_.:/-]+", "_", name)
    name = name.strip("._/ ")
    if not name:
        name = f"gltf_texture_{texture.index}"
    return name


def _unique_glb_texture_name(rm, texture: "GLBTcTexture", texture_uuid: str) -> str:
    base_name = _safe_glb_texture_name(texture)
    asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, base_name)
    if asset is None or asset.uuid == texture_uuid:
        return base_name

    suffix = 2
    while True:
        candidate = f"{base_name}_{suffix}"
        asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, candidate)
        if asset is None or asset.uuid == texture_uuid:
            return candidate
        suffix += 1


def _tc_texture_from_asset_name(rm, name: str):
    from tcbase import log

    asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, name)
    if asset is None:
        log.error(f"[glb_instantiator] Texture asset not found by name: {name}")
        return None

    tc_texture = asset.texture_data
    if tc_texture is None or not tc_texture.is_valid:
        log.error(f"[glb_instantiator] Texture asset failed to load: {name}")
        return None
    log.info(
        f"[glb_instantiator] resolved texture asset name='{name}' "
        f"asset_uuid={asset.uuid} source_path={asset.source_path} "
        f"tc_uuid={tc_texture.uuid} tc_name='{tc_texture.name}' "
        f"size={tc_texture.width}x{tc_texture.height}"
    )
    return tc_texture


def _texture_name_for_source_path(rm, source_path: Path) -> str | None:
    from tcbase import log

    target_path = source_path.resolve()

    texture_names = rm.list_runtime_asset_names(TEXTURE_ASSET_TYPE)
    for name in texture_names:
        asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, name)
        if asset is None:
            continue
        asset_source_path = asset.source_path
        if asset_source_path is not None and asset_source_path.resolve() == target_path:
            log.info(
                f"[glb_instantiator] matched glTF texture source by exact path: "
                f"source_path={target_path} asset_name='{name}' asset_uuid={asset.uuid}"
            )
            return name

    stem_name = source_path.stem
    stem_asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, stem_name)
    if stem_asset is not None and _texture_asset_matches_source_content(stem_asset, source_path):
        log.info(
            f"[glb_instantiator] matched glTF texture source by stem/content: "
            f"source_path={target_path} asset_name='{stem_name}' asset_uuid={stem_asset.uuid} "
            f"asset_source_path={stem_asset.source_path}"
        )
        return stem_name
    log.warning(
        f"[glb_instantiator] no registered texture asset matched glTF source: "
        f"source_path={target_path} stem='{stem_name}' registered_textures={len(texture_names)}"
    )
    return None


def _texture_asset_matches_source_content(asset, source_path: Path) -> bool:
    from tcbase import log

    asset_source_path = asset.source_path
    if asset_source_path is None:
        return True
    left = asset_source_path.resolve()
    right = source_path.resolve()
    if left == right:
        return True
    if not left.exists() or not right.exists():
        return False
    try:
        return hashlib.sha256(left.read_bytes()).digest() == hashlib.sha256(right.read_bytes()).digest()
    except Exception:
        log.warning(
            f"[glb_instantiator] Failed to compare duplicate texture sources: {left} vs {right}",
            exc_info=True,
        )
        return False


def _register_external_texture_asset(rm, source_path: Path, texture: "GLBTcTexture") -> str | None:
    from tcbase import log
    from termin.default_assets.render.texture_asset import TextureAsset
    from termin_assets import read_spec_file

    if not source_path.exists():
        log.error(f"[glb_instantiator] glTF external texture file does not exist: {source_path}")
        return None

    spec_data = read_spec_file(str(source_path))
    texture_uuid = spec_data.get("uuid") if spec_data else None
    name = source_path.stem
    existing = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, name)
    if existing is not None and existing.source_path is not None:
        if existing.source_path.resolve() != source_path.resolve():
            name = _unique_glb_texture_name(rm, texture, texture_uuid or _stable_glb_texture_uuid(texture))
            log.warning(
                f"[glb_instantiator] texture name collision for external glTF texture: "
                f"source_path={source_path.resolve()} base_name='{source_path.stem}' "
                f"existing_source_path={existing.source_path} chosen_name='{name}'"
            )

    asset = TextureAsset(
        texture_data=None,
        name=name,
        source_path=source_path,
        uuid=texture_uuid,
    )
    asset.parse_spec(spec_data)
    rm.register_runtime_asset(TEXTURE_ASSET_TYPE, name, asset, source_path=str(source_path), uuid=asset.uuid)
    log.info(
        f"[glb_instantiator] registered external glTF texture asset: "
        f"name='{name}' uuid={asset.uuid} source_path={source_path.resolve()} "
        f"had_meta={spec_data is not None}"
    )
    return name


def _registered_glb_texture(rm, texture: "GLBTcTexture"):
    from tcbase import log

    source_path = texture.source_path
    if source_path is None:
        log.info(
            f"[glb_instantiator] glTF texture index={texture.index} name='{texture.name}' "
            f"has no external source_path; embedded/runtime path will be used"
        )
        return None

    log.info(
        f"[glb_instantiator] resolving glTF external texture index={texture.index} "
        f"name='{texture.name}' source_path={source_path.resolve()} mime={texture.mime_type}"
    )
    name = _texture_name_for_source_path(rm, source_path)
    if name is None:
        name = _register_external_texture_asset(rm, source_path, texture)
    if name is None:
        log.error(
            f"[glb_instantiator] failed to resolve/register glTF external texture "
            f"index={texture.index} name='{texture.name}' source_path={source_path.resolve()}"
        )
        return None
    return _tc_texture_from_asset_name(rm, name)


def _decode_glb_texture(rm, texture: "GLBTcTexture"):
    """Decode glTF image bytes into a registered TextureAsset."""
    import io

    from PIL import Image
    from termin.default_assets.render.texture_asset import TextureAsset
    from tgfx import TcTexture
    from tcbase import log

    texture_uuid = _stable_glb_texture_uuid(texture)
    existing_asset = rm.get_runtime_asset_by_uuid(TEXTURE_ASSET_TYPE, texture_uuid)
    if existing_asset is not None:
        tc_texture = existing_asset.texture_data
        if tc_texture is not None and tc_texture.is_valid:
            log.info(
                f"[glb_instantiator] reused decoded glTF texture asset: "
                f"index={texture.index} name='{texture.name}' asset_uuid={existing_asset.uuid} "
                f"tc_uuid={tc_texture.uuid}"
            )
            return tc_texture
        log.warning(f"[glb_instantiator] Registered glTF texture asset is invalid: {texture.name} ({texture_uuid})")

    try:
        texture_name = _unique_glb_texture_name(rm, texture, texture_uuid)
        image = Image.open(io.BytesIO(texture.data)).convert("RGBA")
        data = np.array(image, dtype=np.uint8)
        height, width = data.shape[:2]
        asset = TextureAsset(
            texture_data=None,
            name=texture_name,
            uuid=texture_uuid,
        )
        asset.texture_data = TcTexture.from_data(
            data,
            width=width,
            height=height,
            channels=4,
            flip_x=False,
            flip_y=True,
            transpose=False,
            name=texture_name,
            uuid=texture_uuid,
        )
        rm.register_runtime_asset(TEXTURE_ASSET_TYPE, texture_name, asset, uuid=texture_uuid)
        log.info(
            f"[glb_instantiator] decoded glTF texture bytes into runtime asset: "
            f"index={texture.index} name='{texture.name}' registered_name='{texture_name}' "
            f"uuid={asset.uuid} size={asset.texture_data.width}x{asset.texture_data.height}"
        )
        return asset.texture_data
    except Exception:
        log.error(f"[glb_instantiator] Failed to decode glTF texture '{texture.name}'", exc_info=True)
        return None


def _build_texture_lookup(rm, scene_data: "GLBSceneData") -> dict[int, object]:
    """Create a glTF texture-index to TcTexture map."""
    from tcbase import log

    texture_names = rm.list_runtime_asset_names(TEXTURE_ASSET_TYPE)
    log.info(
        f"[glb_instantiator] building glTF texture lookup: "
        f"textures={len(scene_data.textures)} registered_texture_assets={len(texture_names)}"
    )
    textures: dict[int, object] = {}
    for texture in scene_data.textures:
        tc_texture = _registered_glb_texture(rm, texture)
        if tc_texture is None:
            tc_texture = _decode_glb_texture(rm, texture)
        if tc_texture is not None and tc_texture.is_valid:
            textures[texture.index] = tc_texture
            resolved_name = _find_texture_name_for_tc_texture(rm, tc_texture)
            log.info(
                f"[glb_instantiator] glTF texture lookup entry: index={texture.index} "
                f"glTF_name='{texture.name}' resolved_asset_name='{resolved_name}' "
                f"tc_uuid={tc_texture.uuid} tc_name='{tc_texture.name}'"
            )
        else:
            log.error(
                f"[glb_instantiator] glTF texture lookup failed: "
                f"index={texture.index} name='{texture.name}' source_path={texture.source_path}"
            )
    return textures


def _find_texture_name_for_tc_texture(rm, tc_texture) -> str | None:
    for name in rm.list_runtime_asset_names(TEXTURE_ASSET_TYPE):
        asset = rm.get_runtime_asset(TEXTURE_ASSET_TYPE, name)
        if asset is None:
            continue
        cached_texture = asset.cached_data
        if cached_texture is not None and cached_texture.is_valid and cached_texture.uuid == tc_texture.uuid:
            return name
    return None


def _texture_for_index(textures: dict[int, object], texture_index: int | None):
    if texture_index is None:
        return None
    texture = textures.get(texture_index)
    if texture is None:
        from tcbase import log
        log.warning(f"[glb_instantiator] glTF material references missing texture index {texture_index}")
    return texture


def _set_material_texture_if_present(material, name: str, texture) -> None:
    from tcbase import log

    if texture is None:
        log.info(f"[glb_instantiator] material override texture skipped: slot='{name}' texture=None")
        return
    applied = material.set_texture(name, texture)
    if applied == 0:
        log.warning(
            f"[glb_instantiator] material override texture slot missing: "
            f"material='{material.name}' slot='{name}' available={list(material.textures.keys())}"
        )
        return
    log.info(
        f"[glb_instantiator] material override texture set: "
        f"material='{material.name}' slot='{name}' "
        f"tc_uuid={texture.uuid} tc_name='{texture.name}' phases={applied}"
    )


def _configure_import_material(
    material,
    glb_material: "GLBMaterialData",
    texture_lookup: dict[int, object],
) -> None:
    """Apply one glTF material to an already-created Termin material."""
    from termin.geombase import Vec4
    from tcbase import log

    base_color = glb_material.base_color
    if base_color is None:
        base_color = np.array([1.0, 1.0, 1.0, 1.0], dtype=np.float32)

    emissive_factor = glb_material.emissive_factor
    if emissive_factor is None:
        emissive_factor = np.array([0.0, 0.0, 0.0], dtype=np.float32)
    emission_intensity = 1.0 if float(np.linalg.norm(emissive_factor)) > 0.0 else 0.0

    color = Vec4(
        float(base_color[0]),
        float(base_color[1]),
        float(base_color[2]),
        float(base_color[3]),
    )
    emission_color = Vec4(
        float(emissive_factor[0]),
        float(emissive_factor[1]),
        float(emissive_factor[2]),
        1.0,
    )

    base_color_texture = _texture_for_index(texture_lookup, glb_material.base_color_texture)
    metallic_roughness_texture = _texture_for_index(
        texture_lookup,
        glb_material.metallic_roughness_texture,
    )
    normal_texture = _texture_for_index(texture_lookup, glb_material.normal_texture)
    occlusion_texture = _texture_for_index(texture_lookup, glb_material.occlusion_texture)
    emissive_texture = _texture_for_index(texture_lookup, glb_material.emissive_texture)
    log.info(
        f"[glb_instantiator] applying glTF material override: material='{glb_material.name}' "
        f"base_color_index={glb_material.base_color_texture} metallic_roughness_index={glb_material.metallic_roughness_texture} "
        f"normal_index={glb_material.normal_texture} occlusion_index={glb_material.occlusion_texture} "
        f"emissive_index={glb_material.emissive_texture}"
    )

    material.set_uniform_vec4("u_color", color)
    material.set_uniform_float("u_metallic", float(glb_material.metallic_factor))
    material.set_uniform_float("u_roughness", float(glb_material.roughness_factor))
    material.set_uniform_float("u_normal_strength", float(glb_material.normal_scale))
    material.set_uniform_vec4("u_emission_color", emission_color)
    material.set_uniform_float("u_emission_intensity", emission_intensity)

    _set_material_texture_if_present(material, "u_albedo_texture", base_color_texture)
    _set_material_texture_if_present(material, "u_metallic_roughness_texture", metallic_roughness_texture)
    _set_material_texture_if_present(material, "u_normal_texture", normal_texture)
    _set_material_texture_if_present(material, "u_occlusion_texture", occlusion_texture)
    _set_material_texture_if_present(material, "u_emissive_texture", emissive_texture)


def _create_import_material_slot(base_material, glb_material: "GLBMaterialData", texture_lookup: dict[int, object]):
    """Create one material instance for a glTF material slot."""
    from tcbase import log

    material = base_material.copy("")
    if not material.is_valid:
        log.error(f"[glb_instantiator] Failed to create material slot for '{glb_material.name}'")
        return material
    material.name = f"{base_material.name}_gltf_{glb_material.name}"
    log.info(
        f"[glb_instantiator] material slot created: "
        f"source_material='{glb_material.name}' slot_name='{material.name}' phases={len(material.phases)}"
    )
    _configure_import_material(material, glb_material, texture_lookup)
    return material


def _apply_import_material_override(
    renderer: MeshRenderer,
    glb_material: "GLBMaterialData",
    texture_lookup: dict[int, object],
) -> None:
    """Apply one glTF material as a MeshRenderer material override."""
    from tcbase import log

    renderer.set_override_material(True)
    material = renderer.get_overridden_material()
    if not material.is_valid:
        log.error(f"[glb_instantiator] Failed to create material override for '{glb_material.name}'")
        return
    log.info(
        f"[glb_instantiator] material override created: "
        f"source_material='{glb_material.name}' override_name='{material.name}' phases={len(material.phases)}"
    )
    _configure_import_material(material, glb_material, texture_lookup)


def _apply_glb_material_slots(
    renderer: MeshRenderer,
    glb_mesh: "GLBMeshData",
    scene_data: "GLBSceneData",
    texture_lookup: dict[int, object],
) -> None:
    """Populate MeshRenderer material slots from glTF primitive materials."""
    base_material = renderer.get_base_material()
    if not base_material.is_valid:
        return

    material_for_slot: dict[int, int] = {}
    for submesh in glb_mesh.submeshes:
        if submesh.material_index < 0 or submesh.material_index >= len(scene_data.materials):
            continue
        material_for_slot[submesh.material_slot] = submesh.material_index

    if not material_for_slot:
        return

    for slot, material_index in sorted(material_for_slot.items()):
        material = _create_import_material_slot(
            base_material,
            scene_data.materials[material_index],
            texture_lookup,
        )
        if material.is_valid:
            renderer.set_material_slot(slot, material)


def _apply_glb_material_override_if_present(
    renderer: MeshRenderer,
    glb_mesh: "GLBMeshData",
    scene_data: "GLBSceneData",
    texture_lookup: dict[int, object],
) -> None:
    if len(glb_mesh.submeshes) > 1:
        _apply_glb_material_slots(renderer, glb_mesh, scene_data, texture_lookup)
        return
    if glb_mesh.material_index < 0 or glb_mesh.material_index >= len(scene_data.materials):
        return
    _apply_import_material_override(
        renderer,
        scene_data.materials[glb_mesh.material_index],
        texture_lookup,
    )


def _create_entity_from_node(
    node_index: int,
    scene_data: "GLBSceneData",
    meshes: Dict[int, TcMesh],
    mesh_assets: Dict[str, "MeshAsset"],
    base_material,
    texture_lookup: dict[int, object],
    node_to_entity: Optional[Dict[int, Entity]] = None,
    pending_skinned: Optional[List[_PendingSkinnedMesh]] = None,
    scene: Optional[SceneLike] = None,
) -> Entity:
    """Recursively create Entity hierarchy from GLBNodeData."""
    node = scene_data.nodes[node_index]
    pose = GeneralPose3(lin=node.translation, ang=node.rotation, scale=node.scale)

    # Create entity in scene's pool if scene provided, else standalone
    if scene is not None:
        entity = scene.create_entity(node.name)
        entity.transform.relocate(pose)
    else:
        entity = Entity(pose=pose, name=node.name)

    if node_to_entity is not None:
        node_to_entity[node_index] = entity

    # Add renderer for each primitive in the referenced mesh
    if node.mesh_index is not None and node.mesh_index in scene_data.mesh_index_map:
        our_mesh_indices = scene_data.mesh_index_map[node.mesh_index]

        for mesh_idx in our_mesh_indices:
            glb_mesh = scene_data.meshes[mesh_idx]

            if mesh_idx not in meshes:
                # Get tc_mesh from MeshAsset by mesh name
                mesh_asset = mesh_assets.get(glb_mesh.name)
                if mesh_asset is None or mesh_asset.data is None:
                    raise RuntimeError(f"[glb_instantiator] MeshAsset for '{glb_mesh.name}' not found or not loaded")

                tc_mesh = mesh_asset.data
                if not tc_mesh.is_valid:
                    raise RuntimeError(f"[glb_instantiator] TcMesh for '{glb_mesh.name}' is invalid")

                meshes[mesh_idx] = tc_mesh

            tc_mesh = meshes[mesh_idx]

            if glb_mesh.is_skinned and pending_skinned is not None:
                from tcbase import log
                log.info(f"[glb_instantiator] pending skinned mesh={glb_mesh.name} tc_mesh.is_valid={tc_mesh.is_valid} uuid={tc_mesh.uuid}")
                pending_skinned.append(_PendingSkinnedMesh(entity, tc_mesh, glb_mesh))
            else:
                _add_mesh_component(entity, tc_mesh)
                renderer = MeshRenderer(material=base_material)
                _apply_glb_material_override_if_present(renderer, glb_mesh, scene_data, texture_lookup)
                entity.add_component(renderer)

    # Recursively create children
    for child_index in node.children:
        child_entity = _create_entity_from_node(
            child_index, scene_data, meshes, mesh_assets, base_material, texture_lookup,
            node_to_entity, pending_skinned, scene
        )
        entity.transform.add_child(child_entity.transform)

    return entity


class GLBInstantiateResult:
    """Result of GLB instantiation containing entity and resources."""

    def __init__(
        self,
        entity: Entity,
        skeleton_controller: Optional[SkeletonController] = None,
        animation_player: Optional[AnimationPlayer] = None,
        # Debug: keep references to prevent premature destruction
        _bone_entities: Optional[List] = None,
        _clips: Optional[List] = None,
    ):
        self.entity = entity
        self.skeleton_controller = skeleton_controller
        self.animation_player = animation_player
        self._bone_entities = _bone_entities
        self._clips = _clips

    @property
    def skeleton_instance(self) -> Optional[SkeletonInstance]:
        """Get skeleton instance from controller (for backwards compatibility)."""
        if self.skeleton_controller is not None:
            return self.skeleton_controller.skeleton_instance
        return None


def instantiate_glb(
    glb_asset: "GLBAsset",
    name: str | None = None,
    scene: Optional[SceneLike] = None,
) -> GLBInstantiateResult:
    """
    Create Entity hierarchy from GLBAsset.

    Args:
        glb_asset: GLBAsset to instantiate (will be loaded if not already).
        name: Optional name for the root entity. Defaults to asset name.
        scene: Optional Scene - if provided, entities are created directly in
               scene's pool (recommended). Otherwise entities are created in
               standalone pool and migrated when added to scene.

    Returns:
        GLBInstantiateResult containing root Entity, SkeletonController, and AnimationPlayer.
    """
    from tcbase import log
    from termin_assets import get_resource_manager

    # Access scene_data triggers lazy loading and populates child assets
    scene_data = glb_asset.scene_data
    if scene_data is None:
        raise RuntimeError(f"[glb_instantiator] Failed to load GLBAsset '{glb_asset.name}'")

    # Get mesh assets from GLBAsset
    mesh_assets = glb_asset.get_mesh_assets()

    if name is None:
        name = glb_asset.name

    rm = get_resource_manager()
    if rm is None:
        log.error(
            f"[glb_instantiator] Resource manager is not configured; "
            f"cannot instantiate GLBAsset '{glb_asset.name}'"
        )
        raise RuntimeError("Resource manager is not configured for GLB instantiation")

    # Base material for all imported glTF primitives. Per-material glTF
    # parameters are applied through MeshRenderer material overrides.
    base_material = rm.get_material("NormalizedPBR")

    if base_material is None or not base_material.is_valid:
        raise RuntimeError(
            f"[glb_instantiator] Builtin materials not registered: "
            f"NormalizedPBR(default)={base_material is not None}"
        )

    texture_lookup = _build_texture_lookup(rm, scene_data)

    meshes: Dict[int, TcMesh] = {}
    node_to_entity: Dict[int, Entity] = {}
    pending_skinned: List[_PendingSkinnedMesh] = []

    # Helper to create entity in scene's pool or standalone
    def create_entity(entity_name: str) -> Entity:
        if scene is not None:
            ent = scene.create_entity(entity_name)
            ent.transform.relocate(Pose3.identity())
            return ent
        return Entity(pose=Pose3.identity(), name=entity_name)

    # Step 1: Create Entity hierarchy
    root_entity = None
    if scene_data.root_nodes:
        if len(scene_data.root_nodes) == 1:
            root_entity = _create_entity_from_node(
                scene_data.root_nodes[0],
                scene_data,
                meshes,
                mesh_assets,
                base_material,
                texture_lookup,
                node_to_entity=node_to_entity,
                pending_skinned=pending_skinned,
                scene=scene,
            )
            root_entity.name = name
        else:
            root_entity = create_entity(name)
            for root_index in scene_data.root_nodes:
                child_entity = _create_entity_from_node(
                    root_index,
                    scene_data,
                    meshes,
                    mesh_assets,
                    base_material,
                    texture_lookup,
                    node_to_entity=node_to_entity,
                    pending_skinned=pending_skinned,
                    scene=scene,
                )
                root_entity.transform.add_child(child_entity.transform)
    else:
        # Fallback: no node hierarchy, create entities for each mesh directly
        root_entity = create_entity(name)
        for i, glb_mesh in enumerate(scene_data.meshes):
            mesh_asset = mesh_assets.get(glb_mesh.name)
            if mesh_asset is None or mesh_asset.data is None:
                raise RuntimeError(f"[glb_instantiator] MeshAsset for '{glb_mesh.name}' not found or not loaded")

            tc_mesh = mesh_asset.data
            if not tc_mesh.is_valid:
                raise RuntimeError(f"[glb_instantiator] TcMesh for '{glb_mesh.name}' is invalid")

            meshes[i] = tc_mesh
            mesh_entity = create_entity(glb_mesh.name)
            _add_mesh_component(mesh_entity, tc_mesh)
            renderer = MeshRenderer(material=base_material)
            _apply_glb_material_override_if_present(renderer, glb_mesh, scene_data, texture_lookup)
            mesh_entity.add_component(renderer)
            root_entity.transform.add_child(mesh_entity.transform)

    # Step 2: Create skeleton controller
    skeleton_controller: Optional[SkeletonController] = None
    bone_entities: List[Entity] = []  # keep reference for debug
    if scene_data.skins:
        skin = scene_data.skins[0]

        # Get skeleton from GLBAsset's child assets
        skeleton_assets = glb_asset.get_skeleton_assets()
        skeleton_key = "skeleton"
        skeleton_asset = skeleton_assets.get(skeleton_key)

        if skeleton_asset is None or skeleton_asset.skeleton_data is None:
            raise RuntimeError(f"[glb_instantiator] Skeleton not found in GLBAsset '{glb_asset.name}'")

        # Collect bone entities
        for node_idx in skin.joint_node_indices:
            entity = node_to_entity.get(node_idx)
            if entity is None:
                entity = create_entity(f"missing_bone_{node_idx}")
            bone_entities.append(entity)

        # Pass the TcSkeleton from the asset, not the asset itself
        tc_skeleton = skeleton_asset.data
        if tc_skeleton is None or not tc_skeleton.is_valid:
            raise RuntimeError(f"[glb_instantiator] TcSkeleton is invalid for skeleton '{skeleton_key}'")

        skeleton_controller = SkeletonController(
            skeleton=tc_skeleton,
            bone_entities=bone_entities,
        )
        root_entity.add_component(skeleton_controller)

    # Step 3: Setup SkinnedMeshRenderers
    from tcbase import log
    for pending in pending_skinned:
        if skeleton_controller is not None:
            log.info(f"[glb_instantiator] creating SkinnedMeshRenderer mesh.is_valid={pending.mesh.is_valid} uuid={pending.mesh.uuid} name={pending.mesh.name}")
            _add_mesh_component(pending.entity, pending.mesh)
            renderer = SkinnedMeshRenderer(
                material=base_material,
                skeleton_controller=skeleton_controller,
            )
            _apply_glb_material_override_if_present(renderer, pending.glb_mesh, scene_data, texture_lookup)
            pending.entity.add_component(renderer)

    # Step 4: Setup animations from GLBAsset's child assets
    animation_player: Optional[AnimationPlayer] = None
    clips: List[TcAnimationClip] = []  # keep reference for debug
    animation_assets = glb_asset.get_animation_assets()

    if animation_assets:
        animation_player = AnimationPlayer()

        for _anim_name, anim_asset in animation_assets.items():
            clip = anim_asset.clip
            if clip is not None and clip.is_valid:
                clips.append(clip)
                animation_player.add_clip(clip)

        if clips:
            root_entity.add_component(animation_player)

    return GLBInstantiateResult(
        entity=root_entity,
        skeleton_controller=skeleton_controller,
        animation_player=animation_player,
        _bone_entities=bone_entities,
        _clips=clips,
    )
