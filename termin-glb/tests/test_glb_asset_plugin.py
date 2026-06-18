from pathlib import Path

from termin_assets import AssetContext, PreLoadResult
from termin.glb.asset import GLBAsset
from termin.glb.asset_plugin import GLBImportPlugin, GLBRuntimePlugin


class FakeResourceManager:
    def __init__(self):
        self._assets_by_uuid = {}
        self._glb_assets = {}

    def get_glb_asset(self, name: str):
        return self._glb_assets.get(name)

    def get_glb_asset_by_uuid(self, uuid: str):
        asset = self._assets_by_uuid.get(uuid)
        if isinstance(asset, GLBAsset):
            return asset
        return None

    def register_glb_asset(self, name: str, asset: GLBAsset, source_path: str | None = None):
        self._glb_assets[name] = asset
        self._assets_by_uuid[asset.uuid] = asset
        if source_path:
            asset.source_path = source_path


def test_glb_import_plugin_preloads_meta_uuid(tmp_path):
    path = tmp_path / "robot.glb"
    path.write_bytes(b"glb")
    path.with_name(path.name + ".meta").write_text('{"uuid": "glb-test-uuid"}', encoding="utf-8")

    result = GLBImportPlugin().preload(str(path))

    assert result is not None
    assert result.resource_type == "glb"
    assert result.path == str(path)
    assert result.content is None
    assert result.uuid == "glb-test-uuid"
    assert result.spec_data == {"uuid": "glb-test-uuid"}


def test_glb_runtime_plugin_registers_through_resource_manager_api():
    rm = FakeResourceManager()
    result = PreLoadResult(
        resource_type="glb",
        path="/tmp/robot.glb",
        content=None,
        uuid="glb-test-uuid",
        spec_data={"uuid": "glb-test-uuid"},
    )
    context = AssetContext(resource_manager=rm, name="robot")

    GLBRuntimePlugin().register(context, result)

    asset = rm.get_glb_asset("robot")
    assert isinstance(asset, GLBAsset)
    assert asset.uuid == "glb-test-uuid"
    assert asset.source_path == Path("/tmp/robot.glb")
    assert rm.get_glb_asset_by_uuid("glb-test-uuid") is asset
