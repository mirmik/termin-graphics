"""Missing-material runtime fallback."""

from __future__ import annotations

from tcbase import log
from tgfx import TcShader

from termin.materials import TcMaterial


UNKNOWN_SHADER_UUID = "termin-engine-line-default"


def create_unknown_material(
    name: str = "UnknownMaterial",
    error_message: str | None = None,
) -> TcMaterial:
    """Create a visible fallback material for missing or broken materials."""
    mat_name = name
    if error_message:
        mat_name = f"{name}: {error_message[:50]}"

    mat = TcMaterial.create(mat_name, "")
    mat.shader_name = "UnknownShader"

    shader = TcShader.from_builtin_catalog(UNKNOWN_SHADER_UUID)
    if not shader.is_valid:
        log.error(
            "UnknownMaterial: failed to load built-in shader '%s'",
            UNKNOWN_SHADER_UUID,
        )
        return mat

    phase = mat.add_phase(shader, "opaque", 0)

    if phase is not None:
        phase.set_color(1.0, 0.0, 1.0, 1.0)

    return mat


class UnknownMaterial(TcMaterial):
    """Fallback material for missing or broken shaders and materials."""

    def __new__(
        cls,
        original_shader: str | None = None,
        original_material: str | None = None,
        error_message: str | None = None,
    ) -> TcMaterial:
        msg = error_message
        if not msg:
            if original_shader:
                msg = f"Missing shader: {original_shader}"
            elif original_material:
                msg = f"Missing material: {original_material}"
        return create_unknown_material(error_message=msg)

    @classmethod
    def for_missing_shader(cls, shader_name: str) -> TcMaterial:
        """Create an UnknownMaterial for a missing shader."""
        return create_unknown_material(error_message=f"Missing shader: {shader_name}")

    @classmethod
    def for_missing_material(
        cls,
        material_name: str,
    ) -> TcMaterial:
        """Create an UnknownMaterial for a missing material."""
        return create_unknown_material(error_message=f"Missing material: {material_name}")

    @classmethod
    def for_error(
        cls,
        error: Exception,
    ) -> TcMaterial:
        """Create an UnknownMaterial for a material loading error."""
        return create_unknown_material(error_message=str(error))


def material_or_unknown(material: TcMaterial | None, material_name: str) -> TcMaterial:
    """Return a material or a visible fallback for missing material references."""
    if material is not None:
        return material
    return UnknownMaterial.for_missing_material(material_name)
