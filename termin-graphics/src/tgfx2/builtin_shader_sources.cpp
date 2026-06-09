#include "tgfx2/builtin_shader_sources.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <tcbase/trent/json.h>

namespace tgfx {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void add_root(std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
    if (root.empty()) {
        return;
    }
    roots.push_back(root);
}

std::vector<std::filesystem::path> builtin_shader_roots() {
    std::vector<std::filesystem::path> roots;

    if (const char* explicit_root = std::getenv("TERMIN_BUILTIN_SHADER_ROOT")) {
        add_root(roots, explicit_root);
    }

    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        add_root(roots, std::filesystem::path(sdk) / "share" / "termin" / "builtin_shaders");
    }

#ifdef TERMIN_GRAPHICS_SOURCE_DIR
    add_root(
        roots,
        std::filesystem::path(TERMIN_GRAPHICS_SOURCE_DIR) / "resources" / "builtin_shaders");
#endif

    add_root(roots, std::filesystem::path("termin-graphics") / "resources" / "builtin_shaders");
    add_root(roots, std::filesystem::path("..") / "termin-graphics" / "resources" / "builtin_shaders");
    return roots;
}

std::string roots_for_log(const std::vector<std::filesystem::path>& roots) {
    std::string text;
    for (const auto& root : roots) {
        if (!text.empty()) {
            text += ", ";
        }
        text += root.string();
    }
    return text;
}

const nos::trent* dict_get(const nos::trent& value, const char* key) {
    if (value.get_type() != nos::trent_type::dict || !value.contains(key)) {
        return nullptr;
    }
    return &value[key];
}

std::string string_field(const nos::trent& value, const char* key) {
    const nos::trent* field = dict_get(value, key);
    if (!field || field->get_type() != nos::trent_type::string) {
        return {};
    }
    return field->as_string();
}

std::optional<nos::trent> load_builtin_shader_catalog() {
    const std::string catalog_text =
        load_builtin_shader_source("engine-shader-catalog.json", "engine-shader-catalog");
    if (catalog_text.empty()) {
        return std::nullopt;
    }

    try {
        return nos::json::parse(catalog_text);
    } catch (const std::exception& exc) {
        tc::Log::error("[BuiltInShaderCatalog] Failed to parse catalog: %s", exc.what());
        return std::nullopt;
    }
}

std::optional<nos::trent> find_builtin_shader_catalog_entry(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        tc::Log::error("[BuiltInShaderCatalog] Missing shader uuid");
        return std::nullopt;
    }

    std::optional<nos::trent> catalog = load_builtin_shader_catalog();
    if (!catalog) {
        return std::nullopt;
    }

    const nos::trent* shaders = dict_get(*catalog, "shaders");
    if (!shaders || shaders->get_type() != nos::trent_type::list) {
        tc::Log::error("[BuiltInShaderCatalog] Catalog has no shader list");
        return std::nullopt;
    }

    for (const nos::trent& entry : shaders->as_list()) {
        if (string_field(entry, "uuid") == uuid) {
            return entry;
        }
    }

    tc::Log::error("[BuiltInShaderCatalog] No entry for shader uuid '%s'", uuid);
    return std::nullopt;
}

std::string stage_path(const nos::trent& entry, const char* stage_name) {
    const nos::trent* stages = dict_get(entry, "stages");
    if (!stages || stages->get_type() != nos::trent_type::dict) {
        return {};
    }
    const nos::trent* stage = dict_get(*stages, stage_name);
    if (!stage) {
        return {};
    }
    if (stage->get_type() == nos::trent_type::string) {
        return stage->as_string();
    }
    if (stage->get_type() != nos::trent_type::dict) {
        return {};
    }
    return string_field(*stage, "path");
}

} // namespace

std::string load_builtin_shader_source(const char* filename, const char* debug_name) {
    if (!filename || filename[0] == '\0') {
        tc::Log::error("[BuiltInShaderSource] Missing filename for '%s'",
                       debug_name ? debug_name : "<unnamed>");
        return {};
    }

    const auto roots = builtin_shader_roots();
    for (const auto& root : roots) {
        const std::filesystem::path path = root / filename;
        std::string source = read_text_file(path);
        if (!source.empty()) {
            return source;
        }
    }

    tc::Log::error(
        "[BuiltInShaderSource] Failed to load '%s' for '%s'. Searched: %s",
        filename,
        debug_name ? debug_name : "<unnamed>",
        roots_for_log(roots).c_str());
    return {};
}

tc_shader_handle register_builtin_fragment_shader(
    const char* filename,
    const char* name,
    const char* uuid)
{
    const std::string source = load_builtin_shader_source(filename, name);
    if (source.empty()) {
        return tc_shader_handle_invalid();
    }
    return tc_shader_register_static_uuid(nullptr, source.c_str(), nullptr, name, uuid);
}

tc_shader_handle register_builtin_vertex_fragment_shader(
    const char* vertex_filename,
    const char* fragment_filename,
    const char* name,
    const char* uuid)
{
    const std::string vertex_source = load_builtin_shader_source(vertex_filename, name);
    if (vertex_source.empty()) {
        return tc_shader_handle_invalid();
    }

    const std::string fragment_source = load_builtin_shader_source(fragment_filename, name);
    if (fragment_source.empty()) {
        return tc_shader_handle_invalid();
    }

    return tc_shader_register_static_uuid(
        vertex_source.c_str(), fragment_source.c_str(), nullptr, name, uuid);
}

tc_shader_handle register_builtin_shader_from_catalog(const char* uuid) {
    std::optional<nos::trent> entry = find_builtin_shader_catalog_entry(uuid);
    if (!entry) {
        return tc_shader_handle_invalid();
    }

    const std::string language = string_field(*entry, "language");
    if (language != "glsl") {
        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' has unsupported live stage language '%s'",
            uuid ? uuid : "<null>",
            language.c_str());
        return tc_shader_handle_invalid();
    }

    const std::string name = string_field(*entry, "name");
    if (name.empty()) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' has no name", uuid);
        return tc_shader_handle_invalid();
    }

    const std::string vertex_path = stage_path(*entry, "vertex");
    const std::string fragment_path = stage_path(*entry, "fragment");
    if (vertex_path.empty() && fragment_path.empty()) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' has no live vertex/fragment stage", uuid);
        return tc_shader_handle_invalid();
    }

    std::string vertex_source;
    if (!vertex_path.empty()) {
        vertex_source = load_builtin_shader_source(vertex_path.c_str(), name.c_str());
        if (vertex_source.empty()) {
            return tc_shader_handle_invalid();
        }
    }

    std::string fragment_source;
    if (!fragment_path.empty()) {
        fragment_source = load_builtin_shader_source(fragment_path.c_str(), name.c_str());
        if (fragment_source.empty()) {
            return tc_shader_handle_invalid();
        }
    }

    return tc_shader_register_static_uuid(
        vertex_source.empty() ? nullptr : vertex_source.c_str(),
        fragment_source.empty() ? nullptr : fragment_source.c_str(),
        nullptr,
        name.c_str(),
        uuid);
}

BuiltinShaderProgramSource load_builtin_shader_program_from_catalog(const char* uuid) {
    std::optional<nos::trent> entry = find_builtin_shader_catalog_entry(uuid);
    if (!entry) {
        return {};
    }

    const std::string language = string_field(*entry, "language");
    if (language != "shader") {
        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' is not a shader program entry",
            uuid ? uuid : "<null>");
        return {};
    }

    const std::string name = string_field(*entry, "name");
    if (name.empty()) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' has no name", uuid);
        return {};
    }

    const nos::trent* program = dict_get(*entry, "program");
    if (!program || program->get_type() != nos::trent_type::dict) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' has no program object", uuid);
        return {};
    }

    const std::string path = string_field(*program, "path");
    if (path.empty()) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' program has no path", uuid);
        return {};
    }

    BuiltinShaderProgramSource result;
    result.name = name;
    result.source = load_builtin_shader_source(path.c_str(), name.c_str());
    if (result.source.empty()) {
        return {};
    }
    return result;
}

} // namespace tgfx
