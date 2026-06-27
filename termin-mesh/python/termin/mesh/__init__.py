"""Core mesh API and asset integration."""

from __future__ import annotations

from pkgutil import extend_path

from tmesh import Mesh3, TcMesh

__path__ = extend_path(__path__, __name__)


def __getattr__(name: str):
    if name in (
        "MeshComponent",
        "ProceduralMeshComponent",
        "ScriptMeshComponent",
        "SurfaceEdgeHit",
        "find_surface_edge_for_entity",
        "find_aligned_surface_edge_for_entity",
    ):
        from termin.mesh.mesh_component import MeshComponent
        from termin.mesh.procedural_mesh_component import ProceduralMeshComponent
        from termin.mesh.script_mesh_component import ScriptMeshComponent
        from termin.mesh.surface_edge_query import (
            SurfaceEdgeHit,
            find_aligned_surface_edge_for_entity,
            find_surface_edge_for_entity,
        )

        exports = {
            "MeshComponent": MeshComponent,
            "ProceduralMeshComponent": ProceduralMeshComponent,
            "ScriptMeshComponent": ScriptMeshComponent,
            "SurfaceEdgeHit": SurfaceEdgeHit,
            "find_surface_edge_for_entity": find_surface_edge_for_entity,
            "find_aligned_surface_edge_for_entity": find_aligned_surface_edge_for_entity,
        }
        globals().update(exports)
        return exports[name]

    raise AttributeError(f"module 'termin.mesh' has no attribute {name!r}")


__all__ = [
    "Mesh3",
    "TcMesh",
    "MeshComponent",
    "ProceduralMeshComponent",
    "ScriptMeshComponent",
    "SurfaceEdgeHit",
    "find_surface_edge_for_entity",
    "find_aligned_surface_edge_for_entity",
]
