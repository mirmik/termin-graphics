from termin.default_assets.mesh.asset import MeshAsset
from termin.default_assets.mesh.asset_plugin import (
    create_import_plugin,
    create_runtime_plugin,
)
from termin.default_assets.mesh.mesh_spec import MeshSpec
from termin.mesh import MeshAsset as LazyMeshAsset
from termin.mesh.asset import MeshAsset as DomainAssetMeshAsset
from termin.mesh.asset_plugin import create_import_plugin as legacy_create_import_plugin
from termin.mesh.asset_plugin import create_runtime_plugin as legacy_create_runtime_plugin
from termin.mesh.mesh_asset import MeshAsset as LegacyDomainMeshAsset
from termin.mesh.mesh_spec import MeshSpec as DomainMeshSpec
from termin.mesh.obj_loader import OBJMeshData, parse_obj_text
from termin.mesh.stl_loader import STLMeshData
from termin.assets.mesh_asset import MeshAsset as AppLegacyMeshAsset


def test_mesh_asset_legacy_modules_reexport_canonical_class() -> None:
    assert LazyMeshAsset is MeshAsset
    assert DomainAssetMeshAsset is MeshAsset
    assert LegacyDomainMeshAsset is MeshAsset
    assert AppLegacyMeshAsset is MeshAsset
    assert DomainMeshSpec is MeshSpec


def test_mesh_loader_legacy_modules_reexport_canonical_symbols() -> None:
    assert OBJMeshData.__module__ == "termin.default_assets.mesh.obj_loader"
    assert STLMeshData.__module__ == "termin.default_assets.mesh.stl_loader"
    assert parse_obj_text.__module__ == "termin.default_assets.mesh.obj_loader"


def test_mesh_plugin_legacy_modules_reexport_factories() -> None:
    assert legacy_create_import_plugin is create_import_plugin
    assert legacy_create_runtime_plugin is create_runtime_plugin
    assert create_import_plugin().type_id == "mesh"
    assert create_runtime_plugin().type_id == "mesh"
