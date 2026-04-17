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
