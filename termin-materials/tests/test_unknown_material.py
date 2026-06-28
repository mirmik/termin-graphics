from termin.materials import (
    TcMaterial,
    UnknownMaterial,
    create_unknown_material,
    material_or_unknown,
)
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


def test_material_or_unknown_preserves_existing_material() -> None:
    material = TcMaterial.create("ExistingMaterial", "")

    assert material_or_unknown(material, "fallback") is material


def test_material_or_unknown_creates_missing_material_fallback() -> None:
    material = material_or_unknown(None, "probe")

    assert isinstance(material, TcMaterial)
    assert material.is_valid
    assert "Missing material: probe" in material.name
