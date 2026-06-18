"""Compatibility re-export for STL mesh importer helpers.

Canonical module: :mod:`termin.default_assets.mesh.stl_loader`.
"""

from termin.default_assets.mesh.stl_loader import (
    STLMeshData,
    STLSceneData,
    _load_ascii_stl,
    _load_binary_stl,
    load_stl_file,
)

__all__ = [
    "STLMeshData",
    "STLSceneData",
    "_load_ascii_stl",
    "_load_binary_stl",
    "load_stl_file",
]
