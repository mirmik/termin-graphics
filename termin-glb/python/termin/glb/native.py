"""Explicit native cgltf static-mesh import boundary.

This module intentionally does not fall back to the Python loader. The caller
first discovers mesh identities, resolves embedded asset UUIDs, and only then
publishes selected meshes into the native registry.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from termin.glb import _glb_native


@dataclass(frozen=True)
class NativeMeshInfo:
    name: str
    primitive_count: int
    vertex_count: int
    index_count: int


class NativeStaticMeshDocument:
    """Mapped GLB document with compact discovery and transactional mesh build."""

    def __init__(self, path: Path | str):
        self._path = Path(path)
        self._document = _glb_native.NativeDocument(str(self._path))
        self._meshes = tuple(
            NativeMeshInfo(
                name=entry["name"],
                primitive_count=entry["primitive_count"],
                vertex_count=entry["vertex_count"],
                index_count=entry["index_count"],
            )
            for entry in self._document.meshes
        )

    @property
    def path(self) -> Path:
        return self._path

    @property
    def meshes(self) -> tuple[NativeMeshInfo, ...]:
        return self._meshes

    def build_mesh(
        self,
        mesh_index: int,
        mesh_uuid: str,
        *,
        name: str = "",
        convert_to_z_up: bool = True,
    ):
        """Build one discovered mesh and return its existing native handle."""
        self._document.build_static_mesh(mesh_index, mesh_uuid, name, convert_to_z_up)

        from tmesh import tc_mesh_get

        mesh = tc_mesh_get(mesh_uuid)
        if mesh is None or not mesh.is_valid:
            raise RuntimeError(
                f"Native GLB build published no tc_mesh for UUID '{mesh_uuid}'"
            )
        return mesh
