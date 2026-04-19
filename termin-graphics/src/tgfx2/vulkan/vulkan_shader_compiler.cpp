#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"
#include <shaderc/shaderc.hpp>

namespace tgfx::vk {

static shaderc_shader_kind to_shaderc_kind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return shaderc_vertex_shader;
        case ShaderStage::Fragment: return shaderc_fragment_shader;
        case ShaderStage::Geometry: return shaderc_geometry_shader;
        case ShaderStage::Compute:  return shaderc_compute_shader;
    }
    return shaderc_vertex_shader;
}

SpirvCompileResult compile_glsl_to_spirv(
    const std::string& source,
    ShaderStage stage,
    const std::string& entry_point
) {
    SpirvCompileResult result;

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Target Vulkan 1.2 / SPIR-V 1.5 (compatible with validation layers)
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_5);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    // Force GLSL 450 profile to handle #version 330 sources
    options.SetForcedVersionProfile(450, shaderc_profile_core);

    // Auto-assign `layout(location=N)` to `in`/`out` varyings that
    // lack an explicit qualifier. Legacy shaders inherited from the
    // GL 3.3 era declare `in vec3 a_position;` / `out vec3 v_normal;`
    // without locations — SPIR-V requires locations for user inputs
    // and outputs, and shaderc honours this flag to emit them
    // automatically (in declaration order).
    //
    // Auto-bind for resources at the same time: auto-picks binding
    // slots for `uniform sampler2D u_foo;` declarations lacking an
    // explicit `layout(binding=N)`. UBOs/SSBOs we always tag with
    // explicit bindings in the engine, so auto-bind for those would
    // never fire; leaving it on is free.
    options.SetAutoMapLocations(true);
    options.SetAutoBindUniforms(true);

    // Shaders fork their declarations with `#ifdef VULKAN`. shaderc is
    // supposed to auto-define `VULKAN=100` when targeting Vulkan, but in
    // practice that depends on the target env + SPIR-V version combo and
    // proved unreliable here (shaders kept landing in the `#else` branch
    // and trying to bind the GL-emulation UBO at slot 14, breaking
    // pipeline layout validation). Define the macro explicitly — safe
    // because shaderc's own define (if any) uses the same name and
    // compatible numeric value, and in that rare case shaderc silently
    // accepts a matching redefinition.
    options.AddMacroDefinition("VULKAN", "100");

    auto kind = to_shaderc_kind(stage);
    auto module = compiler.CompileGlslToSpv(source, kind, "shader", entry_point.c_str(), options);

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.success = false;
        result.error_message = module.GetErrorMessage();
        return result;
    }

    result.spirv.assign(module.cbegin(), module.cend());
    result.success = true;
    return result;
}

} // namespace tgfx::vk

#endif // TGFX2_HAS_VULKAN
