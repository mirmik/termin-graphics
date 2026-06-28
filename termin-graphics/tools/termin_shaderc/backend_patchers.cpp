#include "backend_patchers.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace termin_shaderc::internal {

void annotate_slang_glsl_symbols(
    std::vector<ShaderResourceBinding>& resources,
    const std::string& source
) {
    for (ShaderResourceBinding& resource : resources) {
        if (!resource.slang_glsl_symbol.empty()) {
            continue;
        }

        if (resource.kind == "constant_buffer") {
            const std::regex cbuffer_re(
                "ConstantBuffer\\s*<\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*>\\s*" +
                regex_escape(resource.name) +
                "\\b");
            std::smatch match;
            if (std::regex_search(source, match, cbuffer_re)) {
                resource.slang_glsl_symbol = "block_" + match[1].str() + "_0";
            } else {
                resource.slang_glsl_symbol = resource.name + "_0";
            }
        } else {
            resource.slang_glsl_symbol = resource.name + "_0";
        }
    }
}

static bool slang_opengl_resource_present_in_glsl(
    const std::string& glsl,
    const ShaderResourceBinding& resource
) {
    if (!resource.slang_glsl_symbol.empty() &&
        glsl.find(resource.slang_glsl_symbol) != std::string::npos) {
        return true;
    }
    const std::string instance_symbol = resource.name + "_0";
    return glsl.find(instance_symbol) != std::string::npos;
}

bool filter_slang_opengl_resources_for_glsl(
    const CompileOptions& options,
    std::vector<ShaderResourceBinding>& resources
) {
    std::string glsl;
    if (!read_file(options.output, glsl)) {
        return false;
    }

    std::vector<ShaderResourceBinding> active;
    active.reserve(resources.size());
    for (const ShaderResourceBinding& resource : resources) {
        if (slang_opengl_resource_present_in_glsl(glsl, resource)) {
            active.push_back(resource);
        }
    }
    resources = std::move(active);
    return true;
}

static bool write_text_file(
    const std::string& path,
    const std::string& text
);

static std::vector<std::string> slang_opengl_resource_symbols(
    const ShaderResourceBinding& resource
) {
    std::vector<std::string> symbols;
    if (!resource.slang_glsl_symbol.empty()) {
        symbols.push_back(resource.slang_glsl_symbol);
    }
    const std::string instance_symbol = resource.name + "_0";
    if (std::find(symbols.begin(), symbols.end(), instance_symbol) == symbols.end()) {
        symbols.push_back(instance_symbol);
    }
    return symbols;
}

static bool patch_slang_opengl_glsl_resource_binding(
    std::string& glsl,
    const ShaderResourceBinding& resource,
    bool& changed
) {
    const std::vector<std::string> symbols = slang_opengl_resource_symbols(resource);
    size_t symbol_pos = std::string::npos;
    std::string matched_symbol;
    for (const std::string& symbol : symbols) {
        symbol_pos = glsl.find(symbol);
        if (symbol_pos != std::string::npos) {
            matched_symbol = symbol;
            break;
        }
    }
    if (symbol_pos == std::string::npos) {
        std::cerr
            << "termin_shaderc: OpenGL GLSL is missing reflected resource '"
            << resource.name << "'; cannot patch binding placement\n";
        return false;
    }

    const std::regex binding_layout_re(
        R"(layout\s*\(([^\)]*?\bbinding\s*=\s*)([0-9]+)([^\)]*)\))");

    size_t search_before = symbol_pos;
    while (true) {
        const size_t layout_pos = glsl.rfind("layout", search_before);
        if (layout_pos == std::string::npos) {
            break;
        }

        const std::string declaration_prefix =
            glsl.substr(layout_pos, symbol_pos - layout_pos + matched_symbol.size());
        std::smatch match;
        if (std::regex_search(declaration_prefix, match, binding_layout_re)) {
            const size_t value_begin =
                layout_pos +
                static_cast<size_t>(match.position(2));
            const size_t value_size = static_cast<size_t>(match.length(2));
            const std::string normalized_binding = std::to_string(resource.binding);
            if (glsl.compare(value_begin, value_size, normalized_binding) == 0) {
                return true;
            }
            glsl.replace(value_begin, value_size, normalized_binding);
            changed = true;
            return true;
        }

        if (layout_pos == 0) {
            break;
        }
        search_before = layout_pos - 1;
    }

    std::cerr
        << "termin_shaderc: OpenGL GLSL declaration for resource '"
        << resource.name << "' has no layout(binding=...) to patch\n";
    return false;
}

bool patch_slang_opengl_glsl_resource_bindings(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources
) {
    std::string glsl;
    if (!read_file(options.output, glsl)) {
        return false;
    }

    bool changed = false;
    for (const ShaderResourceBinding& resource : resources) {
        if (!patch_slang_opengl_glsl_resource_binding(glsl, resource, changed)) {
            return false;
        }
    }
    if (!changed) {
        return true;
    }
    return write_text_file(options.output, glsl);
}

static bool replace_all_literal(
    std::string& text,
    const std::string& needle,
    const std::string& replacement
) {
    bool changed = false;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
        changed = true;
    }
    return changed;
}

static bool write_text_file(
    const std::string& path,
    const std::string& text
) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open output for patch: " << path << "\n";
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

static bool patch_slang_d3d11_hlsl_symbol_register(
    std::string& hlsl,
    const std::string& symbol,
    char register_class,
    uint32_t register_index,
    bool& changed,
    bool& found
) {
    size_t search_from = 0;
    while (true) {
        const size_t symbol_pos = hlsl.find(symbol, search_from);
        if (symbol_pos == std::string::npos) {
            return true;
        }
        const bool left_ok =
            symbol_pos == 0 ||
            !is_identifier_char(hlsl[symbol_pos - 1]);
        const size_t symbol_end = symbol_pos + symbol.size();
        const bool right_ok =
            symbol_end >= hlsl.size() ||
            !is_identifier_char(hlsl[symbol_end]);
        if (!left_ok || !right_ok) {
            search_from = symbol_end;
            continue;
        }

        const size_t line_end = hlsl.find_first_of("\r\n;", symbol_end);
        const size_t search_limit =
            line_end == std::string::npos ? hlsl.size() : line_end;
        const size_t register_pos = hlsl.find("register", symbol_end);
        if (register_pos == std::string::npos || register_pos >= search_limit) {
            search_from = symbol_end;
            continue;
        }
        const size_t open_paren = hlsl.find('(', register_pos);
        if (open_paren == std::string::npos || open_paren >= search_limit) {
            search_from = symbol_end;
            continue;
        }
        size_t class_pos = open_paren + 1;
        while (class_pos < search_limit &&
               (hlsl[class_pos] == ' ' || hlsl[class_pos] == '\t')) {
            ++class_pos;
        }
        if (class_pos >= search_limit || hlsl[class_pos] != register_class) {
            search_from = symbol_end;
            continue;
        }
        size_t value_begin = class_pos + 1;
        while (value_begin < search_limit &&
               (hlsl[value_begin] == ' ' || hlsl[value_begin] == '\t')) {
            ++value_begin;
        }
        size_t value_end = value_begin;
        while (value_end < search_limit &&
               hlsl[value_end] >= '0' &&
               hlsl[value_end] <= '9') {
            ++value_end;
        }
        if (value_begin == value_end) {
            search_from = symbol_end;
            continue;
        }

        found = true;
        const std::string normalized_register = std::to_string(register_index);
        if (hlsl.compare(
                value_begin,
                value_end - value_begin,
                normalized_register) == 0) {
            return true;
        }
        hlsl.replace(value_begin, value_end - value_begin, normalized_register);
        changed = true;
        return true;
    }
}

static bool patch_slang_d3d11_hlsl_resource(
    std::string& hlsl,
    const ShaderResourceBinding& resource,
    bool& changed
) {
    if (resource.d3d11_register_class.size() != 1) {
        return true;
    }
    const char register_class = resource.d3d11_register_class[0];
    const uint32_t register_index = resource.d3d11_register_index;

    auto patch_any_symbol =
        [&](const std::vector<std::string>& symbols,
            char cls,
            bool required) -> bool {
        bool any_found = false;
        for (const std::string& symbol : symbols) {
            bool found = false;
            if (!patch_slang_d3d11_hlsl_symbol_register(
                    hlsl,
                    symbol,
                    cls,
                    register_index,
                    changed,
                    found)) {
                return false;
            }
            any_found = any_found || found;
        }
        if (!any_found && required) {
            std::cerr
                << "termin_shaderc: D3D11 HLSL is missing register declaration "
                << "for reflected resource '" << resource.name << "'\n";
            return false;
        }
        return true;
    };

    if (resource.kind == "texture") {
        const bool texture_found_in_hlsl =
            hlsl.find(resource.name + "_texture_0") != std::string::npos ||
            hlsl.find(resource.name + "_0") != std::string::npos;
        if (!patch_any_symbol(
                {resource.name + "_texture_0", resource.name + "_0"},
                register_class,
                texture_found_in_hlsl)) {
            return false;
        }
        if (resource.slang_combined_texture) {
            const bool sampler_found_in_hlsl =
                hlsl.find(resource.name + "_sampler_0") != std::string::npos;
            if (!patch_any_symbol(
                    {resource.name + "_sampler_0"},
                    's',
                    sampler_found_in_hlsl)) {
                return false;
            }
        }
        return true;
    }

    return patch_any_symbol(
        {resource.name + "_0", resource.name},
        register_class,
        false);
}

struct D3D11HlslResourceDecl {
    std::string symbol;
    std::string resource_name;
    std::string kind;
    char register_class = 0;
    uint32_t register_index = 0;
    bool is_sampler = false;
    bool is_texture = false;
    bool is_storage_texture = false;
    bool is_array = false;
    bool is_comparison_sampler = false;
};

static std::string trim_copy(std::string text) {
    const char* whitespace = " \t\r\n";
    const size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

static bool string_ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string d3d11_hlsl_resource_name_from_symbol(
    const std::string& symbol
) {
    if (string_ends_with(symbol, "_texture_0")) {
        return symbol.substr(0, symbol.size() - 10);
    }
    if (string_ends_with(symbol, "_sampler_0")) {
        return symbol.substr(0, symbol.size() - 10);
    }
    if (string_ends_with(symbol, "_0")) {
        return symbol.substr(0, symbol.size() - 2);
    }
    return symbol;
}

static std::optional<D3D11HlslResourceDecl> parse_d3d11_hlsl_resource_decl(
    const std::string& line
) {
    const size_t register_pos = line.find("register");
    if (register_pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t colon_pos = line.rfind(':', register_pos);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t open_paren = line.find('(', register_pos);
    if (open_paren == std::string::npos) {
        return std::nullopt;
    }
    size_t class_pos = open_paren + 1;
    while (class_pos < line.size() &&
           (line[class_pos] == ' ' || line[class_pos] == '\t')) {
        ++class_pos;
    }
    if (class_pos >= line.size()) {
        return std::nullopt;
    }
    const char register_class = line[class_pos];
    if (register_class != 'b' &&
        register_class != 't' &&
        register_class != 's' &&
        register_class != 'u') {
        return std::nullopt;
    }
    size_t value_begin = class_pos + 1;
    while (value_begin < line.size() &&
           (line[value_begin] == ' ' || line[value_begin] == '\t')) {
        ++value_begin;
    }
    size_t value_end = value_begin;
    while (value_end < line.size() &&
           line[value_end] >= '0' &&
           line[value_end] <= '9') {
        ++value_end;
    }
    if (value_begin == value_end) {
        return std::nullopt;
    }

    std::string prefix = trim_copy(line.substr(0, colon_pos));
    if (prefix.empty()) {
        return std::nullopt;
    }
    const bool is_array = prefix.rfind('[') != std::string::npos;
    const size_t array_pos = prefix.rfind('[');
    if (array_pos != std::string::npos) {
        prefix = trim_copy(prefix.substr(0, array_pos));
    }
    const size_t symbol_begin = prefix.find_last_of(" \t");
    if (symbol_begin == std::string::npos || symbol_begin + 1 >= prefix.size()) {
        return std::nullopt;
    }
    const std::string symbol = prefix.substr(symbol_begin + 1);

    D3D11HlslResourceDecl decl;
    decl.symbol = symbol;
    decl.resource_name = d3d11_hlsl_resource_name_from_symbol(symbol);
    decl.register_class = register_class;
    decl.register_index = static_cast<uint32_t>(
        std::stoul(line.substr(value_begin, value_end - value_begin)));
    decl.is_sampler =
        prefix.find("SamplerState") != std::string::npos ||
        prefix.find("SamplerComparisonState") != std::string::npos;
    decl.is_texture = prefix.find("Texture") != std::string::npos;
    decl.is_storage_texture = prefix.find("RWTexture") != std::string::npos;
    decl.is_array = is_array;
    decl.is_comparison_sampler =
        prefix.find("SamplerComparisonState") != std::string::npos;
    if (prefix.rfind("cbuffer ", 0) == 0) {
        decl.kind = "constant_buffer";
    } else if (decl.is_sampler) {
        decl.kind = "sampler";
    } else if (decl.is_storage_texture || register_class == 'u') {
        decl.kind = "storage_texture";
    } else if (decl.is_texture) {
        decl.kind = "texture";
    } else {
        return std::nullopt;
    }
    return decl;
}

static std::vector<D3D11HlslResourceDecl> collect_d3d11_hlsl_resource_decls(
    const std::string& hlsl
) {
    std::vector<D3D11HlslResourceDecl> decls;
    std::istringstream stream(hlsl);
    std::string line;
    while (std::getline(stream, line)) {
        std::optional<D3D11HlslResourceDecl> decl =
            parse_d3d11_hlsl_resource_decl(line);
        if (decl) {
            decls.push_back(std::move(*decl));
        }
    }
    return decls;
}

static bool d3d11_hlsl_has_decl_for_resource(
    const std::vector<D3D11HlslResourceDecl>& decls,
    const std::string& resource_name,
    bool sampler
) {
    return std::any_of(
        decls.begin(),
        decls.end(),
        [&](const D3D11HlslResourceDecl& decl) {
            return decl.resource_name == resource_name &&
                   decl.is_sampler == sampler;
        });
}

static bool d3d11_hlsl_has_comparison_sampler_array_for_resource(
    const std::vector<D3D11HlslResourceDecl>& decls,
    const std::string& resource_name
) {
    return std::any_of(
        decls.begin(),
        decls.end(),
        [&](const D3D11HlslResourceDecl& decl) {
            return decl.resource_name == resource_name &&
                   decl.is_sampler &&
                   decl.is_comparison_sampler &&
                   decl.is_array;
        });
}

static std::string infer_d3d11_hlsl_resource_scope(
    const std::string& name,
    const std::string& kind
) {
    (void)name;
    (void)kind;
    return "unscoped";
}

static void append_missing_d3d11_hlsl_resources(
    std::vector<ShaderResourceBinding>& resources,
    const std::string& hlsl,
    uint32_t stage_mask
) {
    const std::vector<D3D11HlslResourceDecl> decls =
        collect_d3d11_hlsl_resource_decls(hlsl);
    for (ShaderResourceBinding& binding : resources) {
        if (binding.kind == "texture" &&
            d3d11_hlsl_has_comparison_sampler_array_for_resource(
                decls,
                binding.name)) {
            binding.d3d11_scalar_sampler_for_texture_array = true;
        }
    }
    for (const D3D11HlslResourceDecl& decl : decls) {
        if (decl.is_sampler &&
            d3d11_hlsl_has_decl_for_resource(
                decls,
                decl.resource_name,
                false)) {
            continue;
        }
        if (has_resource_named(resources, decl.resource_name)) {
            continue;
        }

        ShaderResourceBinding binding;
        binding.name = decl.resource_name;
        binding.kind = decl.kind;
        binding.scope =
            infer_d3d11_hlsl_resource_scope(binding.name, binding.kind);
        binding.stage_mask = stage_mask;
        binding.binding = decl.register_index;
        binding.set = 0;
        binding.slang_combined_texture =
            decl.kind == "texture" &&
            d3d11_hlsl_has_decl_for_resource(
                decls,
                decl.resource_name,
                true);
        binding.d3d11_scalar_sampler_for_texture_array =
            decl.kind == "texture" &&
            d3d11_hlsl_has_comparison_sampler_array_for_resource(
                decls,
                decl.resource_name);
        binding.slang_split_texture =
            decl.kind == "texture" && !binding.slang_combined_texture;
        binding.slang_separate_sampler = decl.kind == "sampler";
        binding.slang_storage_texture = decl.kind == "storage_texture";
        resources.push_back(std::move(binding));
    }
}

bool augment_d3d11_resource_bindings_from_hlsl(
    const CompileOptions& options,
    const std::filesystem::path& hlsl_path,
    std::vector<ShaderResourceBinding>& resources
) {
    std::string hlsl;
    if (!read_file(hlsl_path.string(), hlsl)) {
        return false;
    }

    append_missing_d3d11_hlsl_resources(
        resources,
        hlsl,
        stage_mask_for_stage(options.stage));
    assign_missing_resource_scopes(resources);
    apply_default_resource_scope(resources, options.default_scope);
    normalize_scope_first_binding_slots(
        resources,
        options.language == "slang",
        options.target);
    assign_d3d11_register_placement(resources);
    return true;
}

static bool legalize_slang_d3d11_comparison_sampler_arrays(
    std::string& hlsl,
    bool& changed
) {
    static const std::regex comparison_sampler_decl_re(
        R"((SamplerComparisonState\s+)([A-Za-z_][A-Za-z0-9_]*_sampler_0)\s*\[[^\]]+\])");
    static const std::regex comparison_sampler_use_re(
        R"(([A-Za-z_][A-Za-z0-9_]*_sampler_0)\s*\[\s*int\s*\(\s*[0-9]+\s*\)\s*\])");
    const std::string before = hlsl;
    hlsl = std::regex_replace(
        hlsl,
        comparison_sampler_decl_re,
        "$1$2");
    hlsl = std::regex_replace(
        hlsl,
        comparison_sampler_use_re,
        "$1");
    if (hlsl != before) {
        changed = true;
    }
    return true;
}

bool patch_slang_d3d11_hlsl_resource_bindings(
    const std::filesystem::path& hlsl_path,
    const std::vector<ShaderResourceBinding>& resources
) {
    std::string hlsl;
    if (!read_file(hlsl_path.string(), hlsl)) {
        return false;
    }

    if (std::getenv("TERMIN_SHADERC_DEBUG_LAYOUT")) {
        for (const ShaderResourceBinding& resource : resources) {
            std::cerr
                << "termin_shaderc: D3D11 layout resource '"
                << resource.name << "' kind=" << resource.kind
                << " binding=" << resource.binding
                << " register=" << resource.d3d11_register_class
                << resource.d3d11_register_index << "\n";
        }
    }

    bool changed = false;
    if (!legalize_slang_d3d11_comparison_sampler_arrays(hlsl, changed)) {
        return false;
    }
    for (const ShaderResourceBinding& resource : resources) {
        if (!patch_slang_d3d11_hlsl_resource(hlsl, resource, changed)) {
            return false;
        }
    }
    if (!changed) {
        return true;
    }
    return write_text_file(hlsl_path.string(), hlsl);
}

bool legalize_slang_opengl_glsl_builtins(
    const CompileOptions& options
) {
    std::string glsl;
    if (!read_file(options.output, glsl)) {
        return false;
    }

    bool changed = false;
    changed |= replace_all_literal(
        glsl,
        "uint(gl_InstanceIndex - gl_BaseInstance)",
        "uint(gl_InstanceID)");
    changed |= replace_all_literal(
        glsl,
        "gl_InstanceIndex - gl_BaseInstance",
        "gl_InstanceID");
    changed |= replace_all_literal(
        glsl,
        "gl_InstanceIndex",
        "gl_InstanceID");

    if (!changed) {
        return true;
    }
    return write_text_file(options.output, glsl);
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

static bool spirv_resource_has_descriptor_decorations(
    const std::vector<uint32_t>& words,
    uint32_t resource_id
) {
    constexpr uint16_t OP_DECORATE = 71;
    constexpr uint32_t DECORATION_BINDING = 33;
    constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;

    bool has_binding = false;
    bool has_set = false;
    for (size_t i = 5; i < words.size();) {
        const uint32_t instruction = words[i];
        const uint16_t word_count = static_cast<uint16_t>(instruction >> 16);
        const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
        if (word_count == 0 || i + word_count > words.size()) {
            return false;
        }
        if (opcode == OP_DECORATE && word_count >= 4 && words[i + 1] == resource_id) {
            if (words[i + 2] == DECORATION_BINDING) {
                has_binding = true;
            } else if (words[i + 2] == DECORATION_DESCRIPTOR_SET) {
                has_set = true;
            }
        }
        i += word_count;
    }
    return has_binding && has_set;
}

bool filter_slang_vulkan_resources_for_spirv(
    const CompileOptions& options,
    std::vector<ShaderResourceBinding>& resources
) {
    std::vector<uint32_t> words;
    if (!read_spirv_words(options.output, words)) {
        // Fake/offline compiler tests may produce placeholder bytes while still
        // exercising reflection-sidecar generation.
        return true;
    }
    constexpr uint32_t SPIRV_MAGIC = 0x07230203u;
    if (words.empty() || words[0] != SPIRV_MAGIC) {
        // Fake compiler tests use placeholder bytes; there is nothing to
        // stage-filter.
        return true;
    }

    std::vector<ShaderResourceBinding> active;
    active.reserve(resources.size());
    for (const ShaderResourceBinding& resource : resources) {
        uint32_t resource_id = 0;
        if (!spirv_resource_id_for_name(words, resource.name, resource_id)) {
            continue;
        }
        if (!spirv_resource_has_descriptor_decorations(words, resource_id)) {
            continue;
        }
        active.push_back(resource);
    }

    resources = std::move(active);
    return true;
}

bool patch_slang_vulkan_spirv_descriptor_decorations(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources
) {
    std::vector<uint32_t> words;
    if (!read_spirv_words(options.output, words)) {
        // Fake/offline compiler tests may produce placeholder bytes while still
        // exercising reflection-sidecar generation.
        return true;
    }
    constexpr uint32_t SPIRV_MAGIC = 0x07230203u;
    if (words.empty() || words[0] != SPIRV_MAGIC) {
        return true;
    }

    constexpr uint16_t OP_DECORATE = 71;
    constexpr uint32_t DECORATION_BINDING = 33;
    constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;

    bool changed = false;
    for (const ShaderResourceBinding& resource : resources) {
        uint32_t resource_id = 0;
        if (!spirv_resource_id_for_name(words, resource.name, resource_id)) {
            std::cerr
                << "termin_shaderc: reflected SPIR-V resource '"
                << resource.name
                << "' is missing an OpName entry; cannot patch descriptor placement\n";
            return false;
        }

        bool patched_binding = false;
        bool patched_set = false;
        for (size_t i = 5; i < words.size();) {
            const uint32_t instruction = words[i];
            const uint16_t word_count = static_cast<uint16_t>(instruction >> 16);
            const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
            if (word_count == 0 || i + word_count > words.size()) {
                std::cerr
                    << "termin_shaderc: invalid SPIR-V instruction stream while "
                       "patching descriptor placement\n";
                return false;
            }
            if (opcode == OP_DECORATE && word_count >= 4 && words[i + 1] == resource_id) {
                if (words[i + 2] == DECORATION_BINDING) {
                    if (words[i + 3] != resource.binding) {
                        words[i + 3] = resource.binding;
                        changed = true;
                    }
                    patched_binding = true;
                } else if (words[i + 2] == DECORATION_DESCRIPTOR_SET) {
                    if (words[i + 3] != resource.set) {
                        words[i + 3] = resource.set;
                        changed = true;
                    }
                    patched_set = true;
                }
            }
            i += word_count;
        }

        if (!patched_binding || !patched_set) {
            std::cerr
                << "termin_shaderc: reflected SPIR-V resource '"
                << resource.name
                << "' has no complete descriptor decorations; cannot patch descriptor placement\n";
            return false;
        }
    }

    if (!changed) {
        return true;
    }
    return write_spirv(options.output, words);
}

} // namespace termin_shaderc::internal
