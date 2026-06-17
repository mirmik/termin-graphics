"""MeshAsset - Asset for 3D mesh data with GPU rendering support."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from tcbase import log
from tmesh import Mesh3, TcMesh
from termin_assets import DataAsset
from termin.mesh.mesh_spec import DEFAULT_AXIS_X, DEFAULT_AXIS_Y, DEFAULT_AXIS_Z

if TYPE_CHECKING:
    from termin.mesh.mesh_spec import MeshSpec


class MeshAsset(DataAsset[TcMesh]):
    """
    Asset for 3D mesh geometry.

    IMPORTANT: Create through ResourceManager.get_or_create_mesh_asset(),
    not directly. This ensures proper registration and avoids duplicates.

    Stores TcMesh (handle to tc_mesh in C registry).
    GPU upload/draw via TcMesh.draw_gpu().

    Can be loaded from:
    - STL/OBJ files (standalone)
    - GLB files (embedded, via parent asset)
    """

    _uses_binary = True  # STL/OBJ are read as binary

    def __init__(
        self,
        mesh_data: TcMesh | None = None,
        name: str = "mesh",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        super().__init__(data=mesh_data, name=name, source_path=source_path, uuid=uuid)

        # Spec settings (parsed from spec file)
        self._scale: float = 1.0
        self._axis_x: str = DEFAULT_AXIS_X
        self._axis_y: str = DEFAULT_AXIS_Y
        self._axis_z: str = DEFAULT_AXIS_Z
        self._flip_uv_v: bool = False

    # --- Convenience property ---

    @property
    def mesh_data(self) -> TcMesh | None:
        """Mesh geometry data (lazy-loaded via parent's data property)."""
        return self.data

    @mesh_data.setter
    def mesh_data(self, value: TcMesh | None) -> None:
        """Set mesh data and bump version."""
        self.data = value

    # --- Spec parsing ---

    def _parse_spec_fields(self, spec_data: dict) -> None:
        """Parse mesh-specific spec fields."""
        self._scale = spec_data.get("scale", 1.0)
        self._axis_x = spec_data.get("axis_x", DEFAULT_AXIS_X)
        self._axis_y = spec_data.get("axis_y", DEFAULT_AXIS_Y)
        self._axis_z = spec_data.get("axis_z", DEFAULT_AXIS_Z)
        self._flip_uv_v = spec_data.get("flip_uv_v", False)

        # Pre-register TcMesh in registry with load callback for lazy loading
        # Only for standalone meshes (not embedded in GLB)
        if self._source_path is not None and self._parent_asset is None:
            self._declare_tc_mesh()

    def _build_spec_data(self) -> dict:
        """Build spec data with mesh settings."""
        spec = super()._build_spec_data()
        # Only save non-default values
        if self._scale != 1.0:
            spec["scale"] = self._scale
        if self._axis_x != DEFAULT_AXIS_X:
            spec["axis_x"] = self._axis_x
        if self._axis_y != DEFAULT_AXIS_Y:
            spec["axis_y"] = self._axis_y
        if self._axis_z != DEFAULT_AXIS_Z:
            spec["axis_z"] = self._axis_z
        if self._flip_uv_v:
            spec["flip_uv_v"] = True
        return spec

    def _get_mesh_spec(self) -> "MeshSpec":
        """Create MeshSpec from current settings."""
        from termin.mesh.mesh_spec import MeshSpec

        return MeshSpec(
            scale=self._scale,
            axis_x=self._axis_x,
            axis_y=self._axis_y,
            axis_z=self._axis_z,
            flip_uv_v=self._flip_uv_v,
        )

    # --- Lazy loading with tc_mesh registry ---

    def _declare_tc_mesh(self) -> None:
        """Declare TcMesh in registry with load callback for lazy loading."""
        from tmesh import (
            tc_mesh_declare,
            tc_mesh_set_load_callback,
            tc_mesh_is_loaded,
        )

        # Declare empty mesh entry in registry
        tc_mesh = tc_mesh_declare(self._uuid, self._name)
        if tc_mesh.is_valid and not tc_mesh_is_loaded(tc_mesh):
            # Set load callback that triggers asset loading
            tc_mesh_set_load_callback(tc_mesh, lambda m: self._load_from_file())
            # Store handle in _data (but not loaded yet)
            self._data = tc_mesh
            self._loaded = False

    # --- Content parsing ---

    def _parse_content(self, content: bytes) -> TcMesh | None:
        """Parse mesh content (STL or OBJ based on file extension)."""
        if self._source_path is None:
            return None

        import os

        ext = os.path.splitext(str(self._source_path))[1].lower()
        spec = self._get_mesh_spec()

        if ext == ".stl":
            mesh3 = self._parse_stl_to_mesh3(content, spec)
        elif ext == ".obj":
            mesh3 = self._parse_obj_to_mesh3(content, spec)
        else:
            log.warn(f"[MeshAsset] Unsupported format: {ext}")
            return None

        if mesh3 is None:
            return None

        return self._populate_or_create_tc_mesh(mesh3)

    def _populate_or_create_tc_mesh(self, mesh3: Mesh3) -> TcMesh | None:
        """Populate existing declared TcMesh or create new one."""
        from tmesh import TcMesh

        tc_mesh = self._data
        if (tc_mesh is None or not tc_mesh.is_valid) and self._uuid:
            tc_mesh = TcMesh.from_uuid(self._uuid)

        if tc_mesh is not None and tc_mesh.is_valid:
            if tc_mesh.set_from_mesh3(mesh3):
                return tc_mesh
            log.error(f"[MeshAsset] Failed to update TcMesh: {self._name}")
            return None

        return TcMesh.from_mesh3(mesh3, self._name, self._uuid)

    def _parse_stl_to_mesh3(self, content: bytes, spec: "MeshSpec") -> Mesh3 | None:
        """Parse STL content to Mesh3."""
        import io

        from termin.mesh.stl_loader import _load_binary_stl, _load_ascii_stl

        f = io.BytesIO(content)

        # Detect ASCII vs binary
        first_bytes = content[:80]
        is_ascii = (
            first_bytes.strip().lower().startswith(b"solid")
            and b"\x00" not in first_bytes
        )

        if is_ascii:
            try:
                mesh_data = _load_ascii_stl(f, self._name)
            except Exception as e:
                log.debug(f"[MeshAsset] ASCII STL load failed for {self._name}, trying binary: {e}")
                f.seek(0)
                mesh_data = _load_binary_stl(f, self._name)
        else:
            mesh_data = _load_binary_stl(f, self._name)

        # Apply spec transformations
        mesh_data.vertices = spec.apply_to_vertices(mesh_data.vertices)
        if mesh_data.normals is not None:
            mesh_data.normals = spec.apply_to_normals(mesh_data.normals)

        mesh3 = Mesh3(
            name=self._name,
            vertices=mesh_data.vertices,
            triangles=mesh_data.indices.reshape(-1, 3),
            vertex_normals=mesh_data.normals,
        )
        if not mesh3.has_normals():
            mesh3.compute_normals()
        return mesh3

    def _parse_obj_to_mesh3(self, content: bytes, spec: "MeshSpec") -> Mesh3 | None:
        """Parse OBJ content to Mesh3."""
        from termin.mesh.obj_loader import parse_obj_text

        text = content.decode("utf-8", errors="ignore")

        scene_data = parse_obj_text(text, name=self._name, spec=spec)
        if not scene_data.meshes:
            return None

        mesh_data = scene_data.meshes[0]
        mesh3 = Mesh3(
            name=self._name,
            vertices=mesh_data.vertices,
            triangles=mesh_data.indices.reshape(-1, 3),
            vertex_normals=mesh_data.normals,
            uvs=mesh_data.uvs,
        )
        if not mesh3.has_normals():
            mesh3.compute_normals()
        return mesh3

    # --- Convenience methods for mesh manipulation ---

    def get_vertex_count(self) -> int:
        """Get number of vertices."""
        data = self.data
        if data is None or not data.is_valid:
            return 0
        return data.vertex_count

    def get_triangle_count(self) -> int:
        """Get number of triangles."""
        data = self.data
        if data is None or not data.is_valid:
            return 0
        return data.triangle_count

    def interleaved_buffer(self):
        """Get interleaved vertex buffer for GPU upload."""
        data = self.data
        if data is None or not data.is_valid:
            return None
        return data.get_vertices_buffer()

    def get_vertex_layout(self):
        """Get vertex layout for shader binding."""
        from enum import Enum

        class VertexAttribType(Enum):
            FLOAT32 = "float32"

        class VertexAttribute:
            def __init__(self, name, size, vtype: VertexAttribType, offset):
                self.name = name
                self.size = size
                self.vtype = vtype
                self.offset = offset

        class VertexLayout:
            def __init__(self, stride, attributes):
                self.stride = stride
                self.attributes = attributes

        # TcMesh uses pos(3) + normal(3) + uv(2) = 8 floats = 32 bytes
        return VertexLayout(
            stride=32,
            attributes=[
                VertexAttribute("position", 3, VertexAttribType.FLOAT32, 0),
                VertexAttribute("normal", 3, VertexAttribType.FLOAT32, 12),
                VertexAttribute("uv", 2, VertexAttribType.FLOAT32, 24),
            ]
        )

    # --- Factory methods ---

    @classmethod
    def from_mesh3(
        cls,
        mesh3: Mesh3,
        name: str = "mesh",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ) -> "MeshAsset":
        """Create MeshAsset from existing Mesh3 (CPU mesh)."""
        tc_mesh = TcMesh.from_mesh3(mesh3, name, uuid or "")
        return cls(mesh_data=tc_mesh, name=name, source_path=source_path)

    @classmethod
    def from_vertices_triangles(
        cls,
        vertices,
        triangles,
        name: str = "mesh",
    ) -> "MeshAsset":
        """Create MeshAsset from vertices and triangles arrays."""
        mesh3 = Mesh3(name=name, vertices=vertices, triangles=triangles)
        tc_mesh = TcMesh.from_mesh3(mesh3, name)
        return cls(mesh_data=tc_mesh, name=name)

    # --- GPU access ---

    def draw_gpu(self) -> None:
        """Draw mesh (upload to GPU if needed)."""
        tc_mesh = self.data
        if tc_mesh is not None and tc_mesh.is_valid:
            tc_mesh.draw_gpu()

    def delete_gpu(self) -> None:
        """Delete GPU resources (no-op, handled by tc_mesh)."""
        pass
