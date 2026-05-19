#include <shaderc/shaderc.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

static void usage() {
    std::cerr
        << "Usage: termin_shaderc compile --target vulkan "
        << "--stage vertex|fragment|geometry --input <source.glsl> "
        << "--output <artifact.spv> [--entry main] [--debug-name name]\n";
}

static shaderc_shader_kind shader_kind_for_stage(const std::string& stage) {
    if (stage == "vertex") return shaderc_vertex_shader;
    if (stage == "fragment") return shaderc_fragment_shader;
    if (stage == "geometry") return shaderc_geometry_shader;
    return shaderc_glsl_infer_from_source;
}

static bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "termin_shaderc: failed to open input: " << path << "\n";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static bool write_spirv(const std::string& path, const std::vector<uint32_t>& words) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open output: " << path << "\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(words.data()),
              static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "compile") {
        usage();
        return 2;
    }

    std::string target;
    std::string stage;
    std::string input;
    std::string output;
    std::string entry = "main";
    std::string debug_name = "shader";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto take_value = [&](std::string& dst) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "termin_shaderc: missing value for " << arg << "\n";
                return false;
            }
            dst = argv[++i];
            return true;
        };

        if (arg == "--target") {
            if (!take_value(target)) return 2;
        } else if (arg == "--stage") {
            if (!take_value(stage)) return 2;
        } else if (arg == "--input") {
            if (!take_value(input)) return 2;
        } else if (arg == "--output") {
            if (!take_value(output)) return 2;
        } else if (arg == "--entry") {
            if (!take_value(entry)) return 2;
        } else if (arg == "--debug-name") {
            if (!take_value(debug_name)) return 2;
        } else {
            std::cerr << "termin_shaderc: unknown argument: " << arg << "\n";
            usage();
            return 2;
        }
    }

    if (target != "vulkan" || stage.empty() || input.empty() || output.empty()) {
        usage();
        return 2;
    }

    shaderc_shader_kind kind = shader_kind_for_stage(stage);
    if (kind == shaderc_glsl_infer_from_source) {
        std::cerr << "termin_shaderc: unsupported stage: " << stage << "\n";
        return 2;
    }

    std::string source;
    if (!read_file(input, source)) {
        return 1;
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_5);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetForcedVersionProfile(450, shaderc_profile_core);
    options.SetAutoMapLocations(true);
    options.SetAutoBindUniforms(true);
    options.AddMacroDefinition("VULKAN", "100");

    auto module = compiler.CompileGlslToSpv(
        source,
        kind,
        debug_name.c_str(),
        entry.c_str(),
        options
    );

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "termin_shaderc: shader compilation failed for "
                  << debug_name << ": " << module.GetErrorMessage() << "\n";
        return 1;
    }

    std::vector<uint32_t> spirv(module.cbegin(), module.cend());
    if (!write_spirv(output, spirv)) {
        std::cerr << "termin_shaderc: failed to write SPIR-V: " << output << "\n";
        return 1;
    }

    return 0;
}
