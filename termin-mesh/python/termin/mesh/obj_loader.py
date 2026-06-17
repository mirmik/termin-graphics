# termin/loaders/obj_loader.py
"""Pure Python OBJ loader. No external dependencies."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from termin.mesh.mesh_spec import MeshSpec


class OBJMeshData:
    def __init__(self, name, vertices, normals, uvs, indices):
        self.name = name
        self.vertices = vertices  # np.ndarray (N, 3) float32
        self.normals = normals    # np.ndarray (N, 3) float32 or None
        self.uvs = uvs            # np.ndarray (N, 2) float32 or None
        self.indices = indices    # np.ndarray (M,) uint32


class OBJSceneData:
    def __init__(self):
        self.meshes = []


def load_obj_file(path, spec: "MeshSpec | None" = None) -> OBJSceneData:
    """Load OBJ file."""
    path = Path(path)

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()

    return parse_obj_text(text, name=path.stem, spec=spec)


def parse_obj_text(text: str, name: str = "mesh", spec: "MeshSpec | None" = None) -> OBJSceneData:
    """Parse OBJ from text content."""
    scene_data = OBJSceneData()

    # Raw data from file
    positions = []  # v
    tex_coords = []  # vt
    normals_raw = []  # vn
    faces = []  # f

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        parts = line.split()
        if not parts:
            continue

        cmd = parts[0]

        if cmd == "v" and len(parts) >= 4:
            positions.append((float(parts[1]), float(parts[2]), float(parts[3])))

        elif cmd == "vt" and len(parts) >= 3:
            tex_coords.append((float(parts[1]), float(parts[2])))

        elif cmd == "vn" and len(parts) >= 4:
            normals_raw.append((float(parts[1]), float(parts[2]), float(parts[3])))

        elif cmd == "f" and len(parts) >= 4:
            # Parse face vertices (can be triangles or quads)
            face_verts = []
            for vert in parts[1:]:
                # Format: v, v/vt, v/vt/vn, v//vn
                indices_str = vert.split("/")
                v_idx = int(indices_str[0]) - 1  # OBJ is 1-indexed

                vt_idx = None
                if len(indices_str) > 1 and indices_str[1]:
                    vt_idx = int(indices_str[1]) - 1

                vn_idx = None
                if len(indices_str) > 2 and indices_str[2]:
                    vn_idx = int(indices_str[2]) - 1

                face_verts.append((v_idx, vt_idx, vn_idx))

            # Triangulate (fan triangulation for convex polygons)
            for i in range(1, len(face_verts) - 1):
                faces.append(face_verts[0])
                faces.append(face_verts[i])
                faces.append(face_verts[i + 1])

    if not positions:
        scene_data.meshes.append(OBJMeshData(
            name=name,
            vertices=np.array([], dtype=np.float32).reshape(0, 3),
            normals=None,
            uvs=None,
            indices=np.array([], dtype=np.uint32),
        ))
        return scene_data

    # Build final vertex arrays (expand indexed data)
    out_vertices = []
    out_normals = []
    out_uvs = []

    has_normals = bool(normals_raw)
    has_uvs = bool(tex_coords)

    for v_idx, vt_idx, vn_idx in faces:
        out_vertices.append(positions[v_idx])

        if has_normals and vn_idx is not None:
            out_normals.append(normals_raw[vn_idx])

        if has_uvs and vt_idx is not None:
            out_uvs.append(tex_coords[vt_idx])

    vertices_np = np.array(out_vertices, dtype=np.float32)
    normals_np = np.array(out_normals, dtype=np.float32) if out_normals else None
    uvs_np = np.array(out_uvs, dtype=np.float32) if out_uvs else None
    indices_np = np.arange(len(out_vertices), dtype=np.uint32)

    mesh = OBJMeshData(
        name=name,
        vertices=vertices_np,
        normals=normals_np,
        uvs=uvs_np,
        indices=indices_np,
    )

    # Apply spec transformations
    if spec is not None:
        mesh.vertices = spec.apply_to_vertices(mesh.vertices)
        if mesh.normals is not None:
            mesh.normals = spec.apply_to_normals(mesh.normals)
        if mesh.uvs is not None:
            mesh.uvs = spec.apply_to_uvs(mesh.uvs)

    scene_data.meshes.append(mesh)
    return scene_data
