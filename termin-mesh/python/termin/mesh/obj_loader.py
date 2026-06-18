"""Compatibility re-export for OBJ mesh importer helpers.

Canonical module: :mod:`termin.default_assets.mesh.obj_loader`.
"""

from termin.default_assets.mesh.obj_loader import (
    OBJMeshData,
    OBJSceneData,
    load_obj_file,
    parse_obj_text,
)

__all__ = ["OBJMeshData", "OBJSceneData", "load_obj_file", "parse_obj_text"]
