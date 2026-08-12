#pragma once

#include <filesystem>
#include <string>

namespace termin_shaderc::internal {

    enum class GlslCrossProfile {
        Desktop330,
        Es300,
    };

    bool cross_compile_spirv_to_glsl(const std::filesystem::path& spirv_path,
                                     const std::filesystem::path& output_path,
                                     GlslCrossProfile profile,
                                     std::string& error);

} // namespace termin_shaderc::internal
