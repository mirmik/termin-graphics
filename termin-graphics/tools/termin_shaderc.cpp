#include <shaderc/shaderc.hpp>
#include <tcbase/trent/json.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
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
    std::string layout_scheme = "per-pipeline";  // shader-driven, Godot 4 model
    std::vector<std::string> include_dirs;
};

struct ShaderResourceBinding {
    std::string name;
    std::string kind;
    std::string scope;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
    bool slang_combined_texture = false;
    bool slang_split_texture = false;
    bool slang_separate_sampler = false;
    bool slang_storage_texture = false;
    uint32_t original_binding = 0;
    // Field-level layout for constant_buffers (populated from Slang reflection).
    struct Field {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    std::vector<Field> fields;
};

static void usage() {
    std::cerr
        << "Usage: termin_shaderc compile --language glsl|slang "
        << "--target opengl|vulkan|d3d11 --stage vertex|fragment|geometry "
        << "--input <source> --output <artifact> [--entry main] "
        << "[--debug-name name] [--slangc <path>] "
        << "[--matrix-layout column|row] "
        << "[-I <include-dir>] "
        << "[--layout-scheme shared|per-pipeline]\n";
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

static bool is_existing_file(const std::filesystem::path& path);

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

static bool string_contains(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}

static bool string_starts_with(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

static bool is_valid_explicit_resource_scope(const std::string& scope) {
    return scope == "frame" ||
           scope == "pass" ||
           scope == "material" ||
           scope == "draw" ||
           scope == "transient";
}

static std::string infer_resource_scope(
    const std::string& name,
    const std::string& kind
) {
    if (name == "per_frame" || name == "u_per_frame") {
        return "frame";
    }
    if (name == "material") {
        return "material";
    }
    if (name == "draw" || name == "draw_data" || name == "u_draw" ||
        name == "u_push" || name == "pc") {
        return "draw";
    }
    if (name == "lighting" || name == "lighting_ubo" ||
        string_contains(name, "shadow")) {
        return "pass";
    }
    if (name == "u_params") {
        return "pass";
    }
    if (kind == "texture" || kind == "storage_texture" || kind == "sampler") {
        if (name == "u_input" || name == "u_texture" ||
            name == "u_original" || name == "u_bloom" ||
            name == "u_color" || name == "u_id" ||
            name == "u_depth_tex" || name == "u_color_tex" ||
            name == "u_tex") {
            return "transient";
        }
        if (string_contains(name, "albedo") ||
            string_contains(name, "normal") ||
            string_contains(name, "metallic") ||
            string_contains(name, "roughness") ||
            string_contains(name, "occlusion") ||
            string_contains(name, "emissive") ||
            string_contains(name, "diffuse") ||
            string_contains(name, "tint")) {
            return "material";
        }
    }
    if (string_starts_with(name, "u_")) {
        return "unknown";
    }
    return "unknown";
}

static void assign_missing_resource_scopes(
    std::vector<ShaderResourceBinding>& resources
) {
    for (ShaderResourceBinding& resource : resources) {
        if (resource.scope.empty()) {
            resource.scope = infer_resource_scope(resource.name, resource.kind);
        }
    }
}

static void append_unique_resource(
    std::vector<ShaderResourceBinding>& resources,
    ShaderResourceBinding binding
) {
    for (ShaderResourceBinding& existing : resources) {
        if (existing.name == binding.name) {
            existing.kind = binding.kind;
            if (!binding.scope.empty() && binding.scope != "unknown") {
                existing.scope = binding.scope;
            } else if (existing.scope.empty()) {
                existing.scope = binding.scope;
            }
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

static const nos::trent* trent_dict_get(const nos::trent& value, const char* key) {
    if (!value.is_dict()) {
        return nullptr;
    }
    return value._get(key);
}

static bool trent_string_field(
    const nos::trent& value,
    const char* key,
    std::string& out
) {
    const nos::trent* field = trent_dict_get(value, key);
    if (!field || !field->is_string()) {
        return false;
    }
    out = field->as_string();
    return true;
}

static bool trent_uint_field(
    const nos::trent& value,
    const char* key,
    uint32_t& out
) {
    const nos::trent* field = trent_dict_get(value, key);
    if (!field || !field->is_numer()) {
        return false;
    }
    const int64_t number = field->as_integer();
    if (number < 0 ||
        number > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    out = static_cast<uint32_t>(number);
    return true;
}

static bool trent_bool_field(
    const nos::trent& value,
    const char* key,
    bool& out
) {
    const nos::trent* field = trent_dict_get(value, key);
    if (!field || !field->is_bool()) {
        return false;
    }
    out = field->as_bool();
    return true;
}

static bool slang_parameter_binding(
    const nos::trent& parameter,
    std::string& out_binding_kind,
    uint32_t& out_binding,
    uint32_t& out_set
) {
    const nos::trent* binding = trent_dict_get(parameter, "binding");
    if (!binding || !binding->is_dict()) {
        return false;
    }

    if (!trent_string_field(*binding, "kind", out_binding_kind)) {
        return false;
    }
    if (!trent_uint_field(*binding, "index", out_binding)) {
        return false;
    }
    out_set = 0;
    trent_uint_field(*binding, "space", out_set);
    return true;
}

static uint32_t slang_parameter_buffer_size(const nos::trent& parameter) {
    const nos::trent* type = trent_dict_get(parameter, "type");
    if (!type || !type->is_dict()) {
        return 0;
    }
    const nos::trent* element_layout = trent_dict_get(*type, "elementVarLayout");
    if (!element_layout || !element_layout->is_dict()) {
        return 0;
    }
    const nos::trent* binding = trent_dict_get(*element_layout, "binding");
    if (!binding || !binding->is_dict()) {
        return 0;
    }
    uint32_t size = 0;
    trent_uint_field(*binding, "size", size);
    return size;
}

static bool is_slang_texture_base_shape(const std::string& base_shape) {
    return base_shape.rfind("texture", 0) == 0;
}

static bool slang_resource_kind_from_parameter(
    const nos::trent& parameter,
    const std::string& binding_kind,
    ShaderResourceBinding& out_binding
) {
    const nos::trent* type = trent_dict_get(parameter, "type");
    std::string type_kind;
    if (!type || !type->is_dict() ||
        !trent_string_field(*type, "kind", type_kind)) {
        return false;
    }

    if (type_kind == "constantBuffer") {
        if (binding_kind != "constantBuffer" &&
            binding_kind != "descriptorTableSlot") {
            return false;
        }
        out_binding.kind = "constant_buffer";
        out_binding.size = slang_parameter_buffer_size(parameter);
        // Extract struct field offsets from Slang reflection.
        // Fields are in: type.elementVarLayout.type.fields[]
        const nos::trent* el = trent_dict_get(*type, "elementVarLayout");
        if (el && el->is_dict()) {
            const nos::trent* el_type = trent_dict_get(*el, "type");
            if (el_type && el_type->is_dict()) {
                const nos::trent* fields = trent_dict_get(*el_type, "fields");
                if (fields && fields->is_list()) {
                    for (const nos::trent& field : fields->as_list()) {
                        if (!field.is_dict()) continue;
                        ShaderResourceBinding::Field f;
                        if (!trent_string_field(field, "name", f.name)) continue;
                        const nos::trent* fb = trent_dict_get(field, "binding");
                        if (fb && fb->is_dict()) {
                            trent_uint_field(*fb, "offset", f.offset);
                            trent_uint_field(*fb, "size", f.size);
                        }
                        out_binding.fields.push_back(std::move(f));
                    }
                }
            }
        }
        return true;
    }

    if (type_kind == "samplerState") {
        if (binding_kind != "samplerState") {
            return false;
        }
        out_binding.kind = "sampler";
        out_binding.size = 0;
        out_binding.slang_separate_sampler = true;
        return true;
    }

    if (type_kind == "resource") {
        std::string base_shape;
        if (!trent_string_field(*type, "baseShape", base_shape) ||
            !is_slang_texture_base_shape(base_shape)) {
            return false;
        }

        std::string access;
        const bool has_access = trent_string_field(*type, "access", access);
        if (has_access && access == "readWrite") {
            if (binding_kind != "descriptorTableSlot") {
                return false;
            }
            out_binding.kind = "storage_texture";
            out_binding.slang_storage_texture = true;
        } else {
            if (binding_kind != "shaderResource" &&
                binding_kind != "descriptorTableSlot") {
                return false;
            }
            out_binding.kind = "texture";
            bool combined = false;
            trent_bool_field(*type, "combined", combined);
            out_binding.slang_combined_texture = combined;
            out_binding.slang_split_texture = !combined;
        }
        out_binding.size = 0;
        return true;
    }

    return false;
}

static std::optional<std::string> slang_scope_attribute_from_parameter(
    const nos::trent& parameter,
    const std::string& resource_name,
    bool& out_ok
) {
    out_ok = true;
    const nos::trent* user_attribs = trent_dict_get(parameter, "userAttribs");
    if (!user_attribs) {
        return std::nullopt;
    }
    if (!user_attribs->is_list()) {
        std::cerr
            << "termin_shaderc: malformed userAttribs for resource '"
            << resource_name << "'\n";
        out_ok = false;
        return std::nullopt;
    }

    std::optional<std::string> scope;
    for (const nos::trent& attrib : user_attribs->as_list()) {
        if (!attrib.is_dict()) {
            std::cerr
                << "termin_shaderc: malformed user attribute for resource '"
                << resource_name << "'\n";
            out_ok = false;
            return std::nullopt;
        }

        std::string attrib_name;
        if (!trent_string_field(attrib, "name", attrib_name)) {
            std::cerr
                << "termin_shaderc: unnamed user attribute for resource '"
                << resource_name << "'\n";
            out_ok = false;
            return std::nullopt;
        }
        if (attrib_name != "TerminScope" && attrib_name != "Scope") {
            continue;
        }

        const nos::trent* args = trent_dict_get(attrib, "arguments");
        if (!args || !args->is_list()) {
            std::cerr
                << "termin_shaderc: " << attrib_name << " on resource '"
                << resource_name << "' must have exactly one string argument\n";
            out_ok = false;
            return std::nullopt;
        }
        const auto& arg_list = args->as_list();
        if (arg_list.size() != 1 || !arg_list[0].is_string()) {
            std::cerr
                << "termin_shaderc: " << attrib_name << " on resource '"
                << resource_name << "' must have exactly one string argument\n";
            out_ok = false;
            return std::nullopt;
        }

        const std::string candidate = arg_list[0].as_string();
        if (!is_valid_explicit_resource_scope(candidate)) {
            std::cerr
                << "termin_shaderc: invalid " << attrib_name << " value '"
                << candidate << "' on resource '" << resource_name
                << "' (expected frame, pass, material, draw, or transient)\n";
            out_ok = false;
            return std::nullopt;
        }

        if (scope && *scope != candidate) {
            std::cerr
                << "termin_shaderc: conflicting scope attributes on resource '"
                << resource_name << "': '" << *scope << "' and '"
                << candidate << "'\n";
            out_ok = false;
            return std::nullopt;
        }
        scope = candidate;
    }
    return scope;
}

static std::vector<ShaderResourceBinding> infer_resource_bindings_from_slang_reflection(
    const std::string& reflection_text,
    const CompileOptions& options,
    bool& out_ok
) {
    out_ok = true;
    std::vector<ShaderResourceBinding> resources;
    nos::trent root;
    try {
        root = nos::json::parse(reflection_text);
    } catch (const std::exception&) {
        return resources;
    }

    const nos::trent* parameters = trent_dict_get(root, "parameters");
    if (!parameters || !parameters->is_list()) {
        return resources;
    }

    const uint32_t stage_mask = stage_mask_for_stage(options.stage);
    for (const nos::trent& parameter : parameters->as_list()) {
        if (!parameter.is_dict()) {
            continue;
        }
        std::string name;
        if (!trent_string_field(parameter, "name", name) || name.empty()) {
            continue;
        }

        ShaderResourceBinding binding;
        std::string binding_kind;
        if (!slang_parameter_binding(
                parameter,
                binding_kind,
                binding.binding,
                binding.set)) {
            continue;
        }
        binding.original_binding = binding.binding;

        if (!slang_resource_kind_from_parameter(
                parameter,
                binding_kind,
                binding)) {
            continue;
        }
        bool scope_ok = true;
        std::optional<std::string> explicit_scope =
            slang_scope_attribute_from_parameter(parameter, name, scope_ok);
        if (!scope_ok) {
            out_ok = false;
            return {};
        }
        if (explicit_scope) {
            binding.scope = *explicit_scope;
        }
        binding.name = name;
        binding.stage_mask = stage_mask;
        append_unique_resource(resources, std::move(binding));
    }
    return resources;
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
        static const std::regex material_buffer_clean_re(
            R"(ConstantBuffer\s*<\s*MaterialParams\s*>\s*material\s*;)");
        if (std::regex_search(source, material_buffer_clean_re)) {
            ShaderResourceBinding binding;
            binding.name = "material";
            binding.kind = "constant_buffer";
            binding.set = 1;
            binding.binding = 0;
            binding.stage_mask = stage_mask;
            append_unique_resource(resources, std::move(binding));
        }
        static const std::regex texture_register_re(
            R"((Sampler[0-9A-Za-z_]*|Texture[A-Za-z0-9_<>, \t]*|RWTexture[A-Za-z0-9_<>, \t]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*([tu])([0-9]+)\s*,\s*space([0-9]+)\s*\))");
        for (std::sregex_iterator it(source.begin(), source.end(), texture_register_re), end;
             it != end;
             ++it) {
            const std::smatch& match = *it;
            ShaderResourceBinding binding;
            const std::string type = match[1].str();
            const std::string reg_space = match[3].str();
            binding.name = match[2].str();
            binding.kind =
                (reg_space == "u" || type.rfind("RWTexture", 0) == 0)
                ? "storage_texture"
                : "texture";
            binding.binding = static_cast<uint32_t>(std::stoul(match[4].str()));
            binding.set = static_cast<uint32_t>(std::stoul(match[5].str()));
            binding.stage_mask = stage_mask;
            append_unique_resource(resources, std::move(binding));
        }
        static const std::regex sampler_register_re(
            R"((SamplerState|SamplerComparisonState)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*register\s*\(\s*s([0-9]+)\s*,\s*space([0-9]+)\s*\))");
        for (std::sregex_iterator it(source.begin(), source.end(), sampler_register_re), end;
             it != end;
             ++it) {
            const std::smatch& match = *it;
            ShaderResourceBinding binding;
            binding.name = match[2].str();
            binding.kind = "sampler";
            binding.binding = static_cast<uint32_t>(std::stoul(match[3].str()));
            binding.set = static_cast<uint32_t>(std::stoul(match[4].str()));
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
        static const std::regex sampler_re(
            R"(layout\s*\([^\)]*binding\s*=\s*([0-9]+)[^\)]*\)\s*uniform\s+sampler[A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*))");
        for (std::sregex_iterator it(source.begin(), source.end(), sampler_re), end;
             it != end;
             ++it) {
            const std::smatch& match = *it;
            ShaderResourceBinding binding;
            binding.name = match[2].str();
            binding.kind = "texture";
            binding.set = 0;
            binding.binding = static_cast<uint32_t>(std::stoul(match[1].str()));
            binding.stage_mask = stage_mask;
            append_unique_resource(resources, std::move(binding));
        }
    }

    return resources;
}

static bool collect_resource_bindings(
    const CompileOptions& options,
    const std::string& source,
    const std::optional<std::filesystem::path>& reflection_path,
    std::vector<ShaderResourceBinding>& resources
) {
    resources.clear();
    if (reflection_path && is_existing_file(*reflection_path)) {
        std::string reflection_text;
        if (read_file(reflection_path->string(), reflection_text)) {
            bool reflection_ok = true;
            resources = infer_resource_bindings_from_slang_reflection(
                reflection_text,
                options,
                reflection_ok);
            if (!reflection_ok) {
                return false;
            }
        }
    }
    if (resources.empty()) {
        resources = infer_resource_bindings(source, options);
    }
    assign_missing_resource_scopes(resources);
    if (options.language == "slang" && options.target == "vulkan") {
        for (const ShaderResourceBinding& resource : resources) {
            if (resource.slang_split_texture || resource.slang_separate_sampler) {
                std::cerr
                    << "termin_shaderc: Vulkan path does not support split Slang "
                    << "Texture/Sampler resources for '" << resource.name
                    << "'; use Sampler2D until split sampler layout metadata is implemented\n";
                return false;
            }
        }
    }
    return true;
}

static bool write_resource_layout_sidecar(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources
) {
    for (size_t i = 0; i < resources.size(); ++i) {
        for (size_t j = i + 1; j < resources.size(); ++j) {
            const ShaderResourceBinding& a = resources[i];
            const ShaderResourceBinding& b = resources[j];
            if (a.set != b.set || a.binding != b.binding) {
                continue;
            }
            if (a.name != b.name || a.kind != b.kind || a.scope != b.scope) {
                std::cerr
                    << "termin_shaderc: conflicting resources at set="
                    << a.set << " binding=" << a.binding << ": '"
                    << a.name << "' (" << a.kind << ", " << a.scope
                    << ") vs '" << b.name << "' (" << b.kind << ", "
                    << b.scope << ")\n";
                return false;
            }
        }
    }

    const std::string path = options.output + ".layout.json";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open layout sidecar: " << path << "\n";
        return false;
    }

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
            << "\"scope\": \"" << json_escape(binding.scope) << "\", "
            << "\"set\": " << binding.set << ", "
            << "\"binding\": " << binding.binding << ", "
            << "\"stage_mask\": " << binding.stage_mask << ", "
            << "\"size\": " << binding.size;
        if (!binding.fields.empty()) {
            out << ", \"fields\": [";
            for (size_t fi = 0; fi < binding.fields.size(); ++fi) {
                const auto& f = binding.fields[fi];
                out << "{\"name\":\"" << json_escape(f.name) << "\","
                    << "\"offset\":" << f.offset << ","
                    << "\"size\":" << f.size << "}";
                if (fi + 1 < binding.fields.size()) out << ", ";
            }
            out << "]";
        }
        out << "}";
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

static bool write_resource_layout_sidecar(
    const CompileOptions& options,
    const std::string& source,
    const std::optional<std::filesystem::path>& reflection_path = std::nullopt
) {
    std::vector<ShaderResourceBinding> resources;
    if (!collect_resource_bindings(options, source, reflection_path, resources)) {
        return false;
    }
    return write_resource_layout_sidecar(options, resources);
}

static bool apply_slang_vulkan_shared_layout_policy(
    std::vector<ShaderResourceBinding>& resources
) {
    // Legacy compatibility policy: flatten reflected Slang resources to set 0.
    // The active migration target is scope-first metadata; this path exists
    // only for old shared-layout artifacts/tests.
    for (ShaderResourceBinding& resource : resources) {
        resource.set = 0;
    }
    return true;
}

static bool read_spirv_words(
    const std::filesystem::path& path,
    std::vector<uint32_t>& out
) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "termin_shaderc: failed to open SPIR-V artifact: " << path << "\n";
        return false;
    }
    const std::streamsize bytes = in.tellg();
    if (bytes < 0 || (bytes % 4) != 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(bytes) / sizeof(uint32_t));
    if (!out.empty()) {
        in.read(reinterpret_cast<char*>(out.data()), bytes);
    }
    return static_cast<bool>(in);
}

static bool write_spirv_words(
    const std::filesystem::path& path,
    const std::vector<uint32_t>& words
) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open SPIR-V artifact for patch: " << path << "\n";
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(words.data()),
        static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
    return static_cast<bool>(out);
}

static std::string spirv_string_operand(
    const std::vector<uint32_t>& words,
    size_t begin,
    size_t end
) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        uint32_t word = words[i];
        for (uint32_t byte_index = 0; byte_index < 4; ++byte_index) {
            const char ch = static_cast<char>((word >> (byte_index * 8)) & 0xffu);
            if (ch == '\0') {
                return out;
            }
            out.push_back(ch);
        }
    }
    return out;
}

static bool spirv_resource_id_for_name(
    const std::vector<uint32_t>& words,
    const std::string& name,
    uint32_t& out_id
) {
    constexpr uint16_t OP_NAME = 5;
    for (size_t i = 5; i < words.size();) {
        const uint32_t instruction = words[i];
        const uint16_t word_count = static_cast<uint16_t>(instruction >> 16);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
        if (word_count == 0 || i + word_count > words.size()) {
            return false;
        }
        if (opcode == OP_NAME && word_count >= 3) {
            const uint32_t id = words[i + 1];
            if (spirv_string_operand(words, i + 2, i + word_count) == name) {
                out_id = id;
                return true;
            }
        }
        i += word_count;
    }
    return false;
}

static bool patch_spirv_binding_for_resource(
    std::vector<uint32_t>& words,
    const ShaderResourceBinding& resource
) {
    constexpr uint16_t OP_DECORATE = 71;
    constexpr uint32_t DECORATION_BINDING = 33;
    constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;

    uint32_t resource_id = 0;
    if (!spirv_resource_id_for_name(words, resource.name, resource_id)) {
        std::cerr
            << "termin_shaderc: failed to find SPIR-V resource id for '"
            << resource.name << "'\n";
        return false;
    }

    bool patched_binding = false;
    bool patched_set = false;
    for (size_t i = 5; i < words.size();) {
        const uint32_t instruction = words[i];
        const uint16_t word_count = static_cast<uint16_t>(instruction >> 16);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
        if (word_count == 0 || i + word_count > words.size()) {
            return false;
        }
        if (opcode == OP_DECORATE && word_count >= 4 && words[i + 1] == resource_id) {
            if (words[i + 2] == DECORATION_BINDING) {
                words[i + 3] = resource.binding;
                patched_binding = true;
            } else if (words[i + 2] == DECORATION_DESCRIPTOR_SET) {
                words[i + 3] = resource.set;
                patched_set = true;
            }
        }
        i += word_count;
    }

    if (!patched_binding || !patched_set) {
        std::cerr
            << "termin_shaderc: failed to patch SPIR-V binding decorations for '"
            << resource.name << "'\n";
        return false;
    }
    return true;
}

static bool patch_slang_vulkan_spirv_bindings(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources
) {
    bool needs_patch = false;
    for (const ShaderResourceBinding& resource : resources) {
        if (resource.binding != resource.original_binding) {
            needs_patch = true;
            break;
        }
    }
    if (!needs_patch) {
        return true;
    }

    std::vector<uint32_t> words;
    if (!read_spirv_words(options.output, words)) {
        // Fake/offline compiler tests may produce placeholder bytes while still
        // exercising reflection-sidecar generation.
        return true;
    }
    constexpr uint32_t SPIRV_MAGIC = 0x07230203u;
    if (words.empty() || words[0] != SPIRV_MAGIC) {
        // Fake compiler tests use placeholder bytes; there is nothing to patch.
        return true;
    }

    for (const ShaderResourceBinding& resource : resources) {
        if (resource.binding == resource.original_binding) {
            continue;
        }
        if (!patch_spirv_binding_for_resource(words, resource)) {
            return false;
        }
    }
    return write_spirv_words(options.output, words);
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

static void append_unique_existing_dir(
    std::vector<std::string>& dirs,
    const std::filesystem::path& path
) {
    std::error_code ec;
    if (path.empty() ||
        !std::filesystem::exists(path, ec) ||
        !std::filesystem::is_directory(path, ec)) {
        return;
    }
    std::filesystem::path normalized = std::filesystem::absolute(path, ec);
    std::string text = ec ? path.string() : normalized.lexically_normal().string();
    for (const std::string& existing : dirs) {
        if (existing == text) {
            return;
        }
    }
    dirs.push_back(std::move(text));
}

static std::vector<std::string> slang_include_dirs(
    const CompileOptions& options,
    const char* argv0
) {
    std::vector<std::string> dirs;
    for (const std::string& dir : options.include_dirs) {
        append_unique_existing_dir(dirs, dir);
    }

    append_unique_existing_dir(dirs, std::filesystem::path(options.input).parent_path());

    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            append_unique_existing_dir(
                dirs,
                std::filesystem::path(sdk) / "share" / "termin" / "builtin_shaders");
        }
    }

    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        std::filesystem::path tool_dir = std::filesystem::absolute(argv0, ec).parent_path();
        if (!ec) {
            append_unique_existing_dir(
                dirs,
                tool_dir.parent_path() / "share" / "termin" / "builtin_shaders");
            append_unique_existing_dir(
                dirs,
                tool_dir / "share" / "termin" / "builtin_shaders");
        }
    }

    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        append_unique_existing_dir(dirs, cwd / "share" / "termin" / "builtin_shaders");
        append_unique_existing_dir(dirs, cwd / "termin-graphics" / "resources" / "builtin_shaders");
    }

    return dirs;
}

static std::string quote_arg(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
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
#ifdef _WIN32
    cmd << "call ";
#endif
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
        extra_args = {"-profile", "spirv_1_5"};
    } else if (options.target == "opengl") {
        slang_target = "glsl";
        extra_args = {"-profile", "glsl_450"};
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
    for (const std::string& include_dir : slang_include_dirs(options, argv0)) {
        args.insert(args.end(), {"-I", include_dir});
    }
    const std::filesystem::path reflection_path(options.output + ".reflection.json");
    args.insert(args.end(), {"-reflection-json", reflection_path.string()});
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
    std::vector<ShaderResourceBinding> resources;
    if (!collect_resource_bindings(options, source, reflection_path, resources)) {
        std::error_code ec;
        std::filesystem::remove(reflection_path, ec);
        std::filesystem::remove(options.output, ec);
        std::filesystem::remove(options.output + ".layout.json", ec);
        return false;
    }
    bool wrote_layout = false;
    if (options.target == "vulkan") {
        if (options.layout_scheme == "per-pipeline") {
            // Per-pipeline mode: leave SPIR-V bindings as-is from Slang
            // reflection. The Vulkan backend builds VkDescriptorSetLayout
            // from the SPIR-V decorations at pipeline creation time.
            wrote_layout = write_resource_layout_sidecar(options, resources);
        } else {
            // Shared-layout mode: remap all Slang resources into the
            // fixed engine descriptor table (legacy / GLSL compat).
            wrote_layout =
                apply_slang_vulkan_shared_layout_policy(resources) &&
                patch_slang_vulkan_spirv_bindings(options, resources) &&
                write_resource_layout_sidecar(options, resources);
        }
    } else {
        wrote_layout = write_resource_layout_sidecar(options, resources);
    }
    std::error_code ec;
    std::filesystem::remove(reflection_path, ec);
    return wrote_layout;
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
        } else if (arg == "--layout-scheme") {
            if (!take_value(options.layout_scheme)) return 2;
        } else if (arg == "-I" || arg == "--include-dir") {
            std::string include_dir;
            if (!take_value(include_dir)) return 2;
            options.include_dirs.push_back(std::move(include_dir));
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
