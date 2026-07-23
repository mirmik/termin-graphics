# termin/loaders/glb_loader.py
"""GLB/glTF 2.0 loader.

Pure Python implementation without external dependencies (except numpy).
"""

from __future__ import annotations

import json
import struct
import base64
import mimetypes
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional

import numpy as np

from tcbase import log


class _GLBLoadTrace:
    """Realtime timing trace for one GLB/glTF parse operation."""

    def __init__(self, source: str):
        self.source = source
        self.thread_id = threading.get_ident()
        self.started_at = time.perf_counter()

    def begin(self) -> None:
        log.info(f"[GLBLoad] begin source='{self.source}' thread={self.thread_id}")

    def complete(self, scene_data: "GLBSceneData") -> None:
        log.info(
            f"[GLBLoad] complete source='{self.source}' "
            f"meshes={len(scene_data.meshes)} nodes={len(scene_data.nodes)} "
            f"materials={len(scene_data.materials)} textures={len(scene_data.textures)} "
            f"skins={len(scene_data.skins)} animations={len(scene_data.animations)} "
            f"duration_ms={(time.perf_counter() - self.started_at) * 1000.0:.3f} "
            f"thread={self.thread_id}"
        )

    def failed(self) -> None:
        log.error(
            f"[GLBLoad] failed source='{self.source}' "
            f"duration_ms={(time.perf_counter() - self.started_at) * 1000.0:.3f} "
            f"thread={self.thread_id}",
            exc_info=True,
        )

    @contextmanager
    def stage(self, name: str, **fields: object) -> Iterator[None]:
        details = self._format_fields(fields)
        started_at = time.perf_counter()
        log.info(
            f"[GLBLoad] stage-begin stage={name}{details} "
            f"source='{self.source}' thread={self.thread_id}"
        )
        try:
            yield
        except Exception:
            log.error(
                f"[GLBLoad] stage-failed stage={name}{details} "
                f"duration_ms={(time.perf_counter() - started_at) * 1000.0:.3f} "
                f"source='{self.source}' thread={self.thread_id}",
                exc_info=True,
            )
            raise
        log.info(
            f"[GLBLoad] stage-end stage={name}{details} "
            f"duration_ms={(time.perf_counter() - started_at) * 1000.0:.3f} "
            f"source='{self.source}' thread={self.thread_id}"
        )

    @staticmethod
    def _format_fields(fields: dict[str, object]) -> str:
        return "".join(
            f" {key}='{value}'" if isinstance(value, str) else f" {key}={value}"
            for key, value in fields.items()
        )


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

_NORMALIZED_COMPONENT_MAX = {
    5120: 127.0,          # BYTE
    5121: 255.0,          # UNSIGNED_BYTE
    5122: 32767.0,        # SHORT
    5123: 65535.0,        # UNSIGNED_SHORT
}

_UNSIGNED_INDEX_COMPONENT_TYPES = {5121, 5123, 5125}
_UNSIGNED_JOINT_COMPONENT_TYPES = {5121, 5123}


def _normalize_accessor_components(data: np.ndarray, component_type: int) -> np.ndarray:
    """Apply glTF normalized-integer conversion to accessor components."""
    if component_type not in _NORMALIZED_COMPONENT_MAX:
        raise ValueError(f"glTF normalized accessor has unsupported componentType {component_type}")

    normalized = data.astype(np.float32)
    normalized /= _NORMALIZED_COMPONENT_MAX[component_type]
    if component_type in (5120, 5122):
        np.maximum(normalized, -1.0, out=normalized)
    return normalized


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


def _read_accessor(
    gltf: dict,
    buffers: list[bytes],
    accessor_index: int,
) -> np.ndarray:
    """Read one non-sparse accessor, preserving its declared component representation."""
    accessors = gltf.get("accessors", [])
    if accessor_index < 0 or accessor_index >= len(accessors):
        raise ValueError(f"accessor index {accessor_index} is out of range")
    accessor = accessors[accessor_index]
    if "sparse" in accessor:
        raise ValueError(f"sparse accessor {accessor_index} is not supported")
    if "bufferView" not in accessor:
        raise ValueError(f"accessor {accessor_index} has no bufferView")

    buffer_view_index = int(accessor["bufferView"])
    buffer_views = gltf.get("bufferViews", [])
    if buffer_view_index < 0 or buffer_view_index >= len(buffer_views):
        raise ValueError(f"accessor {accessor_index} references missing bufferView {buffer_view_index}")
    buffer_view = buffer_views[buffer_view_index]
    bin_data = _get_buffer_view_bytes(buffers, buffer_view)

    component_type = int(accessor["componentType"])
    accessor_type = accessor["type"]
    count = int(accessor["count"])
    if count < 0:
        raise ValueError(f"accessor {accessor_index} has a negative count")
    if component_type not in COMPONENT_TYPE_DTYPE:
        raise ValueError(f"accessor {accessor_index} has unsupported componentType {component_type}")
    if accessor_type not in TYPE_NUM_COMPONENTS:
        raise ValueError(f"accessor {accessor_index} has unsupported type {accessor_type}")
    if accessor_type.startswith("MAT") and component_type != 5126:
        raise ValueError(
            f"accessor {accessor_index} uses unsupported packed integer matrix representation"
        )

    dtype = np.dtype(COMPONENT_TYPE_DTYPE[component_type]).newbyteorder("<")
    num_components = TYPE_NUM_COMPONENTS[accessor_type]

    buffer_view_offset = int(buffer_view.get("byteOffset", 0))
    buffer_view_length = int(buffer_view.get("byteLength", len(bin_data) - buffer_view_offset))
    accessor_offset = int(accessor.get("byteOffset", 0))
    byte_offset = buffer_view_offset + accessor_offset
    byte_stride = int(buffer_view.get("byteStride", 0))

    element_size = COMPONENT_TYPE_SIZE[component_type] * num_components
    if buffer_view_offset < 0 or accessor_offset < 0 or buffer_view_length < 0:
        raise ValueError(f"accessor {accessor_index} has a negative byte offset or bufferView length")
    if buffer_view_offset + buffer_view_length > len(bin_data):
        raise ValueError(f"accessor {accessor_index} bufferView exceeds its buffer bounds")
    if byte_stride and byte_stride < element_size:
        raise ValueError(f"accessor {accessor_index} byteStride is smaller than its element size")
    effective_stride = byte_stride or element_size
    required_end = byte_offset
    if count:
        required_end += (count - 1) * effective_stride + element_size
    buffer_view_end = buffer_view_offset + buffer_view_length
    if required_end > buffer_view_end or required_end > len(bin_data):
        raise ValueError(f"accessor {accessor_index} exceeds its bufferView bounds")

    if byte_stride == 0 or byte_stride == element_size:
        data = np.frombuffer(
            bin_data,
            dtype=dtype,
            offset=byte_offset,
            count=count * num_components,
        )
        if num_components > 1:
            data = data.reshape(count, num_components)
    else:
        data = np.zeros((count, num_components), dtype=dtype)
        for i in range(count):
            offset = byte_offset + i * byte_stride
            data[i] = np.frombuffer(bin_data, dtype=dtype, offset=offset, count=num_components)

    if accessor.get("normalized", False):
        if component_type == 5126:
            raise ValueError(f"accessor {accessor_index} sets normalized on FLOAT components")
        return _normalize_accessor_components(data, component_type)
    return data


def _read_integral_accessor(
    gltf: dict,
    buffers: list[bytes],
    accessor_index: int,
    allowed_component_types: set[int],
    usage: str,
    required_type: str,
) -> np.ndarray:
    """Read an accessor that glTF requires to remain in an integer domain."""
    accessor = gltf["accessors"][accessor_index]
    component_type = int(accessor["componentType"])
    if component_type not in allowed_component_types:
        raise ValueError(f"{usage} accessor {accessor_index} must use an unsigned integer component type")
    if accessor.get("normalized", False):
        raise ValueError(f"{usage} accessor {accessor_index} must not be normalized")
    if accessor.get("type") != required_type:
        raise ValueError(f"{usage} accessor {accessor_index} must have type {required_type}")
    return _read_accessor(gltf, buffers, accessor_index)


def _read_weight_accessor(gltf: dict, buffers: list[bytes], accessor_index: int) -> np.ndarray:
    """Read WEIGHTS_0 while enforcing glTF's integer normalization contract."""
    accessor = gltf["accessors"][accessor_index]
    component_type = int(accessor["componentType"])
    if accessor.get("type") != "VEC4":
        raise ValueError(f"WEIGHTS_0 accessor {accessor_index} must have type VEC4")
    if component_type != 5126 and not accessor.get("normalized", False):
        raise ValueError(f"integer WEIGHTS_0 accessor {accessor_index} must be normalized")
    return _read_accessor(gltf, buffers, accessor_index).astype(np.float32)


def _read_float_vertex_attribute(
    gltf: dict,
    buffers: list[bytes],
    accessor_index: int,
    semantic: str,
) -> np.ndarray:
    """Read a mesh vertex attribute into the engine's float representation."""
    accessor = gltf["accessors"][accessor_index]
    if int(accessor["componentType"]) != 5126:
        extensions_used = gltf.get("extensionsUsed", [])
        if "KHR_mesh_quantization" not in extensions_used:
            raise ValueError(
                f"{semantic} accessor {accessor_index} uses integer components without KHR_mesh_quantization"
            )
    return _read_accessor(gltf, buffers, accessor_index).astype(np.float32)


def _quaternion_from_rotation_matrix(rotation: np.ndarray) -> np.ndarray:
    """Convert a proper 3x3 rotation matrix to a normalized xyzw quaternion."""
    trace = float(np.trace(rotation))
    if trace > 0.0:
        root = np.sqrt(trace + 1.0) * 2.0
        quaternion = np.array([
            (rotation[2, 1] - rotation[1, 2]) / root,
            (rotation[0, 2] - rotation[2, 0]) / root,
            (rotation[1, 0] - rotation[0, 1]) / root,
            0.25 * root,
        ], dtype=np.float64)
    else:
        axis = int(np.argmax(np.diag(rotation)))
        next_axis = (axis + 1) % 3
        last_axis = (axis + 2) % 3
        root = np.sqrt(
            1.0 + rotation[axis, axis] - rotation[next_axis, next_axis] - rotation[last_axis, last_axis]
        ) * 2.0
        quaternion = np.zeros(4, dtype=np.float64)
        quaternion[axis] = 0.25 * root
        quaternion[next_axis] = (rotation[next_axis, axis] + rotation[axis, next_axis]) / root
        quaternion[last_axis] = (rotation[last_axis, axis] + rotation[axis, last_axis]) / root
        quaternion[3] = (rotation[last_axis, next_axis] - rotation[next_axis, last_axis]) / root

    quaternion /= np.linalg.norm(quaternion)
    if quaternion[3] < 0.0:
        quaternion = -quaternion
    return quaternion.astype(np.float32)


def _complete_rotation_basis(columns: np.ndarray, nonzero_axes: list[int]) -> np.ndarray:
    """Complete the nonzero rotation columns of a rank-deficient TRS matrix."""
    rotation = np.zeros((3, 3), dtype=np.float64)
    for axis in nonzero_axes:
        rotation[:, axis] = columns[:, axis]

    if len(nonzero_axes) == 3:
        return rotation
    if len(nonzero_axes) == 2:
        missing_axis = next(axis for axis in range(3) if axis not in nonzero_axes)
        if missing_axis == 0:
            rotation[:, 0] = np.cross(rotation[:, 1], rotation[:, 2])
        elif missing_axis == 1:
            rotation[:, 1] = np.cross(rotation[:, 2], rotation[:, 0])
        else:
            rotation[:, 2] = np.cross(rotation[:, 0], rotation[:, 1])
        return rotation
    if len(nonzero_axes) == 1:
        known_axis = nonzero_axes[0]
        known_column = rotation[:, known_axis]
        candidate_axis = int(np.argmin(np.abs(known_column)))
        candidate = np.zeros(3, dtype=np.float64)
        candidate[candidate_axis] = 1.0
        if known_axis == 0:
            rotation[:, 1] = np.cross(known_column, candidate)
            rotation[:, 1] /= np.linalg.norm(rotation[:, 1])
            rotation[:, 2] = np.cross(rotation[:, 0], rotation[:, 1])
        elif known_axis == 1:
            rotation[:, 2] = np.cross(known_column, candidate)
            rotation[:, 2] /= np.linalg.norm(rotation[:, 2])
            rotation[:, 0] = np.cross(rotation[:, 1], rotation[:, 2])
        else:
            rotation[:, 0] = np.cross(candidate, known_column)
            rotation[:, 0] /= np.linalg.norm(rotation[:, 0])
            rotation[:, 1] = np.cross(rotation[:, 2], rotation[:, 0])
        return rotation
    return np.eye(3, dtype=np.float64)


def _decompose_node_matrix(matrix_values: Any, node_index: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Decompose a glTF column-major affine matrix into translation, rotation and scale."""
    matrix_data = np.asarray(matrix_values, dtype=np.float64)
    if matrix_data.shape != (16,) or not np.all(np.isfinite(matrix_data)):
        raise ValueError(f"node {node_index} matrix must contain 16 finite values")

    matrix = matrix_data.reshape((4, 4), order="F")
    if not np.allclose(matrix[3], [0.0, 0.0, 0.0, 1.0], atol=1.0e-6, rtol=0.0):
        raise ValueError(f"node {node_index} matrix must be affine")

    translation = matrix[:3, 3].astype(np.float32)
    linear = matrix[:3, :3]
    scale = np.linalg.norm(linear, axis=0)
    nonzero_axes = [axis for axis, value in enumerate(scale) if value > 1.0e-8]
    normalized_columns = np.zeros((3, 3), dtype=np.float64)
    for axis in nonzero_axes:
        normalized_columns[:, axis] = linear[:, axis] / scale[axis]

    for first_index, first_axis in enumerate(nonzero_axes):
        for second_axis in nonzero_axes[first_index + 1:]:
            if not np.isclose(
                np.dot(normalized_columns[:, first_axis], normalized_columns[:, second_axis]),
                0.0,
                atol=1.0e-5,
                rtol=1.0e-5,
            ):
                raise ValueError(f"node {node_index} matrix contains shear and cannot be represented as TRS")

    rotation = _complete_rotation_basis(normalized_columns, nonzero_axes)

    if not np.allclose(rotation.T @ rotation, np.eye(3), atol=1.0e-5, rtol=1.0e-5):
        raise ValueError(f"node {node_index} matrix contains shear and cannot be represented as TRS")

    determinant = float(np.linalg.det(rotation))
    if determinant < 0.0:
        reflection_axis = int(np.argmax(np.abs(scale)))
        scale[reflection_axis] *= -1.0
        rotation[:, reflection_axis] *= -1.0
        determinant = float(np.linalg.det(rotation))
    if not np.isclose(determinant, 1.0, atol=1.0e-5, rtol=1.0e-5):
        raise ValueError(f"node {node_index} matrix has an invalid rotation basis")

    if not np.allclose(rotation @ np.diag(scale), linear, atol=1.0e-5, rtol=1.0e-5):
        raise ValueError(f"node {node_index} matrix cannot be reconstructed as TRS")

    return translation, _quaternion_from_rotation_matrix(rotation), scale.astype(np.float32)


# ---------- PARSING FUNCTIONS ----------

def _parse_meshes(
    gltf: dict,
    buffers: list[bytes],
    scene_data: GLBSceneData,
    trace: _GLBLoadTrace | None = None,
):
    """Parse all meshes from glTF."""
    for mesh_idx, mesh in enumerate(gltf.get("meshes", [])):
        mesh_name = mesh.get("name", f"Mesh_{mesh_idx}")
        mesh_started_at = time.perf_counter()
        if trace is not None:
            log.info(
                f"[GLBLoad] mesh-begin mesh_index={mesh_idx} name='{mesh_name}' "
                f"primitives={len(mesh.get('primitives', []))} "
                f"source='{trace.source}' thread={trace.thread_id}"
            )
        scene_data.mesh_index_map[mesh_idx] = []

        primitive_records: list[dict[str, Any]] = []
        index_chunks: list[np.ndarray] = []
        submeshes: list[GLBSubmeshData] = []
        material_slot_for_index: Dict[int, int] = {}
        first_material_index = -1
        next_first_index = 0
        has_normals = False
        has_uvs = False
        has_tangents = False
        has_joints = False
        has_weights = False

        for prim_idx, primitive in enumerate(mesh.get("primitives", [])):
            primitive_started_at = time.perf_counter()
            attributes = primitive.get("attributes", {})
            if trace is not None:
                log.info(
                    f"[GLBLoad] primitive-begin mesh_index={mesh_idx} "
                    f"primitive_index={prim_idx} name='{mesh_name}' "
                    f"attributes={len(attributes)} source='{trace.source}' "
                    f"thread={trace.thread_id}"
                )

            # Vertices (required)
            if "POSITION" not in attributes:
                if trace is not None:
                    log.info(
                        f"[GLBLoad] primitive-end mesh_index={mesh_idx} "
                        f"primitive_index={prim_idx} name='{mesh_name}' status=skipped "
                        f"duration_ms="
                        f"{(time.perf_counter() - primitive_started_at) * 1000.0:.3f} "
                        f"source='{trace.source}' thread={trace.thread_id}"
                    )
                continue
            vertices = _read_float_vertex_attribute(gltf, buffers, attributes["POSITION"], "POSITION")

            # Normals (optional)
            normals = None
            if "NORMAL" in attributes:
                normals = _read_float_vertex_attribute(gltf, buffers, attributes["NORMAL"], "NORMAL")
                has_normals = True

            # UVs (optional)
            uvs = None
            if "TEXCOORD_0" in attributes:
                uvs = _read_float_vertex_attribute(gltf, buffers, attributes["TEXCOORD_0"], "TEXCOORD_0")
                has_uvs = True

            # Tangents (optional) - vec4 with w = handedness
            tangents = None
            if "TANGENT" in attributes:
                tangents = _read_float_vertex_attribute(gltf, buffers, attributes["TANGENT"], "TANGENT")
                has_tangents = True

            # Skinning data (optional)
            joint_indices = None
            joint_weights = None
            if "JOINTS_0" in attributes:
                joint_indices = _read_integral_accessor(
                    gltf,
                    buffers,
                    attributes["JOINTS_0"],
                    _UNSIGNED_JOINT_COMPONENT_TYPES,
                    "JOINTS_0",
                    "VEC4",
                ).astype(np.uint32)
                has_joints = True
            if "WEIGHTS_0" in attributes:
                joint_weights = _read_weight_accessor(gltf, buffers, attributes["WEIGHTS_0"])
                has_weights = True

            # Indices
            if "indices" in primitive:
                indices = _read_integral_accessor(
                    gltf,
                    buffers,
                    primitive["indices"],
                    _UNSIGNED_INDEX_COMPONENT_TYPES,
                    "indices",
                    "SCALAR",
                ).astype(np.uint32)
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

            prim_name = f"{mesh_name}/primitive_{prim_idx}"
            if 0 <= material_index < len(gltf.get("materials", [])):
                material_name = gltf["materials"][material_index].get("name")
                if material_name:
                    prim_name = f"{mesh_name}/{material_name}"

            primitive_records.append({
                "vertices": vertices,
                "normals": normals,
                "uvs": uvs,
                "tangents": tangents,
                "joint_indices": joint_indices,
                "joint_weights": joint_weights,
                "indices": indices,
                "material_index": material_index,
                "material_slot": material_slot,
                "name": prim_name,
            })
            submeshes.append(GLBSubmeshData(
                name=prim_name,
                first_index=next_first_index,
                index_count=len(indices),
                material_index=material_index,
                material_slot=material_slot,
            ))
            next_first_index += len(indices)
            if trace is not None:
                log.info(
                    f"[GLBLoad] primitive-end mesh_index={mesh_idx} "
                    f"primitive_index={prim_idx} name='{prim_name}' "
                    f"vertices={len(vertices)} indices={len(indices)} "
                    f"duration_ms="
                    f"{(time.perf_counter() - primitive_started_at) * 1000.0:.3f} "
                    f"source='{trace.source}' thread={trace.thread_id}"
                )

        if not primitive_records:
            if trace is not None:
                log.info(
                    f"[GLBLoad] mesh-end mesh_index={mesh_idx} name='{mesh_name}' "
                    f"status=skipped duration_ms="
                    f"{(time.perf_counter() - mesh_started_at) * 1000.0:.3f} "
                    f"source='{trace.source}' thread={trace.thread_id}"
                )
            continue

        vertex_map: dict[tuple, int] = {}
        vertex_out: list[np.ndarray] = []
        normal_out: list[np.ndarray] = []
        uv_out: list[np.ndarray] = []
        tangent_out: list[np.ndarray] = []
        joint_out: list[np.ndarray] = []
        weight_out: list[np.ndarray] = []

        zero3 = (0.0, 0.0, 0.0)
        zero2 = (0.0, 0.0)
        zero4 = (0.0, 0.0, 0.0, 0.0)

        def row_tuple(data: Optional[np.ndarray], index: int, fallback: tuple[float, ...]) -> tuple[float, ...]:
            if data is None:
                return fallback
            return tuple(data[index].tolist())

        total_source_indices = sum(len(record["indices"]) for record in primitive_records)
        deduplicate_started_at = time.perf_counter()
        if trace is not None:
            log.info(
                f"[GLBLoad] deduplicate-begin mesh_index={mesh_idx} name='{mesh_name}' "
                f"source_indices={total_source_indices} source='{trace.source}' "
                f"thread={trace.thread_id}"
            )

        for record in primitive_records:
            local_indices: list[int] = []
            source_indices = record["indices"]
            for source_index in source_indices:
                idx = int(source_index)
                key_parts = [row_tuple(record["vertices"], idx, zero3)]
                if has_normals:
                    key_parts.append(row_tuple(record["normals"], idx, zero3))
                if has_uvs:
                    key_parts.append(row_tuple(record["uvs"], idx, zero2))
                if has_tangents:
                    key_parts.append(row_tuple(record["tangents"], idx, zero4))
                if has_joints:
                    key_parts.append(row_tuple(record["joint_indices"], idx, zero4))
                if has_weights:
                    key_parts.append(row_tuple(record["joint_weights"], idx, zero4))
                key = tuple(key_parts)

                vertex_index = vertex_map.get(key)
                if vertex_index is None:
                    vertex_index = len(vertex_out)
                    vertex_map[key] = vertex_index
                    vertex_out.append(record["vertices"][idx])
                    if has_normals:
                        normal_out.append(
                            record["normals"][idx] if record["normals"] is not None
                            else np.zeros(3, dtype=np.float32)
                        )
                    if has_uvs:
                        uv_out.append(
                            record["uvs"][idx] if record["uvs"] is not None
                            else np.zeros(2, dtype=np.float32)
                        )
                    if has_tangents:
                        tangent_out.append(
                            record["tangents"][idx] if record["tangents"] is not None
                            else np.zeros(4, dtype=np.float32)
                        )
                    if has_joints:
                        joint_out.append(
                            record["joint_indices"][idx] if record["joint_indices"] is not None
                            else np.zeros(4, dtype=np.float32)
                        )
                    if has_weights:
                        weight_out.append(
                            record["joint_weights"][idx] if record["joint_weights"] is not None
                            else np.zeros(4, dtype=np.float32)
                        )
                local_indices.append(vertex_index)

            index_chunks.append(np.asarray(local_indices, dtype=np.uint32))

        if trace is not None:
            log.info(
                f"[GLBLoad] deduplicate-end mesh_index={mesh_idx} name='{mesh_name}' "
                f"source_indices={total_source_indices} unique_vertices={len(vertex_out)} "
                f"duration_ms="
                f"{(time.perf_counter() - deduplicate_started_at) * 1000.0:.3f} "
                f"source='{trace.source}' thread={trace.thread_id}"
            )

        vertices = np.asarray(vertex_out, dtype=np.float32)
        normals = np.asarray(normal_out, dtype=np.float32) if has_normals else None
        uvs = np.asarray(uv_out, dtype=np.float32) if has_uvs else None
        tangents = np.asarray(tangent_out, dtype=np.float32) if has_tangents else None
        joint_indices = np.asarray(joint_out, dtype=np.uint32) if has_joints else None
        joint_weights = np.asarray(weight_out, dtype=np.float32) if has_weights else None
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
        if trace is not None:
            log.info(
                f"[GLBLoad] mesh-end mesh_index={mesh_idx} name='{mesh_name}' "
                f"vertices={len(vertices)} indices={len(indices)} "
                f"duration_ms={(time.perf_counter() - mesh_started_at) * 1000.0:.3f} "
                f"source='{trace.source}' thread={trace.thread_id}"
            )


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
        webp_ext = texture.get("extensions", {}).get("EXT_texture_webp")
        if webp_ext is not None and "source" in webp_ext:
            source_idx = webp_ext.get("source")
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
    matrix_animation_targets = {
        channel["target"]["node"]
        for animation in gltf.get("animations", [])
        for channel in animation.get("channels", [])
        if "target" in channel and "node" in channel["target"]
    }
    for node_idx, node in enumerate(gltf.get("nodes", [])):
        name = node.get("name", f"Node_{node_idx}")
        children = node.get("children", [])
        mesh_index = node.get("mesh")
        skin_index = node.get("skin")

        # Transform
        translation = np.array(node.get("translation", [0, 0, 0]), dtype=np.float32)
        rotation = np.array(node.get("rotation", [0, 0, 0, 1]), dtype=np.float32)  # xyzw
        scale = np.array(node.get("scale", [1, 1, 1]), dtype=np.float32)

        if "matrix" in node:
            if any(field in node for field in ("translation", "rotation", "scale")):
                raise ValueError(f"node {node_idx} must not define both matrix and TRS properties")
            if node_idx in matrix_animation_targets:
                raise ValueError(f"node {node_idx} uses matrix and must not be an animation target")
            translation, rotation, scale = _decompose_node_matrix(node["matrix"], node_idx)

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

def _build_scene_data(
    gltf: dict,
    buffers: list[bytes],
    base_path: Path | None = None,
    trace: _GLBLoadTrace | None = None,
) -> GLBSceneData:
    """Parse glTF JSON and buffers into scene data."""
    scene_data = GLBSceneData()

    if trace is None:
        _parse_nodes(gltf, scene_data)
        _parse_materials(gltf, scene_data)
        _parse_textures(gltf, buffers, scene_data, base_path)
        _parse_meshes(gltf, buffers, scene_data)
        _parse_skins(gltf, buffers, scene_data)
        _parse_animations(gltf, buffers, scene_data)
        return scene_data

    with trace.stage("nodes", count=len(gltf.get("nodes", []))):
        _parse_nodes(gltf, scene_data)
    with trace.stage("materials", count=len(gltf.get("materials", []))):
        _parse_materials(gltf, scene_data)
    with trace.stage("textures", count=len(gltf.get("textures", []))):
        _parse_textures(gltf, buffers, scene_data, base_path)
    with trace.stage("meshes", count=len(gltf.get("meshes", []))):
        _parse_meshes(gltf, buffers, scene_data, trace)
    with trace.stage("skins", count=len(gltf.get("skins", []))):
        _parse_skins(gltf, buffers, scene_data)
    with trace.stage("animations", count=len(gltf.get("animations", []))):
        _parse_animations(gltf, buffers, scene_data)
    return scene_data


def load_glb_file(
    path: str | Path,
    *,
    _trace: _GLBLoadTrace | None = None,
) -> GLBSceneData:
    """Load a GLB or JSON glTF file and return scene data.

    Args:
        path: Path to .glb or .gltf file

    Returns:
        GLBSceneData containing meshes, materials, textures, animations, nodes
    """
    path = Path(path)
    trace = _trace or _GLBLoadTrace(str(path))
    owns_trace = _trace is None
    if owns_trace:
        trace.begin()

    with trace.stage("read-file"):
        with open(path, "rb") as f:
            data = f.read()

    # Parse header
    if len(data) < 12:
        raise ValueError("File too small to be valid GLB")

    magic = data[0:4]
    if magic != b"glTF":
        if path.suffix.lower() != ".gltf":
            raise ValueError(f"Invalid GLB magic: {magic}")
        with trace.stage("parse-json", bytes=len(data)):
            gltf = json.loads(data.decode("utf-8"))
        with trace.stage("read-external-buffers", count=len(gltf.get("buffers", []))):
            buffers = _load_gltf_buffers(gltf, path.parent)
        scene_data = _build_scene_data(gltf, buffers, path.parent, trace)
        if owns_trace:
            trace.complete(scene_data)
        return scene_data

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

    with trace.stage("parse-json", bytes=len(json_data)):
        gltf = json.loads(json_data)
    buffers = [bin_data or b""]

    scene_data = _build_scene_data(gltf, buffers, path.parent, trace)
    if owns_trace:
        trace.complete(scene_data)
    return scene_data


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

    # 5. Keep animation channels in the same local space as normalized nodes.
    for anim in scene_data.animations:
        for channel in anim.channels:
            if channel.node_index == root_idx:
                for key in channel.scale_keys:
                    key[1] = key[1] / root_scale
                continue
            for key in channel.pos_keys:
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

    root_node_indices = {idx for idx in scene_data.root_nodes if idx < len(scene_data.nodes)}

    # Rotate root nodes of scene by -90° X
    for root_idx in scene_data.root_nodes:
        if root_idx < len(scene_data.nodes):
            root_node = scene_data.nodes[root_idx]
            root_node.rotation = _qmul(rot_neg_90_x, root_node.rotation)

    # Animation channels are absolute local TRS tracks. Any correction applied
    # to a node rest pose must also be applied to that node's tracks; otherwise
    # enabling animation snaps the imported root back to raw glTF space.
    for anim in scene_data.animations:
        for channel in anim.channels:
            if channel.node_index not in root_node_indices:
                continue
            for key in channel.rot_keys:
                key[1] = _qmul(rot_neg_90_x, key[1])

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
    trace = _GLBLoadTrace(str(path))
    trace.begin()
    scene_data = load_glb_file(path, _trace=trace)

    # Coordinate conversion first (before scale normalization)
    if convert_to_z_up:
        with trace.stage("convert-y-up-to-z-up"):
            convert_y_up_to_z_up(scene_data)

    # Blender-specific fix (after coordinate conversion)
    if blender_z_up_fix:
        with trace.stage("apply-blender-z-up-fix"):
            apply_blender_z_up_fix(scene_data)

    if normalize_scale:
        with trace.stage("normalize-scale"):
            normalize_glb_scale(scene_data)

    trace.complete(scene_data)
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
    trace = _GLBLoadTrace("<memory>")
    trace.begin()

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

    with trace.stage("parse-json", bytes=len(json_data)):
        gltf = json.loads(json_data)
    buffers = [bin_data or b""]

    scene_data = _build_scene_data(gltf, buffers, trace=trace)

    # Coordinate conversion first (before scale normalization)
    if convert_to_z_up:
        with trace.stage("convert-y-up-to-z-up"):
            convert_y_up_to_z_up(scene_data)

    # Blender-specific fix (after coordinate conversion)
    if blender_z_up_fix:
        with trace.stage("apply-blender-z-up-fix"):
            apply_blender_z_up_fix(scene_data)

    if normalize_scale:
        with trace.stage("normalize-scale"):
            normalize_glb_scale(scene_data)

    trace.complete(scene_data)
    return scene_data
