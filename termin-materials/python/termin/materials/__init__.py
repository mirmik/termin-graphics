from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_base", "termin_graphics", "termin_graphics2", "termin_materials")

from ._materials_native import (
    GlslPreprocessor,
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
    glsl_preprocessor,
    parse_property_directive,
    parse_shader_text,
    register_glsl_preprocessor,
    tc_material_count,
    tc_material_get_all_info,
)

__all__ = [
    "GlslPreprocessor",
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
    "glsl_preprocessor",
    "parse_property_directive",
    "parse_shader_text",
    "register_glsl_preprocessor",
    "tc_material_count",
    "tc_material_get_all_info",
]
