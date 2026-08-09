import os
from pathlib import Path

import pytest

from termin.glb import NativeStaticMeshDocument


_GEOMETRY_ONLY = Path(
    os.environ.get(
        "TERMIN_GLB_GEOMETRY_REFERENCE",
        "/home/mirmik/project/Hunyuan3D-Omni-test/"
        "pixal3d_shape_stages_experiment/front_seed42_1536/shape_1536.glb",
    )
)
_TEXTURED = Path(
    os.environ.get(
        "TERMIN_GLB_TEXTURED_REFERENCE",
        "/home/mirmik/project/Hunyuan3D-Omni-test/"
        "pixal3d_full_1536_experiment/character_full_1536.glb",
    )
)
_FNV_OFFSET = 14695981039346656037
_FNV_PRIME = 1099511628211


def _require_reference(path: Path) -> None:
    if not path.is_file():
        pytest.skip(f"local GLB reference fixture is unavailable: {path}")


def test_native_geometry_only_pixal3d_reference():
    _require_reference(_GEOMETRY_ONLY)
    document = NativeStaticMeshDocument(_GEOMETRY_ONLY)

    assert sum(mesh.vertex_count for mesh in document.meshes) == 2_823_922
    assert sum(mesh.index_count for mesh in document.meshes) == 17_280_924
    assert len(document.meshes) == 1

    mesh, diagnostics = document.build_mesh_with_diagnostics(
        0,
        "reference-pixal3d-geometry-only",
        convert_to_z_up=False,
    )
    assert mesh.stride == 12
    assert diagnostics.payload_bytes == 103_010_760
    assert diagnostics.payload_hash == 5_462_061_671_071_880_564


def test_native_textured_pixal3d_geometry_reference():
    _require_reference(_TEXTURED)
    document = NativeStaticMeshDocument(_TEXTURED)

    assert sum(mesh.vertex_count for mesh in document.meshes) == 753_518
    assert sum(mesh.index_count for mesh in document.meshes) == 2_901_741

    geometry_hash = _FNV_OFFSET
    payload_bytes = 0
    for mesh_index, mesh_info in enumerate(document.meshes):
        mesh, diagnostics = document.build_mesh_with_diagnostics(
            mesh_index,
            f"reference-pixal3d-textured-{mesh_index}",
            name=mesh_info.name,
            convert_to_z_up=False,
        )
        assert mesh.stride == 32
        payload_bytes += diagnostics.payload_bytes
        geometry_hash = diagnostics.payload_hash ^ (geometry_hash * _FNV_PRIME & 0xFFFFFFFFFFFFFFFF)

    assert payload_bytes == 35_719_540
    assert geometry_hash == 10_374_265_412_880_042_524
