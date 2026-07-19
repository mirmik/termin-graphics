from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_base", "termin_graphics", "termin_graphics2", "termin_materials")

from ._materials_native import (
    MaterialProperty,
    MaterialUboEntry,
    MaterialUboLayout,
    PhaseRenderSettings,
    ShasderStage,
    ShaderMultyPhaseProgramm,
    ShaderPhase,
    ShaderStage,
    TcMaterial,
    TcMaterialPhase,
    TcRenderState,
    UniformProperty,
    create_material_from_parsed,
    parse_property_directive,
    parse_shader_text,
    tc_material_count,
    tc_material_get_all_info,
)
from termin.materials.unknown_material import (
    UnknownMaterial,
    create_unknown_material,
    material_or_unknown,
)

__all__ = [
    "MaterialProperty",
    "MaterialUboEntry",
    "MaterialUboLayout",
    "PhaseRenderSettings",
    "ShasderStage",
    "ShaderMultyPhaseProgramm",
    "ShaderPhase",
    "ShaderStage",
    "TcMaterial",
    "TcMaterialPhase",
    "TcRenderState",
    "UniformProperty",
    "UnknownMaterial",
    "create_material_from_parsed",
    "create_unknown_material",
    "material_or_unknown",
    "parse_property_directive",
    "parse_shader_text",
    "tc_material_count",
    "tc_material_get_all_info",
]
