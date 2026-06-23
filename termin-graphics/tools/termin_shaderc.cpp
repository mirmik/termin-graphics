#include "termin_shaderc/cli.hpp"
#include "termin_shaderc/backend_patchers.hpp"

#ifdef TERMIN_SHADERC_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif
#include <tcbase/trent/json.h>
#include <tgfx/resources/tc_shader.h>
#include <tgfx2/internal/process_runner.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace termin_shaderc::internal {

static std::string slang_stage_for_stage(const std::string& stage) {
    if (stage == "vertex") return "vertex";
    if (stage == "fragment") return "fragment";
    if (stage == "geometry") return "geometry";
    if (stage == "compute") return "compute";
    return "";
}

#ifdef TERMIN_SHADERC_HAS_SHADERC
static shaderc_shader_kind shader_kind_for_stage(const std::string& stage) {
    if (stage == "vertex") return shaderc_vertex_shader;
    if (stage == "fragment") return shaderc_fragment_shader;
    if (stage == "geometry") return shaderc_geometry_shader;
    return shaderc_glsl_infer_from_source;
}
#endif

static std::optional<std::string> slang_matrix_layout_arg(const std::string& layout) {
    if (layout == "column" || layout == "col" || layout == "column-major" || layout == "col-major") {
        return "-matrix-layout-column-major";
    }
    if (layout == "row" || layout == "row-major") {
        return "-matrix-layout-row-major";
    }
    return std::nullopt;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "termin_shaderc: failed to open input: " << path << "\n";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static bool ensure_parent_directory(const std::filesystem::path& path, const char* label) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        std::cerr << "termin_shaderc: failed to create " << label
                  << " directory: " << parent.string() << ": "
                  << ec.message() << "\n";
        return false;
    }
    return true;
}

bool write_spirv(const std::string& path, const std::vector<uint32_t>& words) {
    if (!ensure_parent_directory(path, "output")) {
        return false;
    }
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

uint32_t stage_mask_for_stage(const std::string& stage) {
    if (stage == "vertex") return 1u << 0;
    if (stage == "fragment") return 1u << 1;
    if (stage == "geometry") return 1u << 2;
    if (stage == "compute") return 1u << 3;
    return 0u;
}

static std::string d3d11_register_class_for_kind(const std::string& kind) {
    if (kind == "constant_buffer" || kind == "uniform_buffer") return "b";
    if (kind == "texture") return "t";
    if (kind == "sampler") return "s";
    if (kind == "storage_buffer" || kind == "storage_texture") return "u";
    return {};
}

void assign_d3d11_register_placement(std::vector<ShaderResourceBinding>& resources) {
    uint32_t next_constant_buffer = 0;
    uint32_t next_texture = 0;
    uint32_t next_sampler = 0;
    uint32_t next_unordered_access = 0;

    for (ShaderResourceBinding& resource : resources) {
        resource.d3d11_register_class = d3d11_register_class_for_kind(resource.kind);
        if (resource.d3d11_register_class == "b") {
            resource.d3d11_register_index = next_constant_buffer++;
        } else if (resource.d3d11_register_class == "t") {
            resource.d3d11_register_index = next_texture++;
        } else if (resource.d3d11_register_class == "s") {
            resource.d3d11_register_index = next_sampler++;
        } else if (resource.d3d11_register_class == "u") {
            resource.d3d11_register_index = next_unordered_access++;
        }
    }
}

static const char* d3d11_profile_for_stage(const std::string& stage) {
    if (stage == "vertex") return "vs_5_0";
    if (stage == "fragment") return "ps_5_0";
    if (stage == "geometry") return "gs_5_0";
    if (stage == "compute") return "cs_5_0";
    return "";
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

std::string regex_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() * 2);
    for (char ch : value) {
        switch (ch) {
            case '\\':
            case '^':
            case '$':
            case '.':
            case '|':
            case '?':
            case '*':
            case '+':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
                out.push_back('\\');
                break;
            default:
                break;
        }
        out.push_back(ch);
    }
    return out;
}

static bool is_valid_explicit_resource_scope(const std::string& scope) {
    return scope == "frame" ||
           scope == "pass" ||
           scope == "material" ||
           scope == "draw" ||
           scope == "transient";
}

static bool is_unscoped_resource_scope(const std::string& scope) {
    return scope.empty() || scope == "unscoped";
}

void assign_missing_resource_scopes(
    std::vector<ShaderResourceBinding>& resources
) {
    for (ShaderResourceBinding& resource : resources) {
        if (resource.scope.empty()) {
            resource.scope = "unscoped";
        }
    }
}

void apply_default_resource_scope(
    std::vector<ShaderResourceBinding>& resources,
    const std::string& default_scope
) {
    if (default_scope.empty()) {
        return;
    }
    for (ShaderResourceBinding& resource : resources) {
        if (is_unscoped_resource_scope(resource.scope)) {
            resource.scope = default_scope;
        }
    }
}

void normalize_scope_first_binding_slots(
    std::vector<ShaderResourceBinding>& resources,
    bool normalize_transient_resources
) {
    uint32_t next_material_texture_binding = 4;
    uint32_t next_transient_resource_binding = 32;
    for (ShaderResourceBinding& resource : resources) {
        resource.set = 0;
        if (resource.kind == "constant_buffer" || resource.kind == "uniform_buffer") {
            if (resource.scope == "frame" &&
                (resource.name == "per_frame" || resource.name == "u_per_frame")) {
                resource.binding = 2;
            } else if (resource.scope == "pass" && resource.name == "lighting") {
                resource.binding = 0;
            } else if (resource.scope == "pass" && resource.name == "shadow_block") {
                resource.binding = 3;
            } else if (resource.scope == "material" && resource.name == "material") {
                resource.binding = 1;
            } else if (resource.scope == "draw" &&
                       (resource.name == "bone_block" ||
                        resource.name == "BoneBlock")) {
                resource.binding = 16;
            } else if (resource.scope == "draw") {
                resource.binding = 24;
            }
        } else if (resource.kind == "texture" && resource.scope == "pass" &&
                   (resource.name == "shadow_maps" ||
                    resource.name == "u_shadow_map" ||
                    resource.name == "u_shadow_maps")) {
            resource.binding = 8;
        } else if (resource.kind == "texture" && resource.scope == "material") {
            if (next_material_texture_binding == 8) {
                ++next_material_texture_binding;
            }
            resource.binding = next_material_texture_binding++;
        } else if (normalize_transient_resources &&
                   (resource.kind == "texture" ||
                    resource.kind == "storage_texture" ||
                    resource.kind == "sampler") &&
                   resource.scope == "transient") {
            resource.binding = next_transient_resource_binding++;
        } else if (resource.kind == "storage_buffer" && resource.scope == "draw") {
            resource.binding = 25;
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
            if (!binding.scope.empty() && binding.scope != "unknown" &&
                binding.scope != "unscoped") {
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
            if (!binding.fields.empty()) {
                existing.fields = std::move(binding.fields);
            }
            if (!binding.slang_glsl_symbol.empty()) {
                existing.slang_glsl_symbol = std::move(binding.slang_glsl_symbol);
            }
            existing.slang_combined_texture =
                existing.slang_combined_texture || binding.slang_combined_texture;
            existing.slang_split_texture =
                existing.slang_split_texture || binding.slang_split_texture;
            existing.slang_separate_sampler =
                existing.slang_separate_sampler || binding.slang_separate_sampler;
            existing.slang_storage_texture =
                existing.slang_storage_texture || binding.slang_storage_texture;
            return;
        }
    }
    resources.push_back(std::move(binding));
}

bool has_resource_named(
    const std::vector<ShaderResourceBinding>& resources,
    const std::string& name
) {
    return std::any_of(
        resources.begin(),
        resources.end(),
        [&](const ShaderResourceBinding& resource) {
            return resource.name == name;
        });
}

static void append_missing_resources(
    std::vector<ShaderResourceBinding>& resources,
    std::vector<ShaderResourceBinding> candidates
) {
    for (ShaderResourceBinding& candidate : candidates) {
        if (!has_resource_named(resources, candidate.name)) {
            resources.push_back(std::move(candidate));
        }
    }
}

bool is_identifier_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_';
}

static std::optional<std::string_view> entry_function_body(
    const std::string& source,
    const std::string& entry
) {
    if (entry.empty()) {
        return std::nullopt;
    }

    size_t search_from = 0;
    while (true) {
        const size_t name_pos = source.find(entry, search_from);
        if (name_pos == std::string::npos) {
            return std::nullopt;
        }
        const bool left_ok =
            name_pos == 0 ||
            !is_identifier_char(source[name_pos - 1]);
        const size_t name_end = name_pos + entry.size();
        const bool right_ok =
            name_end >= source.size() ||
            !is_identifier_char(source[name_end]);
        if (!left_ok || !right_ok) {
            search_from = name_end;
            continue;
        }

        const size_t params_pos = source.find('(', name_end);
        const size_t body_pos = source.find('{', name_end);
        if (body_pos == std::string::npos) {
            return std::nullopt;
        }
        if (params_pos == std::string::npos || params_pos > body_pos) {
            search_from = name_end;
            continue;
        }

        uint32_t depth = 0;
        for (size_t i = body_pos; i < source.size(); ++i) {
            if (source[i] == '{') {
                ++depth;
            } else if (source[i] == '}') {
                if (depth == 0) {
                    return std::nullopt;
                }
                --depth;
                if (depth == 0) {
                    return std::string_view(source).substr(
                        body_pos + 1,
                        i - body_pos - 1);
                }
            }
        }
        return std::nullopt;
    }
}

static bool entry_body_references_resource(
    std::string_view body,
    const std::string& name
) {
    if (name.empty()) {
        return false;
    }

    size_t search_from = 0;
    while (true) {
        const size_t pos = body.find(name, search_from);
        if (pos == std::string_view::npos) {
            return false;
        }
        const bool left_ok =
            pos == 0 ||
            !is_identifier_char(body[pos - 1]);
        const size_t end = pos + name.size();
        const bool right_ok =
            end >= body.size() ||
            !is_identifier_char(body[end]);
        if (left_ok && right_ok) {
            return true;
        }
        search_from = end;
    }
}

static void append_missing_resources_used_by_entry(
    std::vector<ShaderResourceBinding>& resources,
    std::vector<ShaderResourceBinding> candidates,
    const std::string& source,
    const std::string& entry
) {
    const std::optional<std::string_view> body =
        entry_function_body(source, entry);
    if (!body) {
        append_missing_resources(resources, std::move(candidates));
        return;
    }

    for (ShaderResourceBinding& candidate : candidates) {
        if (!has_resource_named(resources, candidate.name) &&
            entry_body_references_resource(*body, candidate.name)) {
            resources.push_back(std::move(candidate));
        }
    }
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

static std::string material_property_type_from_slang_type(const nos::trent& type) {
    if (!type.is_dict()) {
        return {};
    }

    std::string kind;
    if (!trent_string_field(type, "kind", kind)) {
        return {};
    }

    if (kind == "scalar") {
        std::string scalar_type;
        if (!trent_string_field(type, "scalarType", scalar_type)) {
            return {};
        }
        if (scalar_type == "float32" || scalar_type == "float16") return "Float";
        if (scalar_type == "int32" || scalar_type == "int16" || scalar_type == "int8") return "Int";
        if (scalar_type == "uint32" || scalar_type == "uint16" || scalar_type == "uint8") return "Int";
        if (scalar_type == "bool") return "Bool";
        return {};
    }

    if (kind == "vector") {
        uint32_t element_count = 0;
        if (!trent_uint_field(type, "elementCount", element_count)) {
            return {};
        }
        const nos::trent* element_type = trent_dict_get(type, "elementType");
        if (!element_type || !element_type->is_dict()) {
            return {};
        }
        std::string element_kind;
        std::string scalar_type;
        if (!trent_string_field(*element_type, "kind", element_kind) ||
            element_kind != "scalar" ||
            !trent_string_field(*element_type, "scalarType", scalar_type) ||
            scalar_type != "float32") {
            return {};
        }
        if (element_count == 2) return "Vec2";
        if (element_count == 3) return "Vec3";
        if (element_count == 4) return "Vec4";
        return {};
    }

    if (kind == "matrix") {
        uint32_t row_count = 0;
        uint32_t col_count = 0;
        trent_uint_field(type, "rowCount", row_count);
        trent_uint_field(type, "columnCount", col_count);
        if (row_count == 4 && col_count == 4) {
            return "Mat4";
        }
        return {};
    }

    return {};
}

static bool is_slang_texture_base_shape(const std::string& base_shape) {
    return base_shape.rfind("texture", 0) == 0;
}

static bool is_slang_storage_buffer_base_shape(const std::string& base_shape) {
    return base_shape == "structuredBuffer" ||
           base_shape == "byteAddressBuffer" ||
           base_shape == "rwStructuredBuffer" ||
           base_shape == "rwByteAddressBuffer";
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
                        const nos::trent* ft = trent_dict_get(field, "type");
                        if (ft && ft->is_dict()) {
                            f.type = material_property_type_from_slang_type(*ft);
                        }
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

    auto resource_kind_from_type = [&](
        const nos::trent& resource_type,
        ShaderResourceBinding& binding) -> bool {
        std::string base_shape;
        if (!trent_string_field(resource_type, "baseShape", base_shape)) {
            return false;
        }

        if (is_slang_storage_buffer_base_shape(base_shape)) {
            if (binding_kind != "descriptorTableSlot" &&
                binding_kind != "shaderResource") {
                return false;
            }
            binding.kind = "storage_buffer";
            binding.size = 0;
            return true;
        }

        if (!is_slang_texture_base_shape(base_shape)) {
            return false;
        }

        std::string access;
        const bool has_access = trent_string_field(resource_type, "access", access);
        if (has_access && access == "readWrite") {
            if (binding_kind != "descriptorTableSlot") {
                return false;
            }
            binding.kind = "storage_texture";
            binding.slang_storage_texture = true;
        } else {
            if (binding_kind != "shaderResource" &&
                binding_kind != "descriptorTableSlot") {
                return false;
            }
            binding.kind = "texture";
            bool combined = false;
            trent_bool_field(resource_type, "combined", combined);
            binding.slang_combined_texture = combined;
            binding.slang_split_texture = !combined;
        }
        binding.size = 0;
        return true;
    };

    if (type_kind == "resource") {
        return resource_kind_from_type(*type, out_binding);
    }

    if (type_kind == "array") {
        const nos::trent* element_type = trent_dict_get(*type, "elementType");
        std::string element_kind;
        if (!element_type || !element_type->is_dict() ||
            !trent_string_field(*element_type, "kind", element_kind) ||
            element_kind != "resource") {
            return false;
        }
        return resource_kind_from_type(*element_type, out_binding);
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
        static const std::regex bare_resource_re(
            R"REGEX((?:\[\[\s*(?:TerminScope|Scope)\s*\(\s*"([^"]+)"\s*\)\s*\]\]\s*)?(Sampler[0-9A-Za-z_]*|Texture[A-Za-z0-9_<>, \t]*|RWTexture[A-Za-z0-9_<>, \t]*|SamplerState|SamplerComparisonState)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)REGEX");
        for (std::sregex_iterator it(source.begin(), source.end(), bare_resource_re), end;
             it != end;
             ++it) {
            const std::smatch& match = *it;
            ShaderResourceBinding binding;
            const std::string scope = match[1].str();
            const std::string type = match[2].str();
            binding.name = match[3].str();
            if (!scope.empty() && is_valid_explicit_resource_scope(scope)) {
                binding.scope = scope;
            }
            if (type == "SamplerState" || type == "SamplerComparisonState") {
                binding.kind = "sampler";
                binding.slang_separate_sampler = true;
            } else if (type.rfind("RWTexture", 0) == 0) {
                binding.kind = "storage_texture";
                binding.slang_storage_texture = true;
            } else {
                binding.kind = "texture";
                binding.slang_combined_texture = type.rfind("Sampler", 0) == 0;
                binding.slang_split_texture = !binding.slang_combined_texture;
            }
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
        static const std::regex bone_block_re(
            R"(layout\s*\([^\)]*binding\s*=\s*([0-9]+)[^\)]*\)\s*uniform\s+(BoneBlock|bone_block))");
        if (std::regex_search(source, match, bone_block_re)) {
            ShaderResourceBinding binding;
            binding.name = match[2].str();
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
    std::vector<ShaderResourceBinding> source_resources =
        infer_resource_bindings(source, options);
    if (resources.empty()) {
        resources = std::move(source_resources);
    } else {
        if (options.target == "d3d11") {
            // fxc validates every resource declaration emitted by Slang, even
            // declarations used only through helper functions. Keep all source
            // resources for D3D11 instead of filtering by direct main() usage.
            append_missing_resources(resources, std::move(source_resources));
        } else {
            append_missing_resources_used_by_entry(
                resources,
                std::move(source_resources),
                source,
                options.entry);
        }
    }
    assign_missing_resource_scopes(resources);
    apply_default_resource_scope(resources, options.default_scope);
    normalize_scope_first_binding_slots(
        resources,
        options.language == "slang");
    if (options.target == "d3d11") {
        assign_d3d11_register_placement(resources);
    }
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
    // --- Validate resource layout before writing sidecar ---
    // Vulkan limits: MaxUniformBufferRange typically 16KB (65536),
    // MaxDescriptorSetUniformBuffers typically 120.
    const uint32_t max_ubo_size = 65536;  // 64KB
    const uint32_t max_binding = 1024;     // reasonable upper bound

    for (const auto& res : resources) {
        // UBO size limit
        if (res.kind == "uniform_buffer" && res.size > max_ubo_size) {
            std::cerr
                << "termin_shaderc: UBO '" << res.name
                << "' size " << res.size << " exceeds limit " << max_ubo_size
                << " bytes; shader will likely fail validation on GPU\n";
            return false;
        }
        // Binding range
        if (res.binding > max_binding) {
            std::cerr
                << "termin_shaderc: resource '" << res.name
                << "' binding=" << res.binding
                << " exceeds reasonable limit " << max_binding
                << "; check [[VulkanSet]] / [[VulkanBinding]] attributes\n";
            return false;
        }
        // Scope consistency — warn if unknown
        if (res.scope.empty() || res.scope == "unknown" || res.scope == "unscoped") {
            std::string attr = std::string("[") + "[TerminScope]]";
            std::cerr
                << "termin_shaderc: WARNING — resource '" << res.name
                << "' has "
                << (res.scope == "unscoped" ? "no scope" : "unknown scope")
                << "; consider adding " << attr
                << " attribute or passing --default-scope\n";
        }
    }

    if (options.target == "d3d11") {
        for (const ShaderResourceBinding& res : resources) {
            if (res.d3d11_register_class.empty()) {
                std::cerr
                    << "termin_shaderc: resource '" << res.name
                    << "' kind '" << res.kind
                    << "' has no D3D11 register class mapping\n";
                return false;
            }
        }
        for (size_t i = 0; i < resources.size(); ++i) {
            for (size_t j = i + 1; j < resources.size(); ++j) {
                const ShaderResourceBinding& a = resources[i];
                const ShaderResourceBinding& b = resources[j];
                if (a.d3d11_register_class != b.d3d11_register_class ||
                    a.d3d11_register_index != b.d3d11_register_index ||
                    (a.stage_mask & b.stage_mask) == 0) {
                    continue;
                }
                if (a.name != b.name || a.kind != b.kind || a.scope != b.scope) {
                    std::cerr
                        << "termin_shaderc: conflicting D3D11 resources at register("
                        << a.d3d11_register_class << a.d3d11_register_index
                        << "): '" << a.name << "' (" << a.kind << ", " << a.scope
                        << ") vs '" << b.name << "' (" << b.kind << ", "
                        << b.scope << ")\n";
                    return false;
                }
            }
        }
    } else {
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
    }

    const std::string path = options.output + ".layout.json";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open layout sidecar: " << path << "\n";
        return false;
    }

    const uint32_t sidecar_version = options.target == "d3d11" ? 2u : 1u;
    out << "{\n";
    out << "  \"version\": " << sidecar_version << ",\n";
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
        if (options.target == "d3d11") {
            out << ", \"d3d11\": {"
                << "\"register_class\": \"" << json_escape(binding.d3d11_register_class) << "\", "
                << "\"register_index\": " << binding.d3d11_register_index
                << "}";
        }
        if (!binding.fields.empty()) {
            out << ", \"fields\": [";
            for (size_t fi = 0; fi < binding.fields.size(); ++fi) {
                const auto& f = binding.fields[fi];
                out << "{\"name\":\"" << json_escape(f.name) << "\","
                    << "\"type\":\"" << json_escape(f.type) << "\","
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

#ifdef _WIN32
static std::optional<std::string> find_windows_sdk_fxc() {
    std::vector<std::filesystem::path> roots;
    if (const char* program_files_x86 = std::getenv("ProgramFiles(x86)")) {
        if (program_files_x86[0] != '\0') {
            roots.emplace_back(
                std::filesystem::path(program_files_x86) / "Windows Kits" / "10" / "bin");
        }
    }
    roots.emplace_back("C:/Program Files (x86)/Windows Kits/10/bin");

    std::vector<std::filesystem::path> candidates;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::filesystem::path candidate = entry.path() / "x64" / "fxc.exe";
            if (is_existing_file(candidate)) {
                candidates.push_back(candidate);
            }
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates.back().string();
}
#endif

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

static std::optional<std::string> resolve_fxc(const CompileOptions& options, const char* argv0) {
    if (!options.fxc.empty()) {
        if (!is_existing_file(options.fxc)) {
            std::cerr << "termin_shaderc: fxc does not exist: " << options.fxc << "\n";
            return std::nullopt;
        }
        return options.fxc;
    }

    if (const char* env = std::getenv("TERMIN_FXC")) {
        if (env[0] != '\0') {
            if (!is_existing_file(env)) {
                std::cerr << "termin_shaderc: TERMIN_FXC points to missing fxc: " << env << "\n";
                return std::nullopt;
            }
            return std::string(env);
        }
    }

    if (auto found = find_on_path("fxc")) {
        return found;
    }
    if (auto found = find_sdk_tool("fxc", argv0)) {
        return found;
    }
#ifdef _WIN32
    if (auto found = find_windows_sdk_fxc()) {
        return found;
    }
#endif

    std::cerr
        << "termin_shaderc: fxc not found. Set TERMIN_FXC, add fxc to PATH, "
        << "install it under TERMIN_SDK/bin, or install the Windows SDK.\n";
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

static int run_command(const std::vector<std::string>& args) {
    tgfx::internal::ProcessResult result = tgfx::internal::run_process(args, true);
    if (!result.start_error.empty()) {
        std::cerr << "termin_shaderc: failed to run "
                  << (args.empty() ? "<empty>" : args[0])
                  << ": " << result.start_error << "\n";
    }
    if (!result.output.empty()) {
        std::cerr << result.output;
    }
    return result.exit_code;
}

static bool compile_glsl_to_vulkan(const CompileOptions& options) {
#ifndef TERMIN_SHADERC_HAS_SHADERC
    (void)options;
    std::cerr
        << "termin_shaderc: GLSL to Vulkan compilation requires shaderc support, "
        << "but this termin_shaderc was built without shaderc\n";
    return false;
#else
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
#endif
}

static bool compile_slang(const CompileOptions& options, const char* argv0) {
    std::string slang_stage = slang_stage_for_stage(options.stage);
    if (slang_stage.empty()) {
        std::cerr << "termin_shaderc: unsupported stage: " << options.stage << "\n";
        return false;
    }

    std::string slang_target;
    std::vector<std::string> extra_args;
    std::string d3d11_profile;
    if (options.target == "vulkan") {
        slang_target = "spirv";
        extra_args = {"-profile", "spirv_1_5"};
    } else if (options.target == "opengl") {
        slang_target = "glsl";
        extra_args = {"-profile", "glsl_450"};
    } else if (options.target == "d3d11") {
        slang_target = "hlsl";
        d3d11_profile = d3d11_profile_for_stage(options.stage);
        if (d3d11_profile.empty()) {
            std::cerr << "termin_shaderc: unsupported D3D11 stage: " << options.stage << "\n";
            return false;
        }
        extra_args = {"-profile", "sm_5_0"};
    } else {
        std::cerr << "termin_shaderc: unsupported target: " << options.target << "\n";
        return false;
    }

    auto slangc = resolve_slangc(options, argv0);
    if (!slangc) {
        return false;
    }
    std::optional<std::string> fxc;
    if (options.target == "d3d11") {
        fxc = resolve_fxc(options, argv0);
        if (!fxc) {
            return false;
        }
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
    const bool is_d3d11 = options.target == "d3d11";
    const std::filesystem::path slang_output_path(
        is_d3d11 ? options.output + ".hlsl" : options.output);
    const std::filesystem::path reflection_path(options.output + ".reflection.json");
    if (!ensure_parent_directory(slang_output_path, "Slang output") ||
        !ensure_parent_directory(reflection_path, "Slang reflection") ||
        !ensure_parent_directory(options.output, "artifact output")) {
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
    args.insert(args.end(), {"-reflection-json", reflection_path.string()});
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    args.insert(args.end(), {"-o", slang_output_path.string()});

    int rc = run_command(args);
    if (rc != 0) {
        std::cerr << "termin_shaderc: slangc failed with exit code " << rc << "\n";
        return false;
    }

    if (!is_existing_file(slang_output_path)) {
        std::cerr << "termin_shaderc: slangc did not produce expected output: "
                  << slang_output_path.string() << "\n";
        return false;
    }
    std::vector<ShaderResourceBinding> resources;
    if (!collect_resource_bindings(options, source, reflection_path, resources)) {
        std::error_code ec;
        std::filesystem::remove(reflection_path, ec);
        std::filesystem::remove(options.output, ec);
        std::filesystem::remove(options.output + ".layout.json", ec);
        if (is_d3d11 && !std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
            std::filesystem::remove(slang_output_path, ec);
        }
        return false;
    }
    if (is_d3d11) {
        if (!augment_d3d11_resource_bindings_from_hlsl(
                options,
                slang_output_path,
                resources)) {
            std::error_code ec;
            std::filesystem::remove(options.output, ec);
            std::filesystem::remove(options.output + ".layout.json", ec);
            if (!std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
                std::filesystem::remove(slang_output_path, ec);
            }
            std::filesystem::remove(reflection_path, ec);
            return false;
        }
        if (!patch_slang_d3d11_hlsl_resource_bindings(
                slang_output_path,
                resources)) {
            std::error_code ec;
            std::filesystem::remove(options.output, ec);
            std::filesystem::remove(options.output + ".layout.json", ec);
            if (!std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
                std::filesystem::remove(slang_output_path, ec);
            }
            std::filesystem::remove(reflection_path, ec);
            return false;
        }
        std::vector<std::string> fxc_args = {
            *fxc,
            "/nologo",
            "/T", d3d11_profile,
            "/E", options.entry,
            "/Fo", options.output,
            slang_output_path.string(),
        };
        rc = run_command(fxc_args);
        if (rc != 0) {
            std::cerr << "termin_shaderc: fxc failed with exit code " << rc << "\n";
            std::error_code ec;
            std::filesystem::remove(options.output, ec);
            std::filesystem::remove(options.output + ".layout.json", ec);
            if (!std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
                std::filesystem::remove(slang_output_path, ec);
            }
            std::filesystem::remove(reflection_path, ec);
            return false;
        }
        if (!is_existing_file(options.output)) {
            std::cerr << "termin_shaderc: fxc did not produce expected output: "
                      << options.output << "\n";
            std::error_code ec;
            if (!std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
                std::filesystem::remove(slang_output_path, ec);
            }
            std::filesystem::remove(reflection_path, ec);
            return false;
        }
    }
    bool wrote_layout = false;
    if (options.target == "vulkan") {
        // Slang reflection is stage-local, so resources from separate vertex
        // and fragment compiles can collide. Normalize both the sidecar and
        // SPIR-V descriptor decorations to the same scope-first placement.
        wrote_layout =
            filter_slang_vulkan_resources_for_spirv(options, resources) &&
            patch_slang_vulkan_spirv_descriptor_decorations(options, resources) &&
            write_resource_layout_sidecar(options, resources);
    } else if (options.target == "opengl") {
        annotate_slang_glsl_symbols(resources, source);
        wrote_layout =
            legalize_slang_opengl_glsl_builtins(options) &&
            filter_slang_opengl_resources_for_glsl(options, resources) &&
            patch_slang_opengl_glsl_resource_bindings(options, resources) &&
            write_resource_layout_sidecar(options, resources);
    } else if (options.target == "d3d11") {
        wrote_layout = write_resource_layout_sidecar(options, resources);
    } else {
        wrote_layout = write_resource_layout_sidecar(options, resources);
    }
    std::error_code ec;
    std::filesystem::remove(reflection_path, ec);
    if (is_d3d11 && !std::getenv("TERMIN_SHADERC_KEEP_INTERMEDIATE")) {
        std::filesystem::remove(slang_output_path, ec);
    }
    return wrote_layout;
}

} // namespace termin_shaderc::internal

int main(int argc, char** argv) {
    termin_shaderc::ParsedCommandLine parsed =
        termin_shaderc::parse_command_line(argc, argv);
    if (!parsed.should_compile) {
        return parsed.exit_code;
    }
    const termin_shaderc::CompileOptions& options = parsed.options;

    if (options.language == "glsl") {
        return termin_shaderc::internal::compile_glsl_to_vulkan(options) ? 0 : 1;
    }
    if (options.language == "slang") {
        return termin_shaderc::internal::compile_slang(options, argv[0]) ? 0 : 1;
    }

    std::cerr << "termin_shaderc: unsupported language: " << options.language << "\n";
    return 2;
}
