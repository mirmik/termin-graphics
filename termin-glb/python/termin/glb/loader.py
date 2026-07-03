# termin/loaders/glb_loader.py
"""GLB/glTF 2.0 loader.

Pure Python implementation without external dependencies (except numpy).
"""

from __future__ import annotations

import json
import struct
import base64
import mimetypes
from pathlib import Path
from typing import List, Optional, Dict, Any

import numpy as np

from tcbase import log


# ---------- DATA CLASSES ----------

class GLBSubmeshData:
    """One glTF primitive converted to a Termin submesh section."""
    def __init__(
        self,
        name: str,
        first_index: int,
        index_count: int,
        material_index: int,
        material_slot: int,
    ):
        self.name = name
        self.first_index = first_index
        self.index_count = index_count
        self.material_index = material_index
        self.material_slot = material_slot


class GLBMeshData:
    """Mesh data extracted from GLB."""
    def __init__(self, name: str, vertices: np.ndarray, normals: Optional[np.ndarray],
                 uvs: Optional[np.ndarray], indices: np.ndarray, material_index: int = -1,
                 tangents: Optional[np.ndarray] = None,
                 joint_indices: Optional[np.ndarray] = None,
                 joint_weights: Optional[np.ndarray] = None,
                 submeshes: Optional[List[GLBSubmeshData]] = None):
        self.name = name
        self.vertices = vertices
        self.normals = normals
        self.uvs = uvs
        self.tangents = tangents  # Per-vertex tangents (N, 4) with w = handedness
        self.indices = indices
        self.material_index = material_index
        # Skinning data (N, 4) arrays
        self.joint_indices = joint_indices  # Bone indices per vertex
        self.joint_weights = joint_weights  # Blend weights per vertex
        self.submeshes = submeshes or [
            GLBSubmeshData(name, 0, int(len(indices)), material_index, 0)
        ]

    @property
    def is_skinned(self) -> bool:
        """True if this mesh has skinning data."""
        return self.joint_indices is not None and self.joint_weights is not None


class GLBMaterialData:
    """Material data extracted from GLB."""
    def __init__(self, name: str, base_color: Optional[np.ndarray] = None,
                 base_color_texture: Optional[int] = None,
                 metallic_factor: float = 1.0,
                 roughness_factor: float = 1.0,
                 metallic_roughness_texture: Optional[int] = None,
                 normal_texture: Optional[int] = None,
                 normal_scale: float = 1.0,
                 occlusion_texture: Optional[int] = None,
                 emissive_texture: Optional[int] = None,
                 emissive_factor: Optional[np.ndarray] = None):
        self.name = name
        self.base_color = base_color  # RGBA
        self.base_color_texture = base_color_texture  # texture index
        self.metallic_factor = metallic_factor
        self.roughness_factor = roughness_factor
        self.metallic_roughness_texture = metallic_roughness_texture
        self.normal_texture = normal_texture
        self.normal_scale = normal_scale
        self.occlusion_texture = occlusion_texture
        self.emissive_texture = emissive_texture
        self.emissive_factor = emissive_factor  # RGB


class GLBTcTexture:
    """Texture data extracted from GLB."""
    def __init__(
        self,
        index: int,
        name: str,
        data: bytes,
        mime_type: str,
        source_path: Path | None = None,
    ):
        self.index = index
        self.name = name
        self.data = data  # Raw image bytes (PNG/JPEG)
        self.mime_type = mime_type
        self.source_path = source_path


class GLBAnimationChannel:
    """Animation channel for a single node."""
    def __init__(self, node_index: int, node_name: str,
                 pos_keys: List, rot_keys: List, scale_keys: List):
        self.node_index = node_index
        self.node_name = node_name
        self.pos_keys = pos_keys      # [(time, [x,y,z]), ...]
        self.rot_keys = rot_keys      # [(time, [x,y,z,w]), ...] quaternion
        self.scale_keys = scale_keys  # [(time, [x,y,z]), ...]


class GLBAnimationClip:
    """Animation clip containing multiple channels."""
    def __init__(self, name: str, channels: List[GLBAnimationChannel], duration: float):
        self.name = name
        self.channels = channels
        self.duration = duration


class GLBNodeData:
    """Node in scene hierarchy."""
    def __init__(self, name: str, children: List[int], mesh_index: Optional[int],
                 translation: np.ndarray, rotation: np.ndarray, scale: np.ndarray,
                 skin_index: Optional[int] = None):
        self.name = name
        self.children = children
        self.mesh_index = mesh_index
        self.skin_index = skin_index  # Index into skins array
        self.translation = translation  # [x, y, z]
        self.rotation = rotation        # [x, y, z, w] quaternion
        self.scale = scale              # [x, y, z]


class GLBSkinData:
    """Skin (skeleton) data extracted from GLB."""
    def __init__(
        self,
        name: str,
        joint_node_indices: List[int],
        inverse_bind_matrices: np.ndarray,
        armature_node_index: Optional[int] = None,
    ):
        """
        Initialize skin data.

        Args:
            name: Skin name
            joint_node_indices: List of node indices that are joints (bones)
            inverse_bind_matrices: (N, 4, 4) inverse bind matrices for each joint
            armature_node_index: Node index of skeleton root (Armature in Blender)
        """
        self.name = name
        self.joint_node_indices = joint_node_indices
        self.inverse_bind_matrices = inverse_bind_matrices
        self.armature_node_index = armature_node_index

    @property
    def joint_count(self) -> int:
        """Number of joints in this skin."""
        return len(self.joint_node_indices)


class GLBSceneData:
    """Complete scene data from GLB file."""
    def __init__(self):
        self.meshes: List[GLBMeshData] = []
        self.materials: List[GLBMaterialData] = []
        self.textures: List[GLBTcTexture] = []
        self.animations: List[GLBAnimationClip] = []
        self.nodes: List[GLBNodeData] = []
        self.skins: List[GLBSkinData] = []
        self.root_nodes: List[int] = []
        # Map from glTF mesh index to list of our internal mesh indices
        # (one glTF mesh can have multiple primitives)
        self.mesh_index_map: Dict[int, List[int]] = {}
        # Scale factor from inverse bind matrices (for skinned meshes)
        self.skin_scale: float = 1.0


# ---------- ACCESSOR HELPERS ----------

COMPONENT_TYPE_SIZE = {
    5120: 1,  # BYTE
    5121: 1,  # UNSIGNED_BYTE
    5122: 2,  # SHORT
    5123: 2,  # UNSIGNED_SHORT
    5125: 4,  # UNSIGNED_INT
    5126: 4,  # FLOAT
}

COMPONENT_TYPE_DTYPE = {
    5120: np.int8,
    5121: np.uint8,
    5122: np.int16,
    5123: np.uint16,
    5125: np.uint32,
    5126: np.float32,
}

TYPE_NUM_COMPONENTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


def _read_data_uri(uri: str) -> tuple[bytes, str | None]:
    """Read a data URI payload."""
    if not uri.startswith("data:"):
        raise ValueError("URI is not a data URI")

    header, encoded = uri.split(",", 1)
    mime_type = header[5:].split(";", 1)[0] or None
    if ";base64" in header:
        return base64.b64decode(encoded), mime_type
    return encoded.encode("utf-8"), mime_type


def _read_uri_bytes(uri: str, base_path: Path) -> tuple[bytes, str | None]:
    """Read bytes from a glTF URI."""
    if uri.startswith("data:"):
        return _read_data_uri(uri)

    path = base_path / uri
    try:
        return path.read_bytes(), mimetypes.guess_type(path.name)[0]
    except Exception:
        log.error(f"[glb_loader] Failed to read glTF external URI: {path}", exc_info=True)
        raise


def _external_uri_path(uri: str, base_path: Path) -> Path | None:
    """Resolve an external glTF URI to a filesystem path."""
    if uri.startswith("data:"):
        return None
    return base_path / uri


def _load_gltf_buffers(gltf: dict, base_path: Path) -> list[bytes]:
    """Load all buffers referenced by a JSON .gltf file."""
    buffers: list[bytes] = []
    for index, buffer in enumerate(gltf.get("buffers", [])):
        uri = buffer.get("uri")
        if uri is None:
            raise ValueError(f"glTF buffer {index} has no URI")
        data, _ = _read_uri_bytes(uri, base_path)
        expected_length = buffer.get("byteLength")
        if expected_length is not None and len(data) < int(expected_length):
            raise ValueError(
                f"glTF buffer {index} is shorter than byteLength: "
                f"{len(data)} < {expected_length}"
            )
        buffers.append(data)
    return buffers


def _get_buffer_view_bytes(buffers: list[bytes], buffer_view: dict) -> bytes:
    """Return the raw bytes for a bufferView."""
    buffer_index = int(buffer_view.get("buffer", 0))
    if buffer_index < 0 or buffer_index >= len(buffers):
        raise ValueError(f"bufferView references missing buffer {buffer_index}")
    return buffers[buffer_index]


def _read_accessor(gltf: dict, buffers: list[bytes], accessor_index: int) -> np.ndarray:
    """Read data from an accessor."""
    accessor = gltf["accessors"][accessor_index]
    buffer_view = gltf["bufferViews"][accessor["bufferView"]]
    bin_data = _get_buffer_view_bytes(buffers, buffer_view)

    component_type = accessor["componentType"]
    accessor_type = accessor["type"]
    count = accessor["count"]

    dtype = COMPONENT_TYPE_DTYPE[component_type]
    num_components = TYPE_NUM_COMPONENTS[accessor_type]

    byte_offset = buffer_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    byte_stride = buffer_view.get("byteStride", 0)

    element_size = COMPONENT_TYPE_SIZE[component_type] * num_components

    if byte_stride == 0 or byte_stride == element_size:
        # Tightly packed
        data = np.frombuffer(
            bin_data, dtype=dtype,
            offset=byte_offset,
            count=count * num_components
        )
        if num_components > 1:
            data = data.reshape(count, num_components)
    else:
        # Strided data
        data = np.zeros((count, num_components), dtype=dtype)
        for i in range(count):
            offset = byte_offset + i * byte_stride
            element = np.frombuffer(bin_data, dtype=dtype, offset=offset, count=num_components)
            data[i] = element

    return data.astype(np.float32) if dtype != np.float32 else data


# ---------- PARSING FUNCTIONS ----------

def _parse_meshes(gltf: dict, buffers: list[bytes], scene_data: GLBSceneData):
    """Parse all meshes from glTF."""
    def concat_optional(chunks: list[Optional[np.ndarray]], components: int) -> Optional[np.ndarray]:
        if not any(chunk is not None for chunk in chunks):
            return None
        filled = []
        for vertex_chunk, chunk in zip(vertex_chunks, chunks, strict=True):
            if chunk is None:
                filled.append(np.zeros((len(vertex_chunk), components), dtype=np.float32))
            else:
                filled.append(chunk.astype(np.float32))
        return np.concatenate(filled, axis=0).astype(np.float32)

    for mesh_idx, mesh in enumerate(gltf.get("meshes", [])):
        mesh_name = mesh.get("name", f"Mesh_{mesh_idx}")
        scene_data.mesh_index_map[mesh_idx] = []

        vertex_chunks: list[np.ndarray] = []
        normal_chunks: list[Optional[np.ndarray]] = []
        uv_chunks: list[Optional[np.ndarray]] = []
        tangent_chunks: list[Optional[np.ndarray]] = []
        joint_chunks: list[Optional[np.ndarray]] = []
        weight_chunks: list[Optional[np.ndarray]] = []
        index_chunks: list[np.ndarray] = []
        submeshes: list[GLBSubmeshData] = []
        material_slot_for_index: Dict[int, int] = {}
        first_material_index = -1
        next_vertex_base = 0
        next_first_index = 0

        for prim_idx, primitive in enumerate(mesh.get("primitives", [])):
            attributes = primitive.get("attributes", {})

            # Vertices (required)
            if "POSITION" not in attributes:
                continue
            vertices = _read_accessor(gltf, buffers, attributes["POSITION"])

            # Normals (optional)
            normals = None
            if "NORMAL" in attributes:
                normals = _read_accessor(gltf, buffers, attributes["NORMAL"])

            # UVs (optional)
            uvs = None
            if "TEXCOORD_0" in attributes:
                uvs = _read_accessor(gltf, buffers, attributes["TEXCOORD_0"])

            # Tangents (optional) - vec4 with w = handedness
            tangents = None
            if "TANGENT" in attributes:
                tangents = _read_accessor(gltf, buffers, attributes["TANGENT"])

            # Skinning data (optional)
            joint_indices = None
            joint_weights = None
            if "JOINTS_0" in attributes:
                joint_indices = _read_accessor(gltf, buffers, attributes["JOINTS_0"])
            if "WEIGHTS_0" in attributes:
                joint_weights = _read_accessor(gltf, buffers, attributes["WEIGHTS_0"])

            # Indices
            if "indices" in primitive:
                indices = _read_accessor(gltf, buffers, primitive["indices"]).astype(np.uint32)
                if indices.ndim > 1:
                    indices = indices.flatten()
            else:
                # No indices - sequential
                indices = np.arange(len(vertices), dtype=np.uint32)

            # Material
            material_index = primitive.get("material", 0)
            if first_material_index < 0:
                first_material_index = material_index
            if material_index not in material_slot_for_index:
                material_slot_for_index[material_index] = len(material_slot_for_index)
            material_slot = material_slot_for_index[material_index]

            # Build expanded vertex arrays consumed by mesh asset population.
            expanded_verts = vertices[indices]
            expanded_normals = normals[indices] if normals is not None else None
            expanded_uvs = uvs[indices] if uvs is not None else None
            expanded_tangents = tangents[indices] if tangents is not None else None
            expanded_joints = joint_indices[indices] if joint_indices is not None else None
            expanded_weights = joint_weights[indices] if joint_weights is not None else None

            prim_name = f"{mesh_name}/primitive_{prim_idx}"
            if 0 <= material_index < len(gltf.get("materials", [])):
                material_name = gltf["materials"][material_index].get("name")
                if material_name:
                    prim_name = f"{mesh_name}/{material_name}"

            vertex_chunks.append(expanded_verts.astype(np.float32))
            normal_chunks.append(expanded_normals.astype(np.float32) if expanded_normals is not None else None)
            uv_chunks.append(expanded_uvs.astype(np.float32) if expanded_uvs is not None else None)
            tangent_chunks.append(expanded_tangents.astype(np.float32) if expanded_tangents is not None else None)
            joint_chunks.append(expanded_joints.astype(np.float32) if expanded_joints is not None else None)
            weight_chunks.append(expanded_weights.astype(np.float32) if expanded_weights is not None else None)

            local_indices = np.arange(
                next_vertex_base,
                next_vertex_base + len(expanded_verts),
                dtype=np.uint32,
            )
            index_chunks.append(local_indices)
            submeshes.append(GLBSubmeshData(
                name=prim_name,
                first_index=next_first_index,
                index_count=len(local_indices),
                material_index=material_index,
                material_slot=material_slot,
            ))
            next_vertex_base += len(expanded_verts)
            next_first_index += len(local_indices)

        if not vertex_chunks:
            continue

        vertices = np.concatenate(vertex_chunks, axis=0).astype(np.float32)
        normals = concat_optional(normal_chunks, 3)
        uvs = concat_optional(uv_chunks, 2)
        tangents = concat_optional(tangent_chunks, 4)
        joint_indices = concat_optional(joint_chunks, 4)
        joint_weights = concat_optional(weight_chunks, 4)
        indices = np.concatenate(index_chunks, axis=0).astype(np.uint32)

        our_mesh_idx = len(scene_data.meshes)
        scene_data.mesh_index_map[mesh_idx].append(our_mesh_idx)
        scene_data.meshes.append(GLBMeshData(
            name=mesh_name,
            vertices=vertices,
            normals=normals,
            uvs=uvs,
            indices=indices,
            material_index=first_material_index,
            tangents=tangents,
            joint_indices=joint_indices,
            joint_weights=joint_weights,
            submeshes=submeshes,
        ))


def _parse_materials(gltf: dict, scene_data: GLBSceneData):
    """Parse all materials from glTF."""
    for mat_idx, mat in enumerate(gltf.get("materials", [])):
        name = mat.get("name", f"Material_{mat_idx}")

        base_color = None
        base_color_texture = None
        metallic_factor = 1.0
        roughness_factor = 1.0
        metallic_roughness_texture = None
        normal_texture = None
        normal_scale = 1.0
        occlusion_texture = None
        emissive_texture = None
        emissive_factor = np.array([0.0, 0.0, 0.0], dtype=np.float32)

        pbr = mat.get("pbrMetallicRoughness", {})
        if "baseColorFactor" in pbr:
            base_color = np.array(pbr["baseColorFactor"], dtype=np.float32)
        if "baseColorTexture" in pbr:
            base_color_texture = pbr["baseColorTexture"].get("index")
        if "metallicFactor" in pbr:
            metallic_factor = float(pbr["metallicFactor"])
        if "roughnessFactor" in pbr:
            roughness_factor = float(pbr["roughnessFactor"])
        if "metallicRoughnessTexture" in pbr:
            metallic_roughness_texture = pbr["metallicRoughnessTexture"].get("index")
        if "normalTexture" in mat:
            normal_info = mat["normalTexture"]
            normal_texture = normal_info.get("index")
            if "scale" in normal_info:
                normal_scale = float(normal_info["scale"])
        if "occlusionTexture" in mat:
            occlusion_texture = mat["occlusionTexture"].get("index")
        if "emissiveTexture" in mat:
            emissive_texture = mat["emissiveTexture"].get("index")
        if "emissiveFactor" in mat:
            emissive_factor = np.array(mat["emissiveFactor"], dtype=np.float32)

        scene_data.materials.append(GLBMaterialData(
            name=name,
            base_color=base_color,
            base_color_texture=base_color_texture,
            metallic_factor=metallic_factor,
            roughness_factor=roughness_factor,
            metallic_roughness_texture=metallic_roughness_texture,
            normal_texture=normal_texture,
            normal_scale=normal_scale,
            occlusion_texture=occlusion_texture,
            emissive_texture=emissive_texture,
            emissive_factor=emissive_factor,
        ))


def _parse_textures(
    gltf: dict,
    buffers: list[bytes],
    scene_data: GLBSceneData,
    base_path: Path | None = None,
):
    """Parse all textures/images from glTF."""
    images = gltf.get("images", [])

    for tex_idx, texture in enumerate(gltf.get("textures", [])):
        source_idx = texture.get("source")
        if source_idx is None or source_idx >= len(images):
            continue

        image = images[source_idx]
        name = image.get("name", f"Texture_{tex_idx}")
        mime_type = image.get("mimeType", "image/png")

        # Get image data
        data = None
        source_path = None
        if "bufferView" in image:
            bv = gltf["bufferViews"][image["bufferView"]]
            bin_data = _get_buffer_view_bytes(buffers, bv)
            offset = bv.get("byteOffset", 0)
            length = bv["byteLength"]
            data = bin_data[offset:offset + length]
        elif "uri" in image:
            uri = image["uri"]
            uri_base_path = base_path or Path(".")
            source_path = _external_uri_path(uri, uri_base_path)
            data, uri_mime_type = _read_uri_bytes(uri, uri_base_path)
            if uri_mime_type is not None:
                mime_type = uri_mime_type

        if data:
            scene_data.textures.append(GLBTcTexture(
                index=tex_idx,
                name=name,
                data=data,
                mime_type=mime_type,
                source_path=source_path,
            ))


def _parse_nodes(gltf: dict, scene_data: GLBSceneData):
    """Parse node hierarchy."""
    for node_idx, node in enumerate(gltf.get("nodes", [])):
        name = node.get("name", f"Node_{node_idx}")
        children = node.get("children", [])
        mesh_index = node.get("mesh")
        skin_index = node.get("skin")

        # Transform
        translation = np.array(node.get("translation", [0, 0, 0]), dtype=np.float32)
        rotation = np.array(node.get("rotation", [0, 0, 0, 1]), dtype=np.float32)  # xyzw
        scale = np.array(node.get("scale", [1, 1, 1]), dtype=np.float32)

        # If matrix is provided, decompose it (simplified - just use TRS if available)
        if "matrix" in node and "translation" not in node:
            # TODO: decompose matrix if needed
            pass

        scene_data.nodes.append(GLBNodeData(
            name=name,
            children=children,
            mesh_index=mesh_index,
            skin_index=skin_index,
            translation=translation,
            rotation=rotation,
            scale=scale,
        ))

    # Root nodes from default scene
    scenes = gltf.get("scenes", [])
    default_scene = gltf.get("scene", 0)
    if scenes and default_scene < len(scenes):
        scene_data.root_nodes = scenes[default_scene].get("nodes", [])


def _parse_skins(gltf: dict, buffers: list[bytes], scene_data: GLBSceneData):
    """Parse skins (skeletons) from glTF."""
    for skin_idx, skin in enumerate(gltf.get("skins", [])):
        name = skin.get("name", f"Skin_{skin_idx}")
        joint_node_indices = skin.get("joints", [])
        armature_node_index = skin.get("skeleton")  # Armature node in Blender

        # Read inverse bind matrices
        inverse_bind_matrices = None
        if "inverseBindMatrices" in skin:
            ibm_accessor_idx = skin["inverseBindMatrices"]
            ibm_data = _read_accessor(gltf, buffers, ibm_accessor_idx)
            # Reshape to (N, 4, 4) matrices
            # glTF stores matrices in column-major order, so transpose each one
            inverse_bind_matrices = ibm_data.reshape(-1, 4, 4).astype(np.float32)
            inverse_bind_matrices = np.ascontiguousarray(inverse_bind_matrices.transpose(0, 2, 1))
        else:
            # Default to identity matrices
            n_joints = len(joint_node_indices)
            inverse_bind_matrices = np.zeros((n_joints, 4, 4), dtype=np.float32)
            for i in range(n_joints):
                inverse_bind_matrices[i] = np.eye(4, dtype=np.float32)

        scene_data.skins.append(GLBSkinData(
            name=name,
            joint_node_indices=joint_node_indices,
            inverse_bind_matrices=inverse_bind_matrices,
            armature_node_index=armature_node_index,
        ))


def _parse_animations(gltf: dict, buffers: list[bytes], scene_data: GLBSceneData):
    """Parse animations from glTF."""
    nodes = gltf.get("nodes", [])

    for anim_idx, anim in enumerate(gltf.get("animations", [])):
        anim_name = anim.get("name", f"Animation_{anim_idx}")

        # Group channels by target node
        node_channels: Dict[int, Dict[str, Any]] = {}

        for channel in anim.get("channels", []):
            sampler_idx = channel["sampler"]
            sampler = anim["samplers"][sampler_idx]

            target = channel["target"]
            node_idx = target.get("node")
            path = target.get("path")  # translation, rotation, scale, weights

            if node_idx is None or path not in ("translation", "rotation", "scale"):
                continue

            # Read input (times) and output (values)
            times = _read_accessor(gltf, buffers, sampler["input"])
            values = _read_accessor(gltf, buffers, sampler["output"])

            if times.ndim > 1:
                times = times.flatten()

            if node_idx not in node_channels:
                node_channels[node_idx] = {
                    "pos_keys": [],
                    "rot_keys": [],
                    "scale_keys": [],
                }

            # Build keyframe list (use lists, not tuples, for mutability in normalize_glb_scale)
            keys = []
            for i, t in enumerate(times):
                if values.ndim == 1:
                    v = values[i:i+1]
                else:
                    v = values[i]
                keys.append([float(t), v.copy()])

            if path == "translation":
                node_channels[node_idx]["pos_keys"] = keys
            elif path == "rotation":
                node_channels[node_idx]["rot_keys"] = keys
            elif path == "scale":
                node_channels[node_idx]["scale_keys"] = keys

        # Build animation channels
        channels = []
        max_time = 0.0

        for node_idx, ch_data in node_channels.items():
            node_name = nodes[node_idx].get("name", f"Node_{node_idx}") if node_idx < len(nodes) else f"Node_{node_idx}"

            # Track max time for duration
            for keys in [ch_data["pos_keys"], ch_data["rot_keys"], ch_data["scale_keys"]]:
                if keys:
                    max_time = max(max_time, keys[-1][0])

            channels.append(GLBAnimationChannel(
                node_index=node_idx,
                node_name=node_name,
                pos_keys=ch_data["pos_keys"],
                rot_keys=ch_data["rot_keys"],
                scale_keys=ch_data["scale_keys"],
            ))

        if channels:
            scene_data.animations.append(GLBAnimationClip(
                name=anim_name,
                channels=channels,
                duration=max_time,
            ))


# ---------- PUBLIC API ----------

def _build_scene_data(gltf: dict, buffers: list[bytes], base_path: Path | None = None) -> GLBSceneData:
    """Parse glTF JSON and buffers into scene data."""
    scene_data = GLBSceneData()

    _parse_nodes(gltf, scene_data)
    _parse_materials(gltf, scene_data)
    _parse_textures(gltf, buffers, scene_data, base_path)
    _parse_meshes(gltf, buffers, scene_data)
    _parse_skins(gltf, buffers, scene_data)
    _parse_animations(gltf, buffers, scene_data)

    return scene_data


def load_glb_file(path: str | Path) -> GLBSceneData:
    """Load a GLB or JSON glTF file and return scene data.

    Args:
        path: Path to .glb or .gltf file

    Returns:
        GLBSceneData containing meshes, materials, textures, animations, nodes
    """
    path = Path(path)

    with open(path, "rb") as f:
        data = f.read()

    # Parse header
    if len(data) < 12:
        raise ValueError("File too small to be valid GLB")

    magic = data[0:4]
    if magic != b"glTF":
        if path.suffix.lower() != ".gltf":
            raise ValueError(f"Invalid GLB magic: {magic}")
        gltf = json.loads(data.decode("utf-8"))
        buffers = _load_gltf_buffers(gltf, path.parent)
        return _build_scene_data(gltf, buffers, path.parent)

    version, length = struct.unpack("<II", data[4:12])
    if version != 2:
        raise ValueError(f"Unsupported glTF version: {version}")

    # Parse chunks
    offset = 12
    json_data = None
    bin_data = None

    while offset < len(data):
        if offset + 8 > len(data):
            break

        chunk_length, chunk_type = struct.unpack("<II", data[offset:offset + 8])
        chunk_data = data[offset + 8:offset + 8 + chunk_length]

        if chunk_type == 0x4E4F534A:  # JSON
            json_data = chunk_data.decode("utf-8")
        elif chunk_type == 0x004E4942:  # BIN
            bin_data = chunk_data

        # Align to 4 bytes
        offset += 8 + chunk_length
        offset = (offset + 3) & ~3

    if json_data is None:
        raise ValueError("No JSON chunk found in GLB")

    gltf = json.loads(json_data)
    buffers = [bin_data or b""]

    return _build_scene_data(gltf, buffers, path.parent)


def convert_y_up_to_z_up(scene_data: GLBSceneData) -> None:
    """
    Convert GLB data from glTF Y-up coordinate system to engine Z-up.

    glTF coordinate system: Y-up, -Z forward, X right
    Engine coordinate system: Z-up, Y forward, X right

    Conversion:
        X_engine = X_gltf
        Y_engine = -Z_gltf
        Z_engine = Y_gltf

    This modifies scene_data in place, converting:
    1. Vertex positions and normals
    2. Node translations and rotations
    3. Inverse bind matrices
    4. Animation keyframes (positions and rotations)

    Args:
        scene_data: GLBSceneData to convert (modified in place)
    """

    def convert_position(pos: np.ndarray) -> np.ndarray:
        """Convert position vector: (x, y, z) -> (x, -z, y)"""
        return np.array([pos[0], -pos[2], pos[1]], dtype=np.float32)

    def convert_positions_batch(positions: np.ndarray) -> np.ndarray:
        """Convert array of positions: (..., 3) shape"""
        result = np.empty_like(positions)
        result[..., 0] = positions[..., 0]
        result[..., 1] = -positions[..., 2]
        result[..., 2] = positions[..., 1]
        return result

    def convert_quaternion(q: np.ndarray) -> np.ndarray:
        """
        Convert quaternion from Y-up to Z-up.

        Quaternion (x, y, z, w) represents rotation around axis (x, y, z).
        We need to convert the axis using the same coordinate transform.
        """
        # q = (x, y, z, w) -> (x, -z, y, w)
        return np.array([q[0], -q[2], q[1], q[3]], dtype=np.float32)

    def convert_matrix(m: np.ndarray) -> np.ndarray:
        """
        Convert 4x4 transformation matrix from Y-up to Z-up.

        We apply: M' = C @ M @ C^(-1)
        where C is the coordinate conversion matrix.
        """
        # Conversion matrix: swaps Y and Z, negates new Y
        # [1  0  0  0]
        # [0  0 -1  0]
        # [0  1  0  0]
        # [0  0  0  1]
        c = np.array([
            [1, 0, 0, 0],
            [0, 0, -1, 0],
            [0, 1, 0, 0],
            [0, 0, 0, 1],
        ], dtype=np.float32)

        # C^(-1) is the transpose since it's orthogonal
        c_inv = c.T

        return c @ m @ c_inv

    def convert_tangents_batch(tangents: np.ndarray) -> np.ndarray:
        """Convert array of tangents: (..., 4) shape, preserving w (handedness)."""
        result = np.empty_like(tangents)
        result[..., 0] = tangents[..., 0]
        result[..., 1] = -tangents[..., 2]
        result[..., 2] = tangents[..., 1]
        result[..., 3] = tangents[..., 3]  # Keep w (handedness)
        return result

    # 1. Convert vertex positions, normals, and tangents in all meshes
    for mesh in scene_data.meshes:
        mesh.vertices = convert_positions_batch(mesh.vertices)
        if mesh.normals is not None:
            mesh.normals = convert_positions_batch(mesh.normals)
        if mesh.tangents is not None:
            mesh.tangents = convert_tangents_batch(mesh.tangents)

    # 2. Convert node transforms
    for node in scene_data.nodes:
        node.translation = convert_position(node.translation)
        node.rotation = convert_quaternion(node.rotation)
        # Scale is scalar per-axis, doesn't change with coordinate system
        # but axes swap: (sx, sy, sz) -> (sx, sz, sy)
        node.scale = np.array([node.scale[0], node.scale[2], node.scale[1]], dtype=np.float32)

    # 3. Convert inverse bind matrices
    for skin in scene_data.skins:
        for i in range(len(skin.inverse_bind_matrices)):
            skin.inverse_bind_matrices[i] = convert_matrix(skin.inverse_bind_matrices[i])

    # 4. Convert animation keyframes
    for anim in scene_data.animations:
        for channel in anim.channels:
            # Position keys
            if channel.pos_keys:
                for key in channel.pos_keys:
                    key[1] = convert_position(key[1])

            # Rotation keys
            if channel.rot_keys:
                for key in channel.rot_keys:
                    key[1] = convert_quaternion(key[1])

            # Scale keys - swap Y and Z
            if channel.scale_keys:
                for key in channel.scale_keys:
                    s = key[1]
                    key[1] = np.array([s[0], s[2], s[1]], dtype=np.float32)


def normalize_glb_scale(scene_data: GLBSceneData) -> bool:
    """
    Normalize root scale to 1.0 by applying inverse scale.

    If the root node has scale != 1.0, this function:
    1. Sets root node scale to 1.0
    2. Multiplies all IBM by scale matrix (compensates mesh)
    3. Multiplies translation of all child nodes by root_scale (compensates transforms)

    Args:
        scene_data: GLBSceneData to normalize (modified in place)

    Returns:
        True if normalization was performed, False if already normalized
    """
    if not scene_data.root_nodes or not scene_data.nodes:
        return False

    root_idx = scene_data.root_nodes[0]
    if root_idx >= len(scene_data.nodes):
        return False

    root_node = scene_data.nodes[root_idx]
    root_scale = root_node.scale

    # Check if already normalized
    if np.allclose(root_scale, [1.0, 1.0, 1.0]):
        return False

    # Use uniform scale factor (assumes x == y == z)
    scale_factor = float(root_scale[0])

    # 1. Set root node scale to 1.0
    root_node.scale = np.array([1.0, 1.0, 1.0], dtype=np.float32)

    # 2. Build scale matrix from root_scale for IBM compensation
    scale_matrix = np.diag([root_scale[0], root_scale[1], root_scale[2], 1.0]).astype(np.float32)
    # 3. Compensate IBM: scale applies to input vertex coordinates
    for skin in scene_data.skins:
        for i in range(len(skin.inverse_bind_matrices)):
            skin.inverse_bind_matrices[i] = scale_matrix @ skin.inverse_bind_matrices[i]

    # 4. Scale translation of all child nodes (recursive from root)
    def scale_children_translation(node_idx: int) -> None:
        node = scene_data.nodes[node_idx]
        for child_idx in node.children:
            child_node = scene_data.nodes[child_idx]
            child_node.translation = child_node.translation * scale_factor
            scale_children_translation(child_idx)

    scale_children_translation(root_idx)

    # 5. Scale animation translation keyframes
    for anim in scene_data.animations:
        for channel in anim.channels:
            for key in channel.pos_keys:
                # key = [time, [x, y, z]]
                key[1] = key[1] * scale_factor

    # Store original scale for reference
    scene_data.skin_scale = scale_factor

    return True


def _qmul(q1: np.ndarray, q2: np.ndarray) -> np.ndarray:
    """Multiply two quaternions (x, y, z, w)."""
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return np.array([
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
    ], dtype=np.float32)


def apply_blender_z_up_fix(scene_data: GLBSceneData) -> None:
    """
    Fix Blender's -90°X rotation on Armature when exporting to glTF.

    Blender applies -90°X rotation to the Armature node during export.
    This function compensates by:
    - Rotating root bone (Hips) by +90° around X (and its keyframes)
    - Rotating Armature node by -90° around X

    Args:
        scene_data: GLBSceneData to fix (modified in place)
    """
    # +90° around X: quaternion [sin(45°), 0, 0, cos(45°)]
    rot_pos_90_x = np.array([0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32)
    # -90° around X: quaternion [-sin(45°), 0, 0, cos(45°)]
    rot_neg_90_x = np.array([-0.70710678, 0.0, 0.0, 0.70710678], dtype=np.float32)

    # Rotate root nodes of scene by -90° X
    for root_idx in scene_data.root_nodes:
        if root_idx < len(scene_data.nodes):
            root_node = scene_data.nodes[root_idx]
            root_node.rotation = _qmul(rot_neg_90_x, root_node.rotation)

    # Transform root bone (first joint, e.g. Hips) by +90° X
    # This is a full transform: rotation, translation, and scale
    def transform_pos_90_x(pos: np.ndarray) -> np.ndarray:
        """Apply +90° X rotation to position: (x, y, z) -> (x, -z, y)"""
        return np.array([pos[0], -pos[2], pos[1]], dtype=pos.dtype)

    def transform_scale_90_x(scale: np.ndarray) -> np.ndarray:
        """Apply +90° X rotation to scale: (sx, sy, sz) -> (sx, sz, sy)"""
        return np.array([scale[0], scale[2], scale[1]], dtype=scale.dtype)

    if scene_data.skins:
        skin = scene_data.skins[0]
        if skin.joint_node_indices:
            root_bone_idx = skin.joint_node_indices[0]
            if root_bone_idx < len(scene_data.nodes):
                root_bone_node = scene_data.nodes[root_bone_idx]
                root_bone_name = root_bone_node.name
                root_bone_node.translation = transform_pos_90_x(root_bone_node.translation)
                root_bone_node.rotation = _qmul(rot_pos_90_x, root_bone_node.rotation)
                root_bone_node.scale = transform_scale_90_x(root_bone_node.scale)

                # Transform root bone animation keyframes by +90° X
                for anim in scene_data.animations:
                    for channel in anim.channels:
                        if channel.node_name == root_bone_name:
                            for key in channel.pos_keys:
                                key[1] = transform_pos_90_x(key[1])
                            for key in channel.rot_keys:
                                key[1] = _qmul(rot_pos_90_x, key[1])
                            for key in channel.scale_keys:
                                key[1] = transform_scale_90_x(key[1])


def load_glb_file_normalized(
    path: str | Path,
    normalize_scale: bool = False,
    convert_to_z_up: bool = True,
    blender_z_up_fix: bool = False,
) -> GLBSceneData:
    """
    Load GLB file with optional transformations.

    Args:
        path: Path to .glb file
        normalize_scale: If True, normalize root scale to 1.0
        convert_to_z_up: If True, convert from glTF Y-up to engine Z-up (default True)
        blender_z_up_fix: If True, compensate for Blender's -90°X rotation on Armature

    Returns:
        GLBSceneData (possibly transformed)
    """
    scene_data = load_glb_file(path)

    # Coordinate conversion first (before scale normalization)
    if convert_to_z_up:
        convert_y_up_to_z_up(scene_data)

    # Blender-specific fix (after coordinate conversion)
    if blender_z_up_fix:
        apply_blender_z_up_fix(scene_data)

    if normalize_scale:
        normalize_glb_scale(scene_data)

    return scene_data


def load_glb_file_from_buffer(
    data: bytes,
    normalize_scale: bool = False,
    convert_to_z_up: bool = True,
    blender_z_up_fix: bool = False,
) -> GLBSceneData:
    """
    Load GLB from binary buffer with optional transformations.

    Args:
        data: Binary GLB data
        normalize_scale: If True, normalize root scale to 1.0
        convert_to_z_up: If True, convert from glTF Y-up to engine Z-up (default True)
        blender_z_up_fix: If True, compensate for Blender's -90°X rotation on Armature

    Returns:
        GLBSceneData (possibly transformed)
    """
    # Parse header
    if len(data) < 12:
        raise ValueError("Data too small to be valid GLB")

    magic = data[0:4]
    if magic != b"glTF":
        raise ValueError(f"Invalid GLB magic: {magic}")

    version, length = struct.unpack("<II", data[4:12])
    if version != 2:
        raise ValueError(f"Unsupported glTF version: {version}")

    # Parse chunks
    offset = 12
    json_data = None
    bin_data = None

    while offset < len(data):
        if offset + 8 > len(data):
            break

        chunk_length, chunk_type = struct.unpack("<II", data[offset:offset + 8])
        chunk_data = data[offset + 8:offset + 8 + chunk_length]

        if chunk_type == 0x4E4F534A:  # JSON
            json_data = chunk_data.decode("utf-8")
        elif chunk_type == 0x004E4942:  # BIN
            bin_data = chunk_data

        # Align to 4 bytes
        offset += 8 + chunk_length
        offset = (offset + 3) & ~3

    if json_data is None:
        raise ValueError("No JSON chunk found in GLB")

    gltf = json.loads(json_data)
    buffers = [bin_data or b""]

    scene_data = _build_scene_data(gltf, buffers)

    # Coordinate conversion first (before scale normalization)
    if convert_to_z_up:
        convert_y_up_to_z_up(scene_data)

    # Blender-specific fix (after coordinate conversion)
    if blender_z_up_fix:
        apply_blender_z_up_fix(scene_data)

    if normalize_scale:
        normalize_glb_scale(scene_data)

    return scene_data
