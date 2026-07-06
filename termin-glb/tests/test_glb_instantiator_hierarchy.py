import numpy as np

from termin.glb.instantiator import instantiate_glb
from termin.glb.loader import GLBNodeData, GLBSceneData


class _FakeMaterial:
    is_valid = True


class _FakeResourceManager:
    def get_material(self, name):
        assert name == "NormalizedPBR"
        return _FakeMaterial()

    def list_runtime_asset_names(self, asset_type):
        return []


class _FakeGLBAsset:
    name = "Arthur"

    def __init__(self, scene_data):
        self.scene_data = scene_data

    def get_mesh_assets(self):
        return {}

    def get_skeleton_assets(self):
        return {}

    def get_animation_assets(self):
        return {}


def test_instantiate_glb_preserves_single_imported_root_node(monkeypatch):
    import termin_assets

    monkeypatch.setattr(termin_assets, "get_resource_manager", lambda: _FakeResourceManager())

    scene_data = GLBSceneData()
    scene_data.nodes.append(
        GLBNodeData(
            name="Armature",
            children=[],
            mesh_index=None,
            translation=np.array([1.0, 2.0, 3.0], dtype=np.float32),
            rotation=np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32),
            scale=np.array([1.0, 1.0, 1.0], dtype=np.float32),
        )
    )
    scene_data.root_nodes = [0]

    result = instantiate_glb(_FakeGLBAsset(scene_data), name="Model")

    assert result.entity.name == "Model"
    children = result.entity.transform.children
    assert len(children) == 1
    imported_root = children[0].entity
    assert imported_root.name == "Armature"
    assert tuple(imported_root.transform.local_position()) == (1.0, 2.0, 3.0)
