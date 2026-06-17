# termin/loaders/stl_loader.py
"""Pure Python STL loader (binary and ASCII). No external dependencies."""

from __future__ import annotations

import struct
from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

from tcbase import log

if TYPE_CHECKING:
    from termin.mesh.mesh_spec import MeshSpec


class STLMeshData:
    def __init__(self, name, vertices, normals, indices):
        self.name = name
        self.vertices = vertices  # np.ndarray (N, 3) float32
        self.normals = normals    # np.ndarray (N, 3) float32 or None
        self.indices = indices    # np.ndarray (M,) uint32
        self.uvs = None           # STL format has no UV coordinates


class STLSceneData:
    def __init__(self):
        self.meshes = []


def load_stl_file(path, spec: "MeshSpec | None" = None) -> STLSceneData:
    """Load STL file (binary or ASCII), applying spec if provided."""
    path = Path(path)
    scene_data = STLSceneData()

    with open(path, "rb") as f:
        first_bytes = f.read(80)
        f.seek(0)

        # ASCII STL starts with "solid" and typically has no nulls in first line
        is_ascii = (
            first_bytes.strip().lower().startswith(b"solid")
            and b"\x00" not in first_bytes
        )

        if is_ascii:
            try:
                mesh = _load_ascii_stl(f, path.stem)
            except Exception as e:
                log.debug(f"[STL] ASCII parse failed, falling back to binary: {e}")
                # Fallback to binary
                f.seek(0)
                mesh = _load_binary_stl(f, path.stem)
        else:
            mesh = _load_binary_stl(f, path.stem)

    # Apply spec transformations
    if spec is not None:
        mesh.vertices = spec.apply_to_vertices(mesh.vertices)
        if mesh.normals is not None:
            mesh.normals = spec.apply_to_normals(mesh.normals)

    scene_data.meshes.append(mesh)
    return scene_data


def _load_binary_stl(f, name: str) -> STLMeshData:
    """Load binary STL format."""
    # Skip 80-byte header
    f.seek(80)

    # Read triangle count
    num_triangles = struct.unpack("<I", f.read(4))[0]

    # Pre-allocate lists
    vertices = []
    normals = []

    # Read triangles
    for _ in range(num_triangles):
        # Normal (3 floats) + 3 vertices (9 floats) + attribute (2 bytes)
        data = f.read(50)
        values = struct.unpack("<12fH", data)

        nx, ny, nz = values[0:3]
        v1 = values[3:6]
        v2 = values[6:9]
        v3 = values[9:12]

        vertices.extend([v1, v2, v3])
        normals.extend([(nx, ny, nz)] * 3)

    # Convert to numpy
    vertices_np = np.array(vertices, dtype=np.float32)
    normals_np = np.array(normals, dtype=np.float32)
    indices_np = np.arange(len(vertices), dtype=np.uint32)

    return STLMeshData(
        name=name,
        vertices=vertices_np,
        normals=normals_np,
        indices=indices_np,
    )


def _load_ascii_stl(f, name: str) -> STLMeshData:
    """Load ASCII STL format."""
    vertices = []
    normals = []
    current_normal = None

    for line in f:
        line = line.decode("utf-8", errors="ignore").strip()
        lower = line.lower()

        if lower.startswith("facet normal"):
            parts = line.split()
            if len(parts) >= 5:
                current_normal = (float(parts[2]), float(parts[3]), float(parts[4]))

        elif lower.startswith("vertex"):
            parts = line.split()
            if len(parts) >= 4:
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
                if current_normal:
                    normals.append(current_normal)

    if not vertices:
        raise ValueError("No vertices found in ASCII STL")

    vertices_np = np.array(vertices, dtype=np.float32)
    normals_np = np.array(normals, dtype=np.float32) if normals else None
    indices_np = np.arange(len(vertices), dtype=np.uint32)

    return STLMeshData(
        name=name,
        vertices=vertices_np,
        normals=normals_np,
        indices=indices_np,
    )
