#include <shaderc/shaderc.hpp>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

struct CompileOptions {
    std::string language = "glsl";
    std::string target;
    std::string stage;
    std::string input;
    std::string output;
    std::string entry = "main";
    std::string debug_name = "shader";
    std::string slangc;
    std::string matrix_layout = "column";
};

struct ShaderResourceBinding {
    std::string name;
    std::string kind;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
};

static void usage() {
    std::cerr
        << "Usage: termin_shaderc compile --language glsl|slang "
        << "--target opengl|vulkan|d3d11 --stage vertex|fragment|geometry "
        << "--input <source> --output <artifact> [--entry main] "
        << "[--debug-name name] [--slangc <path>] "
        << "[--matrix-layout column|row]\n";
}

static shaderc_shader_kind shader_kind_for_stage(const std::string& stage) {
    if (stage == "vertex") return shaderc_vertex_shader;
    if (stage == "fragment") return shaderc_fragment_shader;
    if (stage == "geometry") return shaderc_geometry_shader;
    return shaderc_glsl_infer_from_source;
}

static std::string slang_stage_for_stage(const std::string& stage) {
    if (stage == "vertex") return "vertex";
    if (stage == "fragment") return "fragment";
    if (stage == "geometry") return "geometry";
    return "";
}

static std::optional<std::string> slang_matrix_layout_arg(const std::string& layout) {
    if (layout == "column" || layout == "col" || layout == "column-major" || layout == "col-major") {
        return "-matrix-layout-column-major";
    }
    if (layout == "row" || layout == "row-major") {
        return "-matrix-layout-row-major";
    }
    return std::nullopt;
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

static uint32_t stage_mask_for_stage(const std::string& stage) {
    if (stage == "vertex") return 1u << 0;
    if (stage == "fragment") return 1u << 1;
    if (stage == "geometry") return 1u << 2;
    if (stage == "compute") return 1u << 3;
    return 0u;
}

static std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

static void append_unique_resource(
    std::vector<ShaderResourceBinding>& resources,
    ShaderResourceBinding binding
) {
    for (ShaderResourceBinding& existing : resources) {
        if (existing.name == binding.name) {
            existing.kind = binding.kind;
            existing.set = binding.set;
            existing.binding = binding.binding;
            existing.stage_mask |= binding.stage_mask;
            if (binding.size != 0) {
                existing.size = binding.size;
            }
            return;
        }
    }
    resources.push_back(std::move(binding));
}

static std::vector<ShaderResourceBinding> infer_resource_bindings(
    const std::string& source,
    const CompileOptions& options
) {
    std::vector<ShaderResourceBinding> resources;
    const uint32_t stage_mask = stage_mask_for_stage(options.stage);

    if (options.language == "slang") {
        static const std::regex material_buffer_re(
            R"(ConstantBuffer\s*<\s*MaterialParams\s*>\s*material\s*:\s*register\s*\(\s*b([0-9]+)\s*,\s*space([0-9]+)\s*\))");
        std::smatch match;
        if (std::regex_search(source, match, material_buffer_re)) {
            ShaderResourceBinding binding;
            binding.name = "material";
            binding.kind = "constant_buffer";
            binding.binding = static_cast<uint32_t>(std::stoul(match[1].str()));
            binding.set = static_cast<uint32_t>(std::stoul(match[2].str()));
            binding.stage_mask = stage_mask;
            append_unique_resource(resources, std::move(binding));
        }
    }

    if (options.language == "glsl") {
        static const std::regex material_ubo_re(
            R"(layout\s*\([^\)]*binding\s*=\s*([0-9]+)[^\)]*\)\s*uniform\s+MaterialParams)");
        std::smatch match;
        if (std::regex_search(source, match, material_ubo_re)) {
            ShaderResourceBinding binding;
            binding.name = "material";
            binding.kind = "constant_buffer";
            binding.set = 0;
            binding.binding = static_cast<uint32_t>(std::stoul(match[1].str()));
            binding.stage_mask = stage_mask;
            append_unique_resource(resources, std::move(binding));
        }
    }

    return resources;
}

static bool write_resource_layout_sidecar(
    const CompileOptions& options,
    const std::string& source
) {
    const std::string path = options.output + ".layout.json";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open layout sidecar: " << path << "\n";
        return false;
    }

    std::vector<ShaderResourceBinding> resources = infer_resource_bindings(source, options);

    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"language\": \"" << json_escape(options.language) << "\",\n";
    out << "  \"target\": \"" << json_escape(options.target) << "\",\n";
    out << "  \"stage\": \"" << json_escape(options.stage) << "\",\n";
    out << "  \"resources\": [\n";
    for (size_t i = 0; i < resources.size(); ++i) {
        const ShaderResourceBinding& binding = resources[i];
        out << "    {"
            << "\"name\": \"" << json_escape(binding.name) << "\", "
            << "\"kind\": \"" << json_escape(binding.kind) << "\", "
            << "\"set\": " << binding.set << ", "
            << "\"binding\": " << binding.binding << ", "
            << "\"stage_mask\": " << binding.stage_mask << ", "
            << "\"size\": " << binding.size
            << "}";
        if (i + 1 < resources.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    if (!out) {
        std::cerr << "termin_shaderc: failed to write layout sidecar: " << path << "\n";
        return false;
    }
    return true;
}

static bool is_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec)
        && !std::filesystem::is_directory(path, ec);
}

static std::vector<std::string> split_paths(const char* value) {
    std::vector<std::string> paths;
    if (!value || value[0] == '\0') {
        return paths;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string text(value);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            paths.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

static std::vector<std::string> executable_names(const std::string& base) {
#ifdef _WIN32
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".exe") {
        return {base};
    }
    return {base + ".exe", base};
#else
    return {base};
#endif
}

static std::optional<std::string> find_on_path(const std::string& exe_name) {
    for (const std::string& dir : split_paths(std::getenv("PATH"))) {
        for (const std::string& name : executable_names(exe_name)) {
            std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (is_existing_file(candidate)) {
                return candidate.string();
            }
        }
    }
    return std::nullopt;
}

static std::optional<std::string> find_sdk_tool(const std::string& exe_name, const char* argv0) {
    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            for (const std::string& name : executable_names(exe_name)) {
                std::filesystem::path candidate = std::filesystem::path(sdk) / "bin" / name;
                if (is_existing_file(candidate)) {
                    return candidate.string();
                }
            }
        }
    }

    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        std::filesystem::path tool_dir = std::filesystem::absolute(argv0, ec).parent_path();
        if (!ec) {
            for (const std::string& name : executable_names(exe_name)) {
                std::filesystem::path candidate = tool_dir / name;
                if (is_existing_file(candidate)) {
                    return candidate.string();
                }
            }
        }
    }
    return std::nullopt;
}

static std::optional<std::string> resolve_slangc(const CompileOptions& options, const char* argv0) {
    if (!options.slangc.empty()) {
        if (!is_existing_file(options.slangc)) {
            std::cerr << "termin_shaderc: slangc does not exist: " << options.slangc << "\n";
            return std::nullopt;
        }
        return options.slangc;
    }

    if (const char* env = std::getenv("TERMIN_SLANGC")) {
        if (env[0] != '\0') {
            if (!is_existing_file(env)) {
                std::cerr << "termin_shaderc: TERMIN_SLANGC points to missing slangc: " << env << "\n";
                return std::nullopt;
            }
            return std::string(env);
        }
    }

    if (auto found = find_on_path("slangc")) {
        return found;
    }
    if (auto found = find_sdk_tool("slangc", argv0)) {
        return found;
    }

    std::cerr
        << "termin_shaderc: slangc not found. Set TERMIN_SLANGC, add slangc to PATH, "
        << "or install it under TERMIN_SDK/bin.\n";
    return std::nullopt;
}

static std::string quote_arg(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
#endif
}

static int run_command(const std::vector<std::string>& args) {
    std::ostringstream cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd << ' ';
        cmd << quote_arg(args[i]);
    }
    int status = std::system(cmd.str().c_str());
#ifdef _WIN32
    return status;
#else
    if (status == -1) {
        return 127;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return status;
#endif
}

static bool compile_glsl_to_vulkan(const CompileOptions& options) {
    if (options.target != "vulkan") {
        std::cerr << "termin_shaderc: GLSL input currently supports only --target vulkan\n";
        return false;
    }

    shaderc_shader_kind kind = shader_kind_for_stage(options.stage);
    if (kind == shaderc_glsl_infer_from_source) {
        std::cerr << "termin_shaderc: unsupported stage: " << options.stage << "\n";
        return false;
    }

    std::string source;
    if (!read_file(options.input, source)) {
        return false;
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions shader_options;
    shader_options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    shader_options.SetTargetSpirv(shaderc_spirv_version_1_5);
    shader_options.SetOptimizationLevel(shaderc_optimization_level_performance);
    shader_options.SetForcedVersionProfile(450, shaderc_profile_core);
    shader_options.SetAutoMapLocations(true);
    shader_options.SetAutoBindUniforms(true);
    shader_options.AddMacroDefinition("VULKAN", "100");

    auto module = compiler.CompileGlslToSpv(
        source,
        kind,
        options.debug_name.c_str(),
        options.entry.c_str(),
        shader_options
    );

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "termin_shaderc: shader compilation failed for "
                  << options.debug_name << ": " << module.GetErrorMessage() << "\n";
        return false;
    }

    std::vector<uint32_t> spirv(module.cbegin(), module.cend());
    if (!write_spirv(options.output, spirv)) {
        std::cerr << "termin_shaderc: failed to write SPIR-V: " << options.output << "\n";
        return false;
    }

    return write_resource_layout_sidecar(options, source);
}

static bool compile_slang(const CompileOptions& options, const char* argv0) {
    std::string slang_stage = slang_stage_for_stage(options.stage);
    if (slang_stage.empty()) {
        std::cerr << "termin_shaderc: unsupported stage: " << options.stage << "\n";
        return false;
    }

    std::string slang_target;
    std::vector<std::string> extra_args;
    if (options.target == "vulkan") {
        slang_target = "spirv";
        extra_args = {
            "-profile", "spirv_1_5",
            "-fvk-b-shift", "0", "all",
            "-fvk-t-shift", "0", "all",
            "-fvk-s-shift", "0", "all",
        };
    } else if (options.target == "opengl") {
        slang_target = "glsl";
        extra_args = {
            "-profile", "glsl_450",
            "-fvk-b-shift", "0", "all",
            "-fvk-t-shift", "0", "all",
            "-fvk-s-shift", "0", "all",
        };
    } else if (options.target == "d3d11") {
        std::cerr
            << "termin_shaderc: slang -> d3d11 requires the Windows FXC/DXBC path; "
            << "this target is reserved for the Windows backend phase\n";
        return false;
    } else {
        std::cerr << "termin_shaderc: unsupported target: " << options.target << "\n";
        return false;
    }

    auto slangc = resolve_slangc(options, argv0);
    if (!slangc) {
        return false;
    }
    auto matrix_layout_arg = slang_matrix_layout_arg(options.matrix_layout);
    if (!matrix_layout_arg) {
        std::cerr
            << "termin_shaderc: unsupported matrix layout: " << options.matrix_layout
            << " (expected column or row)\n";
        return false;
    }
    std::string source;
    if (!read_file(options.input, source)) {
        return false;
    }

    std::vector<std::string> args = {
        *slangc,
        options.input,
        "-entry", options.entry,
        "-stage", slang_stage,
        "-target", slang_target,
        *matrix_layout_arg,
    };
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    args.insert(args.end(), {"-o", options.output});

    int rc = run_command(args);
    if (rc != 0) {
        std::cerr << "termin_shaderc: slangc failed with exit code " << rc << "\n";
        return false;
    }

    if (!is_existing_file(options.output)) {
        std::cerr << "termin_shaderc: slangc did not produce expected output: "
                  << options.output << "\n";
        return false;
    }
    return write_resource_layout_sidecar(options, source);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "compile") {
        usage();
        return 2;
    }

    CompileOptions options;

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
            if (!take_value(options.target)) return 2;
        } else if (arg == "--language") {
            if (!take_value(options.language)) return 2;
        } else if (arg == "--stage") {
            if (!take_value(options.stage)) return 2;
        } else if (arg == "--input") {
            if (!take_value(options.input)) return 2;
        } else if (arg == "--output") {
            if (!take_value(options.output)) return 2;
        } else if (arg == "--entry") {
            if (!take_value(options.entry)) return 2;
        } else if (arg == "--debug-name") {
            if (!take_value(options.debug_name)) return 2;
        } else if (arg == "--slangc") {
            if (!take_value(options.slangc)) return 2;
        } else if (arg == "--matrix-layout") {
            if (!take_value(options.matrix_layout)) return 2;
        } else {
            std::cerr << "termin_shaderc: unknown argument: " << arg << "\n";
            usage();
            return 2;
        }
    }

    if (options.target.empty() || options.stage.empty()
        || options.input.empty() || options.output.empty()) {
        usage();
        return 2;
    }

    if (options.language == "glsl") {
        return compile_glsl_to_vulkan(options) ? 0 : 1;
    }
    if (options.language == "slang") {
        return compile_slang(options, argv[0]) ? 0 : 1;
    }

    std::cerr << "termin_shaderc: unsupported language: " << options.language << "\n";
    return 2;
}
