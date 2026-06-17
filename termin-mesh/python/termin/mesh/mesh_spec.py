# termin/loaders/mesh_spec.py
"""Mesh import specification - settings for loading mesh files."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Tuple

import numpy as np

from tcbase import log

DEFAULT_AXIS_X = "x"
DEFAULT_AXIS_Y = "z"
DEFAULT_AXIS_Z = "y"


@dataclass
class MeshSpec:
    """
    Import settings for mesh files.

    Stored as .meta file next to the mesh (e.g., model.stl.meta).
    """

    # Scale factor applied to all vertices
    scale: float = 1.0

    # Axis mapping: which source axis maps to engine X, Y, Z.
    # Defaults convert common Unity-style assets (X right, Y up, Z forward)
    # to Termin coordinates (X right, Y forward, Z up).
    # Values: "x", "y", "z", "-x", "-y", "-z"
    axis_x: str = DEFAULT_AXIS_X
    axis_y: str = DEFAULT_AXIS_Y
    axis_z: str = DEFAULT_AXIS_Z

    # UV transformations
    flip_uv_v: bool = False  # Flip V coordinate (v = 1 - v)

    @classmethod
    def load(cls, spec_path: str | Path) -> "MeshSpec":
        """Load spec from file."""
        path = Path(spec_path)
        if not path.exists():
            return cls()

        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            return cls(
                scale=data.get("scale", 1.0),
                axis_x=data.get("axis_x", DEFAULT_AXIS_X),
                axis_y=data.get("axis_y", DEFAULT_AXIS_Y),
                axis_z=data.get("axis_z", DEFAULT_AXIS_Z),
                flip_uv_v=data.get("flip_uv_v", False),
            )
        except Exception:
            log.warning(f"[MeshSpec] Failed to load spec from {spec_path}", exc_info=True)
            return cls()

    @classmethod
    def for_mesh_file(cls, mesh_path: str | Path) -> "MeshSpec":
        """Load spec for a mesh file (looks for mesh_path.meta or .spec)."""
        # Try .meta first (new format)
        meta_path = Path(str(mesh_path) + ".meta")
        if meta_path.exists():
            return cls.load(meta_path)
        # Fallback to .spec (old format)
        spec_path = Path(str(mesh_path) + ".spec")
        return cls.load(spec_path)

    def save(self, spec_path: str | Path, preserve_existing: bool = False) -> None:
        """Save spec to file.

        Args:
            spec_path: Path to save the spec file.
            preserve_existing: If True, preserve fields from existing file (like uuid).
        """
        path = Path(spec_path)

        # Read existing data to preserve fields like uuid
        existing_data = {}
        if preserve_existing and path.exists():
            try:
                with open(path, "r", encoding="utf-8") as f:
                    existing_data = json.load(f)
            except Exception:
                log.warning(f"[MeshSpec] Failed to read existing spec {path}", exc_info=True)

        # Update with our fields
        data = existing_data.copy()
        data.update({
            "scale": self.scale,
            "axis_x": self.axis_x,
            "axis_y": self.axis_y,
            "axis_z": self.axis_z,
            "flip_uv_v": self.flip_uv_v,
        })
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

    def save_for_mesh(self, mesh_path: str | Path) -> None:
        """Save spec next to mesh file (.meta format).

        Preserves existing fields like uuid from the .meta file.
        """
        import os
        meta_path = Path(str(mesh_path) + ".meta")
        self.save(meta_path, preserve_existing=True)
        # Remove old .spec if exists (migration)
        old_spec = Path(str(mesh_path) + ".spec")
        if old_spec.exists():
            try:
                os.remove(old_spec)
            except Exception:
                log.warning(f"[MeshSpec] Failed to remove old spec {old_spec}", exc_info=True)

    def apply_to_vertices(self, vertices: np.ndarray) -> np.ndarray:
        """
        Apply spec transformations to vertices.

        Args:
            vertices: (N, 3) array of vertices

        Returns:
            Transformed vertices (N, 3)
        """
        if vertices is None or len(vertices) == 0:
            return vertices

        result = np.zeros_like(vertices)

        # Apply axis mapping
        axis_map = {"x": 0, "y": 1, "z": 2, "-x": 0, "-y": 1, "-z": 2}
        sign_map = {"x": 1, "y": 1, "z": 1, "-x": -1, "-y": -1, "-z": -1}

        src_x = axis_map.get(self.axis_x, 0)
        src_y = axis_map.get(self.axis_y, 1)
        src_z = axis_map.get(self.axis_z, 2)

        sign_x = sign_map.get(self.axis_x, 1)
        sign_y = sign_map.get(self.axis_y, 1)
        sign_z = sign_map.get(self.axis_z, 1)

        result[:, 0] = vertices[:, src_x] * sign_x
        result[:, 1] = vertices[:, src_y] * sign_y
        result[:, 2] = vertices[:, src_z] * sign_z

        # Apply scale
        result *= self.scale

        return result.astype(np.float32)

    def apply_to_normals(self, normals: np.ndarray) -> np.ndarray:
        """
        Apply axis reordering to normals (no scale).

        Args:
            normals: (N, 3) array of normals

        Returns:
            Transformed normals (N, 3)
        """
        if normals is None or len(normals) == 0:
            return normals

        result = np.zeros_like(normals)

        axis_map = {"x": 0, "y": 1, "z": 2, "-x": 0, "-y": 1, "-z": 2}
        sign_map = {"x": 1, "y": 1, "z": 1, "-x": -1, "-y": -1, "-z": -1}

        src_x = axis_map.get(self.axis_x, 0)
        src_y = axis_map.get(self.axis_y, 1)
        src_z = axis_map.get(self.axis_z, 2)

        sign_x = sign_map.get(self.axis_x, 1)
        sign_y = sign_map.get(self.axis_y, 1)
        sign_z = sign_map.get(self.axis_z, 1)

        result[:, 0] = normals[:, src_x] * sign_x
        result[:, 1] = normals[:, src_y] * sign_y
        result[:, 2] = normals[:, src_z] * sign_z

        return result.astype(np.float32)

    def apply_to_uvs(self, uvs: np.ndarray) -> np.ndarray:
        """
        Apply UV transformations.

        Args:
            uvs: (N, 2) array of UV coordinates

        Returns:
            Transformed UVs (N, 2)
        """
        if uvs is None or len(uvs) == 0:
            return uvs

        result = uvs.copy()

        if self.flip_uv_v:
            result[:, 1] = 1.0 - result[:, 1]

        return result.astype(np.float32)
