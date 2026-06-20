from termin.materials import TcMaterial, UnknownMaterial, create_unknown_material
from termin.materials.unknown_material import UnknownMaterial as CanonicalUnknownMaterial


def test_unknown_material_is_canonical_material_api() -> None:
    assert UnknownMaterial is CanonicalUnknownMaterial

    material = create_unknown_material(error_message="missing probe")

    assert isinstance(material, TcMaterial)
    assert material.is_valid
    assert material.name.startswith("UnknownMaterial")


def test_unknown_material_missing_material_factory() -> None:
    material = UnknownMaterial.for_missing_material("probe")

    assert isinstance(material, TcMaterial)
    assert material.is_valid
    assert "probe" in material.name
