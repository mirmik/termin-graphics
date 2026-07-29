import json

import numpy as np
import pytest

from termin.glb.instantiator import (
    _build_texture_lookup,
    _configure_import_material,
    _plan_texture_imports,
    _stable_glb_texture_uuid,
)
from termin.glb.loader import GLBMaterialData, GLBSceneData, GLBTcTexture
from termin.image import encode_png_rgba8
from tgfx import TextureEncoding


_PNG_1X1 = encode_png_rgba8(
    np.array([[[255, 64, 32, 255]]], dtype=np.uint8)
)


def _texture(
    index: int,
    *,
    image_index: int = 0,
    sampler_index: int | None = None,
    sampler: dict[str, int] | None = None,
) -> GLBTcTexture:
    return GLBTcTexture(
        index=index,
        name="SharedImage",
        data=_PNG_1X1,
        mime_type="image/png",
        image_index=image_index,
        sampler_index=sampler_index,
        sampler=sampler,
    )


def _scene(
    textures: list[GLBTcTexture],
    materials: list[GLBMaterialData],
) -> GLBSceneData:
    scene = GLBSceneData()
    scene.textures = textures
    scene.materials = materials
    return scene


def test_color_only_texture_keeps_one_ordinary_import() -> None:
    scene = _scene(
        [_texture(0)],
        [GLBMaterialData("Color", base_color_texture=0)],
    )

    imports = _plan_texture_imports(scene)

    assert len(imports) == 1
    assert imports[0].encoding == "srgb"
    assert imports[0].texture_indices == (0,)
    assert not imports[0].collision


def test_data_only_texture_keeps_one_ordinary_import() -> None:
    scene = _scene(
        [_texture(0)],
        [GLBMaterialData("Normal", normal_texture=0)],
    )

    imports = _plan_texture_imports(scene)

    assert len(imports) == 1
    assert imports[0].encoding == "linear"
    assert imports[0].texture_indices == (0,)
    assert not imports[0].collision


def test_shared_image_with_same_encoding_stays_one_import() -> None:
    scene = _scene(
        [_texture(0), _texture(1)],
        [
            GLBMaterialData("Base", base_color_texture=0),
            GLBMaterialData("Emission", emissive_texture=1),
        ],
    )

    imports = _plan_texture_imports(scene)

    assert len(imports) == 1
    assert imports[0].encoding == "srgb"
    assert imports[0].texture_indices == (0, 1)
    assert not imports[0].collision


def test_shared_image_collision_creates_exactly_two_encoding_variants() -> None:
    texture = _texture(0)
    scene = _scene(
        [texture],
        [
            GLBMaterialData(
                "Conflicting",
                base_color_texture=0,
                normal_texture=0,
            )
        ],
    )

    imports = _plan_texture_imports(scene)

    assert [(item.encoding, item.texture_indices, item.collision) for item in imports] == [
        ("linear", (0,), True),
        ("srgb", (0,), True),
    ]
    assert _stable_glb_texture_uuid(texture, "linear") != _stable_glb_texture_uuid(
        texture,
        "srgb",
    )


def test_sampler_variants_remain_distinct_resource_identities() -> None:
    scene = _scene(
        [
            _texture(
                0,
                sampler_index=0,
                sampler={"wrapS": 10497, "wrapT": 10497},
            ),
            _texture(
                1,
                sampler_index=1,
                sampler={"wrapS": 33071, "wrapT": 33071},
            ),
        ],
        [
            GLBMaterialData("Repeated", base_color_texture=0),
            GLBMaterialData("Clamped", base_color_texture=1),
        ],
    )

    imports = _plan_texture_imports(scene)

    assert len(imports) == 2
    assert imports[0].resource_key != imports[1].resource_key
    assert {item.texture_indices for item in imports} == {(0,), (1,)}
    assert all(item.encoding == "srgb" for item in imports)
    assert all(not item.collision for item in imports)


def test_material_traversal_order_does_not_change_import_plan() -> None:
    textures = [_texture(0), _texture(1)]
    materials = [
        GLBMaterialData("Color", base_color_texture=0),
        GLBMaterialData("Data", occlusion_texture=1),
        GLBMaterialData("Collision", normal_texture=0),
    ]

    forward = _plan_texture_imports(_scene(textures, materials))
    backward = _plan_texture_imports(_scene(textures, list(reversed(materials))))

    assert forward == backward


def test_extension_texture_usage_requires_explicit_supported_encoding() -> None:
    scene = _scene(
        [_texture(0)],
        [
            GLBMaterialData(
                "Extension",
                extension_texture_usages=[(0, "linear")],
            )
        ],
    )

    assert _plan_texture_imports(scene)[0].encoding == "linear"

    with pytest.raises(ValueError, match="unsupported encoding contract"):
        GLBMaterialData(
            "UnknownExtension",
            extension_texture_usages=[(0, "by-filename")],
        )


class _NamedTexture:
    def __init__(self, name: str):
        self.name = name
        self.uuid = f"{name}-uuid"


class _RecordingMaterial:
    name = "RecordingMaterial"
    textures = {}

    def __init__(self):
        self.assigned = {}

    def set_uniform_vec4(self, name, value):
        pass

    def set_uniform_float(self, name, value):
        pass

    def set_texture(self, name, texture):
        self.assigned[name] = texture
        return 1


def test_material_slots_select_their_declared_lookup_encoding() -> None:
    srgb = _NamedTexture("srgb")
    linear = _NamedTexture("linear")
    lookup = {
        (0, "srgb"): srgb,
        (0, "linear"): linear,
    }
    material = _RecordingMaterial()

    _configure_import_material(
        material,
        GLBMaterialData(
            "Conflicting",
            base_color_texture=0,
            metallic_roughness_texture=0,
            normal_texture=0,
            occlusion_texture=0,
            emissive_texture=0,
        ),
        lookup,
    )

    assert material.assigned["u_albedo_texture"] is srgb
    assert material.assigned["u_emissive_texture"] is srgb
    assert material.assigned["u_metallic_roughness_texture"] is linear
    assert material.assigned["u_normal_texture"] is linear
    assert material.assigned["u_occlusion_texture"] is linear


class _RuntimeTextureManager:
    def __init__(self):
        self.by_name = {}
        self.by_uuid = {}

    def list_runtime_asset_names(self, asset_type):
        assert asset_type == "texture"
        return list(self.by_name)

    def get_runtime_asset(self, asset_type, name):
        assert asset_type == "texture"
        return self.by_name.get(name)

    def get_runtime_asset_by_uuid(self, asset_type, asset_uuid):
        assert asset_type == "texture"
        return self.by_uuid.get(asset_uuid)

    def register_runtime_asset(
        self,
        asset_type,
        name,
        asset,
        *,
        source_path=None,
        uuid=None,
    ):
        assert asset_type == "texture"
        self.by_name[name] = asset
        self.by_uuid[uuid] = asset


def test_collision_lookup_builds_and_reuses_two_native_encodings() -> None:
    scene = _scene(
        [_texture(0)],
        [GLBMaterialData("Conflicting", base_color_texture=0, normal_texture=0)],
    )
    rm = _RuntimeTextureManager()

    first = _build_texture_lookup(rm, scene)
    second = _build_texture_lookup(rm, scene)

    assert set(first) == {(0, "srgb"), (0, "linear")}
    assert len(rm.by_uuid) == 2
    assert first[(0, "srgb")].encoding == TextureEncoding.SRGB
    assert first[(0, "linear")].encoding == TextureEncoding.LINEAR
    assert second[(0, "srgb")].uuid == first[(0, "srgb")].uuid
    assert second[(0, "linear")].uuid == first[(0, "linear")].uuid
    assert first[(0, "srgb")].uuid != first[(0, "linear")].uuid
    assert set(rm.by_name) == {"SharedImage_linear", "SharedImage_srgb"}


def test_unambiguous_lookup_does_not_add_encoding_to_name_or_uuid() -> None:
    texture = _texture(0)
    scene = _scene(
        [texture],
        [GLBMaterialData("Data", normal_texture=0)],
    )
    rm = _RuntimeTextureManager()

    lookup = _build_texture_lookup(rm, scene)

    assert set(rm.by_name) == {"SharedImage"}
    assert lookup[(0, "linear")].uuid == _stable_glb_texture_uuid(texture)


def test_shared_same_encoding_lookup_reuses_one_native_asset() -> None:
    scene = _scene(
        [_texture(0), _texture(1)],
        [
            GLBMaterialData("Base", base_color_texture=0),
            GLBMaterialData("Emission", emissive_texture=1),
        ],
    )
    rm = _RuntimeTextureManager()

    lookup = _build_texture_lookup(rm, scene)

    assert len(rm.by_uuid) == 1
    assert lookup[(0, "srgb")].uuid == lookup[(1, "srgb")].uuid


def test_external_metadata_mismatch_creates_immutable_runtime_texture(
    tmp_path,
) -> None:
    source_path = tmp_path / "normal.png"
    source_path.write_bytes(_PNG_1X1)
    source_path.with_name("normal.png.meta").write_text(
        json.dumps({"uuid": "project-srgb", "encoding": "srgb"}),
        encoding="utf-8",
    )
    texture = GLBTcTexture(
        index=0,
        name="NormalImage",
        data=_PNG_1X1,
        mime_type="image/png",
        source_path=source_path,
        image_index=0,
    )
    scene = _scene(
        [texture],
        [GLBMaterialData("Normal", normal_texture=0)],
    )
    rm = _RuntimeTextureManager()

    lookup = _build_texture_lookup(rm, scene)

    assert lookup[(0, "linear")].encoding == TextureEncoding.LINEAR
    assert lookup[(0, "linear")].uuid != "project-srgb"
    assert len(rm.by_uuid) == 1
