import numpy as np

from termin_assets import AssetContext, AssetTypeRegistry, PreLoadResult
from termin.mesh.asset import MeshAsset
from termin.mesh.asset_plugin import (
    create_import_plugin,
    create_runtime_plugin,
    register_mesh_import_plugin,
    register_mesh_runtime_plugin,
)
from termin.mesh.mesh_asset import MeshAsset as LegacyDomainMeshAsset
from termin.mesh.mesh_spec import DEFAULT_AXIS_X, DEFAULT_AXIS_Y, DEFAULT_AXIS_Z, MeshSpec
from termin.assets.mesh_asset import MeshAsset as AppLegacyMeshAsset
from termin.loaders.mesh_spec import MeshSpec as AppLegacyMeshSpec
from tmesh import Mesh3


class FakeResourceManager:
    def __init__(self) -> None:
        self.by_name = {}
        self.by_uuid = {}

    def get_runtime_asset(self, type_id: str, name: str):
        return self.by_name.get((type_id, name))

    def get_runtime_asset_by_uuid(self, type_id: str, uuid: str):
        return self.by_uuid.get((type_id, uuid))

    def register_runtime_asset(self, type_id: str, name: str, asset, *, source_path=None, uuid=None) -> None:
        self.by_name[(type_id, name)] = asset
        if uuid is not None:
            self.by_uuid[(type_id, uuid)] = asset


def test_mesh_asset_wraps_mesh3() -> None:
    mesh3 = Mesh3(
        vertices=np.array(
            [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float32,
        ),
        triangles=np.array([[0, 1, 2]], dtype=np.uint32),
        name="triangle",
    )

    asset = MeshAsset.from_mesh3(mesh3, name="triangle", source_path="/tmp/triangle.obj")

    assert asset.name == "triangle"
    assert asset.mesh_data is not None
    assert asset.mesh_data.is_valid
    assert asset.get_vertex_count() == 3
    assert asset.get_triangle_count() == 1
    assert str(asset.source_path) == "/tmp/triangle.obj"


def test_mesh_asset_legacy_modules_reexport_canonical_class() -> None:
    assert LegacyDomainMeshAsset is MeshAsset
    assert AppLegacyMeshAsset is MeshAsset
    assert AppLegacyMeshSpec is MeshSpec


def test_mesh_spec_defaults_live_in_termin_mesh() -> None:
    spec = MeshSpec()

    assert spec.axis_x == DEFAULT_AXIS_X
    assert spec.axis_y == DEFAULT_AXIS_Y
    assert spec.axis_z == DEFAULT_AXIS_Z


def test_mesh_plugins_register_with_asset_registry() -> None:
    registry = AssetTypeRegistry()

    register_mesh_import_plugin(registry)
    register_mesh_runtime_plugin(registry)

    assert registry.get_import("mesh") is not None
    assert registry.get_runtime("mesh") is not None
    assert registry.get_for_extension(".OBJ")[0].type_id == "mesh"


def test_mesh_runtime_plugin_registers_lazy_asset() -> None:
    resource_manager = FakeResourceManager()
    result = PreLoadResult(
        resource_type="mesh",
        path="/tmp/triangle.obj",
        uuid="mesh-uuid",
        spec_data={"scale": 2.0},
    )

    create_runtime_plugin().register(
        AssetContext(resource_manager=resource_manager, name="triangle"),
        result,
    )

    asset = resource_manager.get_runtime_asset("mesh", "triangle")
    assert isinstance(asset, MeshAsset)
    assert asset.uuid == "mesh-uuid"
    assert str(asset.source_path) == "/tmp/triangle.obj"
    assert asset._scale == 2.0


def test_mesh_entry_point_factories() -> None:
    assert create_import_plugin().type_id == "mesh"
    assert create_runtime_plugin().type_id == "mesh"
