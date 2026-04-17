#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <string>
#include <vector>
#include <cstdint>

#include "tgfx2/enums.hpp"

namespace tgfx::vk {

struct SpirvCompileResult {
    std::vector<uint32_t> spirv;
    std::string error_message;
    bool success = false;
};

// Compile GLSL source to SPIR-V bytecode using shaderc.
// Handles #version 330/450 differences automatically.
SpirvCompileResult compile_glsl_to_spirv(
    const std::string& source,
    ShaderStage stage,
    const std::string& entry_point = "main"
);

} // namespace tgfx::vk

#endif // TGFX2_HAS_VULKAN
