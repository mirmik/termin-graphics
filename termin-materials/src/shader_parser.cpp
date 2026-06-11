#include <termin/materials/shader_parser.hpp>

#include <tcbase/tc_log.hpp>

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <utility>

// Use tgfx2's shared include-resolution hook so the parser's strip /
// inject passes see the full expanded source — including content
// pulled in from lighting.glsl / shadows.glsl. Without this, plain
// `uniform mat4 u_view;` decls hiding inside included .glsl files
// slip past the engine-uniforms strip and break Vulkan compilation.
#include "tgfx2/internal/shader_preprocess.hpp"

namespace termin {

namespace {

// Trim whitespace from both ends
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Convert to lowercase
std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Check if string starts with prefix
bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// Split string by whitespace
std::vector<std::string> split_whitespace(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string word;
    while (iss >> word) {
        result.push_back(word);
    }
    return result;
}

// Parse property value from string
MaterialProperty::DefaultValue parse_property_value(
    const std::string& value_str,
    const std::string& property_type
) {
    std::string val = trim(value_str);

    if (property_type == "Float") {
        return std::stod(val);
    }
    else if (property_type == "Int") {
        return std::stoi(val);
    }
    else if (property_type == "Bool") {
        return parse_bool(val);
    }
    else if (property_type == "Vec2" || property_type == "Vec3" ||
             property_type == "Vec4" || property_type == "Color") {
        // Parse: Color(1.0, 0.5, 0.0, 1.0) or Vec3(1, 2, 3) or [1.0, 0.5, 0.0, 1.0] or "1.0 0.5 0.0"
        std::vector<double> values;

        // Try constructor format: Type(...)
        std::regex ctor_regex(R"(\w+\s*\(\s*([^)]+)\s*\))");
        std::smatch match;
        std::string inner;

        if (std::regex_search(val, match, ctor_regex)) {
            inner = match[1].str();
        } else {
            inner = val;
        }

        // Parse comma or space separated values, ignoring brackets
        std::string num;
        for (char c : inner) {
            if (c == ',' || c == ' ' || c == '\t' || c == '[' || c == ']') {
                if (!num.empty()) {
                    values.push_back(std::stod(trim(num)));
                    num.clear();
                }
            } else {
                num += c;
            }
        }
        if (!num.empty()) {
            values.push_back(std::stod(trim(num)));
        }

        // Ensure correct size
        size_t expected = 0;
        if (property_type == "Vec2") expected = 2;
        else if (property_type == "Vec3") expected = 3;
        else expected = 4;  // Vec4 or Color

        while (values.size() < expected) {
            values.push_back(property_type == "Color" ? 1.0 : 0.0);
        }

        return values;
    }
    else if (property_type == "Texture") {
        // Remove quotes if present
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            return val.substr(1, val.size() - 2);
        }
        return val.empty() ? MaterialProperty::DefaultValue{std::monostate{}} :
                             MaterialProperty::DefaultValue{val};
    }

    throw std::runtime_error("Unknown property type: " + property_type);
}

// Get default value for property type
MaterialProperty::DefaultValue get_default_for_type(const std::string& type) {
    if (type == "Float") return 0.0;
    if (type == "Int") return 0;
    if (type == "Bool") return false;
    if (type == "Vec2") return std::vector<double>{0.0, 0.0};
    if (type == "Vec3") return std::vector<double>{0.0, 0.0, 0.0};
    if (type == "Vec4") return std::vector<double>{0.0, 0.0, 0.0, 0.0};
    if (type == "Color") return std::vector<double>{1.0, 1.0, 1.0, 1.0};
    if (type == "Mat4") {
        // 4x4 identity, column-major like Mat44f storage.
        return std::vector<double>{
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        };
    }
    if (type == "Texture") return std::monostate{};
    throw std::runtime_error("Unknown property type: " + type);
}

} // anonymous namespace


bool parse_bool(const std::string& value) {
    std::string low = to_lower(trim(value));
    if (low == "true" || low == "1" || low == "yes" || low == "on") {
        return true;
    }
    if (low == "false" || low == "0" || low == "no" || low == "off") {
        return false;
    }
    throw std::runtime_error("Cannot parse as bool: " + value);
}


// ========== std140 Material UBO generator ==========

std::pair<uint32_t, uint32_t> std140_size_align(const std::string& property_type) {
    // Scalars.
    if (property_type == "Float" || property_type == "Int" || property_type == "Bool") {
        return {4u, 4u};
    }
    // Two-component vector.
    if (property_type == "Vec2") {
        return {8u, 8u};
    }
    // Three-component vector: data size 12, but base alignment is 16.
    if (property_type == "Vec3") {
        return {12u, 16u};
    }
    // Four-component vectors.
    if (property_type == "Vec4" || property_type == "Color") {
        return {16u, 16u};
    }
    // 4x4 matrix — four column vec4s, base alignment 16.
    if (property_type == "Mat4") {
        return {64u, 16u};
    }
    // Texture / anything else: not in UBO.
    return {0u, 0u};
}

static uint32_t round_up(uint32_t value, uint32_t align) {
    if (align == 0) return value;
    return (value + align - 1u) & ~(align - 1u);
}

MaterialUboLayout compute_std140_layout(const std::vector<MaterialProperty>& properties) {
    MaterialUboLayout layout;
    uint32_t cursor = 0;

    for (const auto& prop : properties) {
        auto [size, align] = std140_size_align(prop.property_type);
        if (size == 0) continue;  // Texture / unknown — skip.

        cursor = round_up(cursor, align);

        MaterialUboEntry entry;
        entry.name = prop.name;
        entry.property_type = prop.property_type;
        entry.offset = cursor;
        entry.size = size;
        layout.entries.push_back(std::move(entry));

        cursor += size;
    }

    // Whole block rounds up to 16-byte boundary per std140.
    layout.block_size = round_up(cursor, 16u);
    return layout;
}

std::string synthesize_material_ubo_glsl(const MaterialUboLayout& layout) {
    if (layout.empty()) return "";

    auto glsl_type = [](const std::string& prop_type) -> const char* {
        if (prop_type == "Float") return "float";
        if (prop_type == "Int")   return "int";
        if (prop_type == "Bool")  return "bool";
        if (prop_type == "Vec2")  return "vec2";
        if (prop_type == "Vec3")  return "vec3";
        if (prop_type == "Vec4")  return "vec4";
        if (prop_type == "Color") return "vec4";
        if (prop_type == "Mat4")  return "mat4";
        return nullptr;
    };

    // Binding 1 matches MATERIAL_UBO_BINDING in ColorPass's C++ side —
    // apply_material_phase_ubo calls bind_uniform_buffer(1, ubo). On
    // Vulkan the binding is prescribed here and baked into SPIR-V, since
    // set_block_binding is a GL-only escape hatch that no-ops on Vulkan.
    // GL 4.3+ accepts the qualifier via core GL_ARB_shading_language_420pack
    // and glBindBufferBase(GL_UNIFORM_BUFFER, 1, ...) lands on the same
    // slot without needing glUniformBlockBinding.
    std::ostringstream out;
    out << "layout(std140, binding = 1) uniform MaterialParams {\n";
    for (const auto& e : layout.entries) {
        const char* t = glsl_type(e.property_type);
        if (!t) continue;
        out << "    " << t << " " << e.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string synthesize_material_ubo_slang(const MaterialUboLayout& layout) {
    if (layout.empty()) return "";

    auto slang_type = [](const std::string& prop_type) -> const char* {
        if (prop_type == "Float") return "float";
        if (prop_type == "Int")   return "int";
        // Runtime packing writes Bool as a 32-bit 0/1 integer. Keep the Slang
        // ABI explicit instead of depending on backend bool buffer layout.
        if (prop_type == "Bool")  return "int";
        if (prop_type == "Vec2")  return "float2";
        if (prop_type == "Vec3")  return "float3";
        if (prop_type == "Vec4")  return "float4";
        if (prop_type == "Color") return "float4";
        if (prop_type == "Mat4")  return "column_major float4x4";
        return nullptr;
    };

    std::ostringstream out;
    out << "struct MaterialParams {\n";
    for (const auto& e : layout.entries) {
        const char* t = slang_type(e.property_type);
        if (!t) continue;
        out << "    " << t << " " << e.name << ";\n";
    }
    out << "};\n";
    out << "[[TerminScope(\"material\")]]\n";
    out << "ConstantBuffer<MaterialParams> material;\n";
    return out.str();
}

std::vector<std::string> collect_texture_properties(const std::vector<MaterialProperty>& properties) {
    std::vector<std::string> names;
    for (const auto& prop : properties) {
        if (prop.property_type == "Texture" || prop.property_type == "Texture2D") {
            names.push_back(prop.name);
        }
    }
    return names;
}

std::string strip_sampler_decls(
    const std::string& source,
    const std::vector<std::string>& sampler_names
) {
    if (sampler_names.empty()) return source;

    std::vector<std::regex> res;
    res.reserve(sampler_names.size());
    for (const auto& name : sampler_names) {
        std::string pattern =
            std::string("[ \\t]*(layout[ \\t]*\\([^)]*\\)[ \\t]*)?")
            + "uniform[ \\t]+sampler[A-Za-z0-9_]*[ \\t]+"
            + name + "[ \\t]*;[ \\t]*";
        res.emplace_back(pattern);
    }

    std::string result;
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t line_end = (eol == std::string::npos) ? source.size() : eol;
        std::string line = source.substr(i, line_end - i);
        bool drop = false;
        for (const auto& re : res) {
            if (std::regex_match(line, re)) {
                drop = true;
                break;
            }
        }
        if (!drop) {
            result.append(line);
            if (eol != std::string::npos) result.push_back('\n');
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return result;
}

std::string strip_slang_sampler_decls(
    const std::string& source,
    const std::vector<std::string>& sampler_names
) {
    if (sampler_names.empty()) return source;

    std::vector<std::regex> res;
    res.reserve(sampler_names.size());
    for (const auto& name : sampler_names) {
        std::string pattern =
            std::string("[ \\t]*Sampler[0-9A-Za-z_]*[ \\t]+")
            + name + "[ \\t]*;[ \\t]*";
        res.emplace_back(pattern);
    }

    std::string result;
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t line_end = (eol == std::string::npos) ? source.size() : eol;
        std::string line = source.substr(i, line_end - i);
        bool drop = false;
        for (const auto& re : res) {
            if (std::regex_match(line, re)) {
                drop = true;
                break;
            }
        }
        if (!drop) {
            result.append(line);
            if (eol != std::string::npos) result.push_back('\n');
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return result;
}

bool source_uses_identifier(const std::string& source, const std::string& name) {
    std::regex re(std::string("\\b") + name + "\\b");
    return std::regex_search(source, re);
}

bool slang_source_has_prelude_import(const std::string& source) {
    static const std::regex re(R"(\bimport\s+termin_prelude\s*;)");
    return std::regex_search(source, re);
}

std::string ensure_slang_prelude_import(std::string source) {
    if (source.find("TerminScope(") == std::string::npos ||
        slang_source_has_prelude_import(source)) {
        return source;
    }
    return "import termin_prelude;\n" + source;
}

std::string synthesize_material_sampler_glsl(
    const std::vector<std::string>& texture_names,
    const std::string& stage_source
) {
    if (texture_names.empty()) return "";

    std::ostringstream out;
    for (size_t i = 0; i < texture_names.size(); ++i) {
        const std::string& name = texture_names[i];
        if (!source_uses_identifier(stage_source, name)) {
            continue;
        }
        out << "layout(binding = " << material_texture_binding_for_index(static_cast<uint32_t>(i))
            << ") uniform sampler2D " << name << ";\n";
    }
    return out.str();
}

std::string synthesize_material_sampler_slang(
    const std::vector<std::string>& texture_names,
    const std::string& stage_source
) {
    if (texture_names.empty()) return "";

    std::ostringstream out;
    for (const std::string& name : texture_names) {
        if (!source_uses_identifier(stage_source, name)) {
            continue;
        }
        out << "[[TerminScope(\"material\")]]\n";
        out << "Sampler2D " << name << ";\n";
    }
    return out.str();
}

bool is_engine_uniform_name(const std::string& name);

std::optional<std::string> material_type_from_glsl_uniform_type(
    const std::string& glsl_type)
{
    if (glsl_type == "float") return "Float";
    if (glsl_type == "int") return "Int";
    if (glsl_type == "bool") return "Bool";
    if (glsl_type == "vec2") return "Vec2";
    if (glsl_type == "vec3") return "Vec3";
    if (glsl_type == "vec4") return "Vec4";
    if (glsl_type == "mat4") return "Mat4";
    if (glsl_type == "sampler2D") return "Texture";
    return std::nullopt;
}

std::vector<MaterialProperty> discover_material_uniforms_from_stages(
    const ShaderPhase& phase,
    const std::vector<MaterialProperty>& material_properties)
{
    std::vector<MaterialProperty> discovered;
    std::vector<std::string> known_names;

    for (const auto& prop : material_properties) {
        known_names.push_back(prop.name);
    }
    for (const auto& prop : phase.uniforms) {
        known_names.push_back(prop.name);
    }

    auto known = [&](const std::string& name) {
        return std::find(known_names.begin(), known_names.end(), name) != known_names.end();
    };

    std::regex uniform_re(
        R"(^[ \t]*(?:layout[ \t]*\([^)]*\)[ \t]*)?uniform[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([A-Za-z_][A-Za-z0-9_]*)(?:[ \t]*\[[^\]]+\])?[ \t]*;[ \t]*(?://.*)?$)");

    for (const auto& kv : phase.stages) {
        std::istringstream lines(kv.second.source);
        std::string line;
        while (std::getline(lines, line)) {
            std::smatch match;
            if (!std::regex_match(line, match, uniform_re)) {
                continue;
            }

            std::string glsl_type = match[1].str();
            std::string name = match[2].str();
            if (known(name) || is_engine_uniform_name(name)) {
                continue;
            }

            auto material_type = material_type_from_glsl_uniform_type(glsl_type);
            if (!material_type.has_value()) {
                continue;
            }

            known_names.push_back(name);
            discovered.emplace_back(
                name,
                material_type.value(),
                get_default_for_type(material_type.value()));
        }
    }

    return discovered;
}

std::vector<MaterialProperty> collect_used_material_properties(
    const std::vector<MaterialProperty>& material_properties,
    const ShaderPhase& phase)
{
    std::vector<MaterialProperty> used;
    for (const auto& prop : material_properties) {
        for (const auto& kv : phase.stages) {
            if (source_uses_identifier(kv.second.source, prop.name)) {
                used.push_back(prop);
                break;
            }
        }
    }
    return used;
}

bool contains_slang_material_params_declaration(const ShaderPhase& phase) {
    for (const auto& kv : phase.stages) {
        const std::string& src = kv.second.source;
        if (src.find("struct MaterialParams") != std::string::npos ||
            src.find("ConstantBuffer<MaterialParams>") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool stage_uses_material_ubo_layout(
    const ShaderStage& stage,
    const MaterialUboLayout& layout)
{
    for (const auto& entry : layout.entries) {
        if (source_uses_identifier(stage.source, entry.name)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Engine uniforms auto-substitution
// ============================================================================
//
// Legacy shaders declare the engine-provided per-frame / per-draw data
// as plain `uniform mat4 u_view;` etc. — fine on GL 3.3, but SPIR-V
// disallows non-opaque uniforms outside a block. Transform the stage
// source so that:
//
//   - `uniform <type> <name>;` decls for known engine names are stripped
//   - a PerFrame UBO (binding 2, matches ColorPass's per_frame_ubo_) is
//     injected with view / projection / view_projection / camera_position
//   - a push_constant block with `u_model` is injected (on Vulkan; on GL
//     it's a std140 emulation UBO at TGFX2_PUSH_CONSTANTS_BINDING = 14)
//   - `#define u_model pc.u_model` so stage bodies keep writing
//     `u_model * vec4(pos, 1.0)` without manual rewrite
//
// This is the minimal substitution needed to get stdlib materials
// (TestMaterialUbo, BlinnPhong, CookTorrancePBR, …) to compile for
// Vulkan without touching their .shader files. The list of recognised
// names is deliberately closed — shader-specific uniforms that aren't
// wired into the engine remain the shader author's responsibility.
const char* const ENGINE_PLAIN_UNIFORM_NAMES[] = {
    "u_model",
    "u_view",
    "u_projection",
    "u_view_projection",
    "u_inv_view",
    "u_inv_proj",
    "u_camera_position",
    "u_resolution",
    "u_near",
    "u_far",
};

bool is_engine_uniform_name(const std::string& name) {
    for (const char* engine_name : ENGINE_PLAIN_UNIFORM_NAMES) {
        if (name == engine_name) {
            return true;
        }
    }
    return false;
}

std::string strip_engine_uniform_decls(const std::string& source) {
    std::vector<std::regex> res;
    for (const char* name : ENGINE_PLAIN_UNIFORM_NAMES) {
        std::string pattern =
            std::string("[ \\t]*uniform[ \\t]+[A-Za-z_][A-Za-z0-9_]*[ \\t]+") +
            name + "[ \\t]*;[ \\t]*";
        res.emplace_back(pattern);
    }

    std::string result;
    result.reserve(source.size());
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t line_end = (eol == std::string::npos) ? source.size() : eol;
        std::string line = source.substr(i, line_end - i);
        bool drop = false;
        for (const auto& re : res) {
            if (std::regex_match(line, re)) { drop = true; break; }
        }
        if (!drop) {
            result.append(line);
            if (eol != std::string::npos) result.push_back('\n');
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return result;
}

// Engine-supplied per-frame block. Inject this only when a stage declares
// legacy plain per-frame uniforms. Do not use identifier-only scans here:
// fields such as `struct IdPushData { mat4 u_model; }` are not engine
// uniforms and must not trigger macro injection.
const char* ENGINE_PER_FRAME_BLOCK = R"(
layout(std140, binding = 2) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
    mat4 u_view_projection;
    mat4 u_inv_view;
    mat4 u_inv_proj;
    vec4 u_camera_position;
    vec2 u_resolution;
    float u_near;
    float u_far;
};
)";

// Engine-supplied per-draw block. `u_model` is aliased via `#define` so
// existing stage bodies keep using the plain identifier and don't need to know
// about the push_constant indirection. This block is intentionally separate
// from PerFrame: the macro is only safe when the source actually declared a
// top-level legacy `uniform mat4 u_model;`.
const char* ENGINE_MODEL_PUSH_BLOCK = R"(

struct ColorPushData {
    mat4 _u_model;
};
#ifdef VULKAN
layout(push_constant) uniform ColorPushBlock { ColorPushData pc; };
#else
layout(std140, binding = 14) uniform ColorPushBlock { ColorPushData pc; };
#endif
#define u_model pc._u_model
)";

struct EngineUniformDeclUsage {
    bool per_frame = false;
    bool model = false;

    bool any() const { return per_frame || model; }
};

bool is_per_frame_engine_uniform_name(const std::string& name) {
    return name == "u_view" ||
           name == "u_projection" ||
           name == "u_view_projection" ||
           name == "u_inv_view" ||
           name == "u_inv_proj" ||
           name == "u_camera_position" ||
           name == "u_resolution" ||
           name == "u_near" ||
           name == "u_far";
}

bool stage_uses_per_frame_engine_uniform(const std::string& source) {
    for (const char* name : ENGINE_PLAIN_UNIFORM_NAMES) {
        if (!is_per_frame_engine_uniform_name(name)) {
            continue;
        }
        std::regex re(std::string("\\b") + name + "\\b");
        if (std::regex_search(source, re)) {
            return true;
        }
    }
    return false;
}

EngineUniformDeclUsage collect_engine_uniform_usage(const std::string& source) {
    EngineUniformDeclUsage usage;
    std::regex uniform_re(
        R"(^[ \t]*(?:layout[ \t]*\([^)]*\)[ \t]*)?uniform[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]+([A-Za-z_][A-Za-z0-9_]*)(?:[ \t]*\[[^\]]+\])?[ \t]*;[ \t]*(?://.*)?$)");

    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (!std::regex_match(line, match, uniform_re)) {
            continue;
        }

        std::string name = match[1].str();
        if (name == "u_model") {
            usage.model = true;
        } else if (is_per_frame_engine_uniform_name(name)) {
            usage.per_frame = true;
        }
    }

    // Stdlib helper code such as shadows.glsl uses `u_view` but intentionally
    // does not redeclare it. PerFrame has no macro side effects, so usage-based
    // injection is safe for these names. Keep `u_model` declaration-based only:
    // injecting its macro on identifier use corrupts user push-constant structs
    // with fields named `u_model`.
    if (!usage.per_frame && stage_uses_per_frame_engine_uniform(source)) {
        usage.per_frame = true;
    }

    return usage;
}

std::string synthesize_engine_uniform_glsl(const EngineUniformDeclUsage& usage) {
    std::string block;
    if (usage.per_frame) {
        block += ENGINE_PER_FRAME_BLOCK;
    }
    if (usage.model) {
        block += ENGINE_MODEL_PUSH_BLOCK;
    }
    return block;
}

// Slang equivalents of the engine uniform blocks — same data, same bindings,
// only the syntax differs (ConstantBuffer<T>).
const char* ENGINE_PER_FRAME_BLOCK_SLANG = R"(
struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    column_major float4x4 u_view_projection;
    column_major float4x4 u_inv_view;
    column_major float4x4 u_inv_proj;
    float4 u_camera_position;
    float2 u_resolution;
    float u_near;
    float u_far;
};
[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;
)";

const char* ENGINE_MODEL_PUSH_BLOCK_SLANG = R"(
struct DrawData {
    column_major float4x4 _u_model;
};
[[TerminScope("draw")]]
ConstantBuffer<DrawData> draw_data;
#define u_model draw_data._u_model
)";

std::string synthesize_engine_uniform_slang(const EngineUniformDeclUsage& usage) {
    std::string block;
    if (usage.per_frame) {
        block += ENGINE_PER_FRAME_BLOCK_SLANG;
    }
    if (usage.model) {
        block += ENGINE_MODEL_PUSH_BLOCK_SLANG;
    }
    return block;
}

std::string strip_uniform_decls(const std::string& source,
                                const std::vector<std::string>& names) {
    if (names.empty()) return source;

    // Match simple top-level declarations: `uniform <type> <name>;` where
    // name is one of the provided. Use a per-name regex applied per line,
    // since MSVC STL lacks std::regex::multiline.
    std::vector<std::regex> res;
    res.reserve(names.size());
    for (const auto& name : names) {
        std::string pattern =
            std::string("[ \\t]*uniform[ \\t]+[A-Za-z_][A-Za-z0-9_]*[ \\t]+") +
            name + "[ \\t]*;[ \\t]*";
        res.emplace_back(pattern);
    }

    std::string result;
    result.reserve(source.size());
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t line_end = (eol == std::string::npos) ? source.size() : eol;
        std::string line = source.substr(i, line_end - i);
        bool drop = false;
        for (const auto& re : res) {
            if (std::regex_match(line, re)) { drop = true; break; }
        }
        if (!drop) {
            result.append(line);
            if (eol != std::string::npos) result.push_back('\n');
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return result;
}

std::string inject_after_version(const std::string& source, const std::string& block) {
    // Always runs — even with an empty block — so that every stage's
    // `#version 330 core` gets upgraded to `#version 450 core` for
    // shaderc. Without the upgrade shaderc emits a "forced to 450 while
    // source declares 330" warning and treats legacy (attribute/
    // varying) syntax inconsistently; with it every stage is uniform.

    // The synthesised block uses `layout(std140, binding = 1)` which is
    // a GL 4.2+ / GL_ARB_shading_language_420pack feature and also the
    // baseline for Vulkan GLSL via shaderc. User-authored `.shader`
    // stages often start with `#version 330 core` — safe to upgrade in
    // place since everything we inject is forward-compatible and the
    // stage body rarely uses anything 330-specific. Find the first
    // `#version` line, replace it with `#version 450 core`, then insert
    // the generated block right after.
    std::regex version_re(R"([ \t]*#version[^\n]*)");
    size_t i = 0;
    while (i < source.size()) {
        size_t eol = source.find('\n', i);
        size_t line_end = (eol == std::string::npos) ? source.size() : eol;
        std::string line = source.substr(i, line_end - i);
        if (std::regex_match(line, version_re)) {
            size_t line_start = i;
            std::string before = source.substr(0, line_start);
            std::string after = (eol == std::string::npos) ? std::string()
                                                           : source.substr(eol + 1);
            return before + "#version 450 core\n" + block + after;
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    // No #version — prepend both the version line and the block.
    return std::string("#version 450 core\n") + block + source;
}

std::string rewrite_engine_uniforms_for_stage_source(
    const std::string& source,
    const std::string& stage_name
) {
    std::string src = tgfx::internal::preprocess_shader_source(
        source, stage_name.c_str());

    // Parsed .shader sources may already have gone through the engine
    // rewrite before they are passed to TcMaterial.add_phase_from_sources
    // (e.g. builtin resources created from ShaderMultyPhaseProgramm). Keep
    // the helper idempotent.
    bool already_rewritten =
        src.find("uniform PerFrame") != std::string::npos ||
        src.find("uniform ColorPushBlock") != std::string::npos ||
        src.find("#define u_model pc._u_model") != std::string::npos;
    if (already_rewritten) {
        return inject_after_version(src, "");
    }

    EngineUniformDeclUsage usage = collect_engine_uniform_usage(src);
    if (usage.any()) {
        src = strip_engine_uniform_decls(src);
    }

    return inject_after_version(
        src,
        synthesize_engine_uniform_glsl(usage)
    );
}

// ========== std140 value packer ==========

namespace {

// Scalar readers: convert the property's variant payload to a single float
// (std140 bool stores as 4-byte int-style 0/1, but since we read back as
// vec4 in shaders for bools it's safe to write as float 0.0/1.0 — the bit
// pattern matches for 0 and 1 in IEEE 754).

inline void write_float(uint8_t* dst, float v) {
    std::memcpy(dst, &v, sizeof(float));
}

inline void write_int(uint8_t* dst, int32_t v) {
    std::memcpy(dst, &v, sizeof(int32_t));
}

// Write up to `count` floats from a vector<double> source. Missing elements
// default to 0.0. Used for Vec2/Vec3/Vec4/Color.
inline void write_float_array(uint8_t* dst,
                               const std::vector<double>& src,
                               size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float v = i < src.size() ? static_cast<float>(src[i]) : 0.0f;
        write_float(dst + i * sizeof(float), v);
    }
}

// Resolve a property-type string + variant payload into raw std140 bytes at
// the given destination. Returns true if a value was actually written.
bool pack_one(const std::string& property_type,
              const MaterialProperty::DefaultValue& value,
              uint8_t* dst) {
    if (property_type == "Float") {
        if (auto* d = std::get_if<double>(&value)) {
            write_float(dst, static_cast<float>(*d));
            return true;
        }
        if (auto* i = std::get_if<int>(&value)) {
            write_float(dst, static_cast<float>(*i));
            return true;
        }
        return false;
    }
    if (property_type == "Int") {
        if (auto* i = std::get_if<int>(&value)) {
            write_int(dst, *i);
            return true;
        }
        if (auto* d = std::get_if<double>(&value)) {
            write_int(dst, static_cast<int32_t>(*d));
            return true;
        }
        return false;
    }
    if (property_type == "Bool") {
        if (auto* b = std::get_if<bool>(&value)) {
            write_int(dst, *b ? 1 : 0);
            return true;
        }
        return false;
    }
    if (property_type == "Vec2" || property_type == "Vec3" ||
        property_type == "Vec4" || property_type == "Color") {
        auto* arr = std::get_if<std::vector<double>>(&value);
        if (!arr) return false;

        size_t count = 4;
        if (property_type == "Vec2") count = 2;
        else if (property_type == "Vec3") count = 3;
        // Vec4 / Color → 4.
        write_float_array(dst, *arr, count);
        return true;
    }
    if (property_type == "Mat4") {
        // 16 floats in column-major order, matching Mat44f storage and the
        // GLSL `mat4` default. std140 aligns each column to 16 bytes, which
        // for a tightly-packed mat4 means sequential 16-byte columns — so
        // the raw memcpy-style layout works.
        auto* arr = std::get_if<std::vector<double>>(&value);
        if (!arr) return false;
        write_float_array(dst, *arr, 16);
        return true;
    }
    // Texture or unknown — not in UBO.
    return false;
}

} // namespace

namespace {

// Two property_type strings are "compatible" for std140 packing if they
// pack to the same std140 slot layout. This is needed because the
// runtime value side (tc_uniform_value → MaterialProperty) doesn't know
// whether the original declaration was `Color` or `Vec4` — both round-
// trip through a vec4 of floats. Similarly, Float/Int can be written
// through the same 4-byte slot, and Bool packs identically to Int
// (std140 rule: Bool is stored as 32-bit uint).
bool property_types_compatible(const std::string& a, const std::string& b) {
    if (a == b) return true;
    // Color and Vec4 are the same std140 payload.
    if ((a == "Color" && b == "Vec4") || (a == "Vec4" && b == "Color")) return true;
    return false;
}

} // namespace

void std140_pack(const MaterialUboLayout& layout,
                 const std::vector<MaterialProperty>& values,
                 uint8_t* out_buffer) {
    if (!out_buffer || layout.empty()) return;

    // For each UBO entry, find a matching value by name. Linear scan is
    // fine — material UBOs have at most a handful of fields.
    for (const auto& entry : layout.entries) {
        const MaterialProperty* match = nullptr;
        for (const auto& v : values) {
            if (v.name == entry.name) {
                match = &v;
                break;
            }
        }
        if (!match) continue;  // leave the slot as-is (caller may have zeroed it).

        // Type must agree with what the layout expects. Mismatches are
        // skipped to avoid writing garbage into the wrong slot.
        if (!property_types_compatible(match->property_type, entry.property_type)) continue;

        // Always dispatch on the layout's declared type — pack_one reads
        // the layout type to know how many floats to write, and Color
        // packs as a 4-float Vec4.
        pack_one(entry.property_type, match->default_value,
                 out_buffer + entry.offset);
    }
}


static MaterialProperty parse_typed_uniform_directive(
    const std::string& line,
    const std::string& directive)
{
    // Remove directive prefix.
    std::string content = line.substr(directive.size());
    content = trim(content);

    // Extract range(...) if present
    std::optional<double> range_min, range_max;
    std::regex range_regex(R"(\brange\s*\(\s*([^,]+)\s*,\s*([^)]+)\s*\))");
    std::smatch range_match;
    if (std::regex_search(content, range_match, range_regex)) {
        try {
            range_min = std::stod(trim(range_match[1].str()));
            range_max = std::stod(trim(range_match[2].str()));
        } catch (...) {
            tc::Log::debug("Failed to parse range() in %s directive: %s",
                           directive.c_str(), line.c_str());
        }
        // Remove range from content
        content = content.substr(0, range_match.position());
        content = trim(content);
    }

    // Parse: Type name = value
    std::string property_type, name, value_str;

    size_t eq_pos = content.find('=');
    if (eq_pos != std::string::npos) {
        // Has default value
        std::string left = trim(content.substr(0, eq_pos));
        value_str = trim(content.substr(eq_pos + 1));

        auto parts = split_whitespace(left);
        if (parts.size() < 2) {
            throw std::runtime_error(directive + " requires type and name: " + line);
        }
        property_type = parts[0];
        name = parts[1];
    } else {
        // No default value
        auto parts = split_whitespace(content);
        if (parts.size() < 2) {
            throw std::runtime_error(directive + " requires type and name: " + line);
        }
        property_type = parts[0];
        name = parts[1];
    }

    // Validate type (Texture2D is alias for Texture)
    if (property_type == "Texture2D") {
        property_type = "Texture";
    }
    static const std::vector<std::string> valid_types = {
        "Float", "Int", "Bool", "Vec2", "Vec3", "Vec4", "Color", "Mat4", "Texture"
    };
    bool type_valid = std::find(valid_types.begin(), valid_types.end(), property_type) != valid_types.end();
    if (!type_valid) {
        throw std::runtime_error("Unknown property type: " + property_type);
    }

    // Parse default value
    MaterialProperty::DefaultValue default_value;
    if (!value_str.empty()) {
        default_value = parse_property_value(value_str, property_type);
    } else {
        default_value = get_default_for_type(property_type);
    }

    return MaterialProperty(name, property_type, default_value, range_min, range_max);
}

MaterialProperty parse_property_directive(const std::string& line) {
    return parse_typed_uniform_directive(line, "@property");
}


ShaderMultyPhaseProgramm parse_shader_text(const std::string& text) {
    std::istringstream stream(text);
    std::string raw_line;

    std::string program_name;
    std::string language = "glsl";
    std::vector<ShaderPhase> phases;
    std::vector<std::string> features;  // From @features directive

    // For @phases mode (shared stages)
    std::vector<std::string> declared_phases;  // From @phases directive
    std::unordered_map<std::string, ShaderStage> shared_stages;
    std::vector<MaterialProperty> material_properties;
    std::unordered_map<std::string, ShaderPhase> phase_settings;  // Per-phase overrides

    ShaderPhase* current_phase = nullptr;
    std::string current_settings_phase;  // Which phase @settings applies to
    std::string current_stage_name;
    std::vector<std::string> current_stage_lines;
    bool in_shared_stage = false;  // Stage outside @phase (for @phases mode)

    auto add_material_property = [&](MaterialProperty prop) {
        auto existing = std::find_if(
            material_properties.begin(),
            material_properties.end(),
            [&](const MaterialProperty& other) {
                return other.name == prop.name;
            });
        if (existing != material_properties.end()) {
            if (existing->property_type != prop.property_type) {
                throw std::runtime_error(
                    "Duplicate @property with conflicting type: " + prop.name);
            }
            return;
        }
        material_properties.push_back(std::move(prop));
    };

    auto close_current_stage = [&]() {
        if (current_stage_name.empty()) return;

        std::string source;
        for (const auto& l : current_stage_lines) {
            source += l;
        }

        if (in_shared_stage) {
            // Shared stage (for @phases mode)
            shared_stages[current_stage_name] = ShaderStage(current_stage_name, source);
        } else if (current_phase) {
            // Traditional @phase mode
            current_phase->stages[current_stage_name] = ShaderStage(current_stage_name, source);
        }

        current_stage_name.clear();
        current_stage_lines.clear();
        in_shared_stage = false;
    };

    auto close_current_phase = [&]() {
        if (!current_phase) return;
        close_current_stage();
        phases.push_back(std::move(*current_phase));
        delete current_phase;
        current_phase = nullptr;
    };

    while (std::getline(stream, raw_line)) {
        // Keep newline for stage content
        std::string line_with_newline = raw_line + "\n";
        std::string line = trim(raw_line);

        // Inside @stage: collect lines until @endstage or another directive
        if (!current_stage_name.empty()) {
            if (starts_with(line, "@endstage")) {
                close_current_stage();
                continue;
            }
            else if (starts_with(line, "@stage ")) {
                close_current_stage();
                // Fall through to handle @stage below
            }
            else if (starts_with(line, "@phase ")) {
                close_current_stage();
                close_current_phase();
                // Fall through to handle @phase below
            }
            else if (starts_with(line, "@endphase")) {
                close_current_stage();
                // Fall through to handle @endphase below
            }
            else if (starts_with(line, "@settings ")) {
                close_current_stage();
                // Fall through to handle @settings below
            }
            else if (starts_with(line, "@endsettings")) {
                close_current_stage();
                // Fall through
            }
            else {
                current_stage_lines.push_back(line_with_newline);
                continue;
            }
        }

        // Outside @stage: process directives
        if (!starts_with(line, "@") || line == "@") {
            continue;
        }

        auto parts = split_whitespace(line);
        std::string directive = parts[0];

        if (directive == "@program") {
            if (parts.size() < 2) {
                throw std::runtime_error("@program without name");
            }
            program_name = parts[1];
            for (size_t i = 2; i < parts.size(); ++i) {
                program_name += " " + parts[i];
            }
        }
        else if (directive == "@language") {
            if (parts.size() != 2) {
                throw std::runtime_error("@language expects exactly one value");
            }
            language = to_lower(parts[1]);
            if (language != "glsl" && language != "slang") {
                throw std::runtime_error("Unsupported shader language: " + parts[1]);
            }
        }
        else if (directive == "@features") {
            // Parse comma-separated feature names: @features lighting_ubo, instancing
            std::string rest = line.substr(9);  // len("@features") = 9
            std::string feature_name;
            for (char c : rest) {
                if (c == ',' || c == ' ' || c == '\t') {
                    std::string trimmed = trim(feature_name);
                    if (!trimmed.empty()) {
                        features.push_back(trimmed);
                        feature_name.clear();
                    }
                } else {
                    feature_name += c;
                }
            }
            std::string trimmed = trim(feature_name);
            if (!trimmed.empty()) {
                features.push_back(trimmed);
            }
        }
        else if (directive == "@phases") {
            // Parse comma-separated phase names: @phases opaque, transparent
            std::string rest = line.substr(7);  // len("@phases") = 7
            std::string phase_name;
            for (char c : rest) {
                if (c == ',' || c == ' ' || c == '\t') {
                    std::string trimmed = trim(phase_name);
                    if (!trimmed.empty()) {
                        declared_phases.push_back(trimmed);
                        phase_name.clear();
                    }
                } else {
                    phase_name += c;
                }
            }
            std::string trimmed = trim(phase_name);
            if (!trimmed.empty()) {
                declared_phases.push_back(trimmed);
            }
        }
        else if (directive == "@settings") {
            if (parts.size() < 2) {
                throw std::runtime_error("@settings without phase name");
            }
            current_settings_phase = parts[1];
            // Initialize settings for this phase if not exists
            if (phase_settings.find(current_settings_phase) == phase_settings.end()) {
                phase_settings[current_settings_phase] = ShaderPhase(current_settings_phase);
            }
        }
        else if (directive == "@endsettings") {
            current_settings_phase.clear();
        }
        else if (directive == "@phase") {
            if (parts.size() < 2) {
                throw std::runtime_error("@phase without mark");
            }
            close_current_phase();

            // Parse comma-separated marks: @phase mark1, mark2
            std::string rest = line.substr(6);  // len("@phase") = 6
            std::vector<std::string> marks;
            std::string mark_name;
            for (char c : rest) {
                if (c == ',' || c == ' ' || c == '\t') {
                    std::string trimmed = trim(mark_name);
                    if (!trimmed.empty()) {
                        marks.push_back(trimmed);
                        mark_name.clear();
                    }
                } else {
                    mark_name += c;
                }
            }
            std::string trimmed = trim(mark_name);
            if (!trimmed.empty()) {
                marks.push_back(trimmed);
            }

            current_phase = new ShaderPhase(std::move(marks));
        }
        else if (directive == "@endphase") {
            close_current_phase();
        }
        else if (directive == "@priority") {
            if (!current_settings_phase.empty()) {
                if (parts.size() < 2) throw std::runtime_error("@priority without value");
                phase_settings[current_settings_phase].priority = std::stoi(parts[1]);
            } else if (current_phase) {
                if (parts.size() < 2) throw std::runtime_error("@priority without value");
                current_phase->priority = std::stoi(parts[1]);
            } else {
                throw std::runtime_error("@priority outside @phase or @settings");
            }
        }
        else if (directive == "@glDepthMask") {
            if (parts.size() < 2) throw std::runtime_error("@glDepthMask without value");
            bool val = parse_bool(parts[1]);
            if (!current_settings_phase.empty()) {
                phase_settings[current_settings_phase].gl_depth_mask = val;
            } else if (current_phase) {
                current_phase->gl_depth_mask = val;
            } else {
                throw std::runtime_error("@glDepthMask outside @phase or @settings");
            }
        }
        else if (directive == "@glDepthTest") {
            if (parts.size() < 2) throw std::runtime_error("@glDepthTest without value");
            bool val = parse_bool(parts[1]);
            if (!current_settings_phase.empty()) {
                phase_settings[current_settings_phase].gl_depth_test = val;
            } else if (current_phase) {
                current_phase->gl_depth_test = val;
            } else {
                throw std::runtime_error("@glDepthTest outside @phase or @settings");
            }
        }
        else if (directive == "@glBlend") {
            if (parts.size() < 2) throw std::runtime_error("@glBlend without value");
            bool val = parse_bool(parts[1]);
            if (!current_settings_phase.empty()) {
                phase_settings[current_settings_phase].gl_blend = val;
            } else if (current_phase) {
                current_phase->gl_blend = val;
            } else {
                throw std::runtime_error("@glBlend outside @phase or @settings");
            }
        }
        else if (directive == "@glCull") {
            if (parts.size() < 2) throw std::runtime_error("@glCull without value");
            bool val = parse_bool(parts[1]);
            if (!current_settings_phase.empty()) {
                phase_settings[current_settings_phase].gl_cull = val;
            } else if (current_phase) {
                current_phase->gl_cull = val;
            } else {
                throw std::runtime_error("@glCull outside @phase or @settings");
            }
        }
        else if (directive == "@stage") {
            if (parts.size() < 2) {
                throw std::runtime_error("@stage without name");
            }
            if (!current_stage_name.empty()) {
                throw std::runtime_error("Nested @stage not supported");
            }
            current_stage_name = parts[1];
            current_stage_lines.clear();

            // If inside @phase, it's phase-specific; otherwise shared
            in_shared_stage = (current_phase == nullptr);
        }
        else if (directive == "@endstage") {
            close_current_stage();
        }
        else if (directive == "@property") {
            add_material_property(parse_property_directive(line));
        }
        else {
            throw std::runtime_error("Unknown directive: " + directive);
        }
    }

    // Close anything remaining
    close_current_stage();
    if (current_phase) {
        close_current_phase();
    }

    // If @phases was used, generate ONE phase with all marks as available choices
    if (!declared_phases.empty()) {
        ShaderPhase phase(declared_phases);  // Constructor sets phase_mark to first, available_marks to all

        // Copy shared stages
        phase.stages = shared_stages;

        // Apply default opaque settings
        phase.gl_depth_test = true;
        phase.gl_depth_mask = true;
        phase.gl_blend = false;
        phase.gl_cull = true;
        phase.priority = 0;

        // Store per-mark settings from @settings blocks
        for (const auto& mark : declared_phases) {
            PhaseRenderSettings settings;
            // Default opaque settings
            settings.gl_depth_test = true;
            settings.gl_depth_mask = true;
            settings.gl_blend = false;
            settings.gl_cull = true;
            settings.priority = 0;

            // Apply overrides from @settings
            auto it = phase_settings.find(mark);
            if (it != phase_settings.end()) {
                const auto& overrides = it->second;
                if (overrides.gl_depth_test.has_value())
                    settings.gl_depth_test = overrides.gl_depth_test;
                if (overrides.gl_depth_mask.has_value())
                    settings.gl_depth_mask = overrides.gl_depth_mask;
                if (overrides.gl_blend.has_value())
                    settings.gl_blend = overrides.gl_blend;
                if (overrides.gl_cull.has_value())
                    settings.gl_cull = overrides.gl_cull;
                if (overrides.priority != 0)
                    settings.priority = overrides.priority;
            }

            // Default priority for transparent
            if (mark == "transparent" && settings.priority == 0) {
                settings.priority = 1000;
            }

            phase.mark_settings[mark] = settings;
        }

        // Apply settings for the default (first) phase mark
        auto it = phase.mark_settings.find(phase.phase_mark);
        if (it != phase.mark_settings.end()) {
            const auto& s = it->second;
            phase.gl_depth_test = s.gl_depth_test;
            phase.gl_depth_mask = s.gl_depth_mask;
            phase.gl_blend = s.gl_blend;
            phase.gl_cull = s.gl_cull;
            phase.priority = s.priority;
        }

        phases.push_back(std::move(phase));
    }

    ShaderMultyPhaseProgramm result(
        program_name,
        std::move(phases),
        "",
        std::move(features),
        std::move(material_properties));
    result.language = language;

    if (result.language == "slang") {
        for (auto& phase : result.phases) {
            phase.uniforms = collect_used_material_properties(
                result.material_properties,
                phase);

            MaterialUboLayout layout = compute_std140_layout(phase.uniforms);
            std::vector<std::string> texture_names =
                collect_texture_properties(phase.uniforms);

            if (!layout.empty() && contains_slang_material_params_declaration(phase)) {
                throw std::runtime_error(
                    "Slang .shader @property auto-generates MaterialParams; "
                    "remove the manual MaterialParams/ConstantBuffer declaration");
            }

            std::string block_slang = synthesize_material_ubo_slang(layout);
            for (auto& kv : phase.stages) {
                std::string sampler_slang =
                    synthesize_material_sampler_slang(texture_names, kv.second.source);
                if (!sampler_slang.empty()) {
                    kv.second.source =
                        strip_slang_sampler_decls(kv.second.source, texture_names);
                }

                // Engine uniforms for Slang: detect u_view / u_model usage
                // and inject matching PerFrame UBO + push_constant block.
                EngineUniformDeclUsage eng_usage =
                    collect_engine_uniform_usage(kv.second.source);
                std::string eng_slang;
                if (eng_usage.any()) {
                    kv.second.source =
                        strip_engine_uniform_decls(kv.second.source);
                    eng_slang = synthesize_engine_uniform_slang(eng_usage);
                }

                if (stage_uses_material_ubo_layout(kv.second, layout)) {
                    kv.second.source = block_slang + eng_slang + sampler_slang + kv.second.source;
                } else if (!sampler_slang.empty() || !eng_slang.empty()) {
                    kv.second.source = eng_slang + sampler_slang + kv.second.source;
                }
                kv.second.source = ensure_slang_prelude_import(std::move(kv.second.source));
            }
            if (!layout.empty()) {
                phase.material_ubo_layout = std::move(layout);
            }
        }
        return result;
    }

    // Material UBO synthesis is unconditional: any phase that declares
    // scalar/vector @property or @uniform entries gets a std140
    // MaterialParams block auto-synthesized, injected into the stage
    // sources, and the original `uniform T name;` decls stripped from the
    // raw GLSL. Texture properties/uniforms are not part of the UBO, but
    // their sampler declarations still get explicit layout bindings that
    // match ColorPass material texture slots.
    //
    // Phases without UBO-eligible properties get an empty layout and
    // their sources are left alone.
    //
    // The former `@features material_ubo` opt-in was temporary scaffolding
    // for the migration; see shadow/depth/normal/id pass pattern for the
    // same "two code paths converge into one" cleanup.
    for (auto& phase : result.phases) {
        for (auto& kv : phase.stages) {
            kv.second.source = tgfx::internal::preprocess_shader_source(
                kv.second.source,
                kv.first.c_str());
        }

        phase.uniforms = collect_used_material_properties(
            result.material_properties,
            phase);
        phase.material_uniforms = discover_material_uniforms_from_stages(
            phase,
            result.material_properties);

        std::vector<MaterialProperty> shader_uniforms = phase.uniforms;
        shader_uniforms.insert(
            shader_uniforms.end(),
            phase.material_uniforms.begin(),
            phase.material_uniforms.end());

        MaterialUboLayout layout = compute_std140_layout(shader_uniforms);
        std::vector<std::string> texture_names = collect_texture_properties(shader_uniforms);
        std::string block_glsl;
        std::vector<std::string> ubo_names;
        if (!layout.empty()) {
            block_glsl = synthesize_material_ubo_glsl(layout);
            ubo_names.reserve(layout.entries.size());
            for (const auto& e : layout.entries) {
                ubo_names.push_back(e.name);
            }
        }

        for (auto& kv : phase.stages) {
            std::string& src = kv.second.source;

            std::string sampler_glsl =
                synthesize_material_sampler_glsl(texture_names, src);
            src = strip_sampler_decls(src, texture_names);

            // Material UBO: strip @property plain-uniform decls and
            // inject the synthesised std140 block. Only for phases with
            // non-empty layout — phases without @property entries skip
            // this step but still go through the engine uniforms pass
            // below so their u_view/u_projection/u_model references
            // still resolve.
            if (!layout.empty()) {
                src = strip_uniform_decls(src, ubo_names);
            }

            // Engine uniforms: strip plain decls of u_view / u_projection /
            // u_model / u_view_projection / u_camera_position, then inject the
            // matching PerFrame UBO and/or push_constant block.
            //
            // Skip the engine injection entirely when any of the engine
            // names collide with a MaterialParams @property entry — this
            // happens in shaders that hand the view/proj matrices down
            // through the material system rather than asking the engine
            // to supply them (e.g. the Skybox program). Injecting would
            // redeclare `u_view` in PerFrame and collide with the same
            // name already promoted from MaterialParams into the global
            // scope ("nameless block contains a member that already has
            // a name at global scope").
            bool collision_with_material = false;
            for (const char* engine_name : ENGINE_PLAIN_UNIFORM_NAMES) {
                for (const auto& ubo_name : ubo_names) {
                    if (ubo_name == engine_name) {
                        collision_with_material = true;
                        break;
                    }
                }
                if (collision_with_material) break;
            }
            EngineUniformDeclUsage engine_usage;
            if (!collision_with_material) {
                engine_usage = collect_engine_uniform_usage(src);
            }
            if (engine_usage.any()) {
                src = strip_engine_uniform_decls(src);
            }

            // Combine material block + engine block into a single
            // after-version injection so both land above the stage body
            // and get the same `#version 450 core` upgrade treatment.
            // Always called so the stage's #version line is upgraded to
            // 450 regardless of whether there's anything to inject.
            std::string inject = block_glsl + sampler_glsl;
            inject += synthesize_engine_uniform_glsl(engine_usage);
            src = inject_after_version(src, inject);
        }

        if (!layout.empty()) {
            phase.material_ubo_layout = std::move(layout);
        }
    }

    return result;
}

} // namespace termin
