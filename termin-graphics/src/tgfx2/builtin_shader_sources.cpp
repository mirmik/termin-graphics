#include "tgfx2/builtin_shader_sources.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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

std::filesystem::path current_module_dir() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&current_module_dir),
            &module)) {
        return {};
    }

    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size() - 1) {
            return std::filesystem::path(buffer.data()).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&current_module_dir), &info) == 0 || !info.dli_fname) {
        return {};
    }
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

void add_share_roots_from_ancestors(
    std::vector<std::filesystem::path>& roots,
    std::filesystem::path base)
{
    for (int depth = 0; depth < 5 && !base.empty(); ++depth) {
        add_root(roots, base / "share" / "termin" / "builtin_shaders");
        const std::filesystem::path parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }
}

} // anonymous namespace

std::vector<std::filesystem::path> builtin_shader_roots() {
    std::vector<std::filesystem::path> roots;

    if (const char* explicit_root = std::getenv("TERMIN_BUILTIN_SHADER_ROOT")) {
        add_root(roots, explicit_root);
    }

    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        add_root(roots, std::filesystem::path(sdk) / "share" / "termin" / "builtin_shaders");
    }

    add_share_roots_from_ancestors(roots, current_module_dir());

    std::error_code current_path_error;
    const std::filesystem::path cwd = std::filesystem::current_path(current_path_error);
    if (!current_path_error) {
        add_root(roots, cwd / "share" / "termin" / "builtin_shaders");
    }

    return roots;
}

namespace {

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
    return value._get(key);
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

std::string load_builtin_shader_stage_source_from_catalog(
    const char* uuid,
    const char* stage_name)
{
    if (!stage_name || stage_name[0] == '\0') {
        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' requested with missing stage name",
            uuid ? uuid : "<null>");
        return {};
    }

    std::optional<nos::trent> entry = find_builtin_shader_catalog_entry(uuid);
    if (!entry) {
        return {};
    }

    const std::string language = string_field(*entry, "language");
    if (language != "glsl" && language != "slang") {
        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' has unsupported live stage language '%s'",
            uuid ? uuid : "<null>",
            language.c_str());
        return {};
    }

    const std::string name = string_field(*entry, "name");
    if (name.empty()) {
        tc::Log::error("[BuiltInShaderCatalog] Shader '%s' has no name", uuid);
        return {};
    }

    const std::string path = stage_path(*entry, stage_name);
    if (path.empty()) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' has no '%s' stage",
            uuid ? uuid : "<null>",
            stage_name);
        return {};
    }

    return load_builtin_shader_source(path.c_str(), name.c_str());
}

tc_shader_handle register_builtin_shader_from_catalog(const char* uuid) {
    std::optional<nos::trent> entry = find_builtin_shader_catalog_entry(uuid);
    if (!entry) {
        return tc_shader_handle_invalid();
    }

    const std::string language = string_field(*entry, "language");
    const bool is_glsl = (language == "glsl");
    const bool is_slang = (language == "slang");
    if (!is_glsl && !is_slang) {
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

    tc_shader_handle handle = tc_shader_register_static_uuid(
        vertex_source.empty() ? nullptr : vertex_source.c_str(),
        fragment_source.empty() ? nullptr : fragment_source.c_str(),
        nullptr,
        name.c_str(),
        uuid);

    if (!tc_shader_handle_is_invalid(handle) && is_slang) {
        tc_shader* shader = tc_shader_get(handle);
        if (shader) {
            tc_shader_set_language(shader, TC_SHADER_LANGUAGE_SLANG);
            tc_shader_set_artifact_policy(shader, TC_SHADER_ARTIFACT_REQUIRED);
        }
    }

    // Set per-stage entry points from catalog.
    {
        tc_shader* shader = tc_shader_get(handle);
        if (shader) {
            auto set_entry = [&](const char* stage_name, char** target) {
                const nos::trent* stages = dict_get(*entry, "stages");
                if (!stages || !stages->is_dict()) return;
                const nos::trent* stage_obj = dict_get(*stages, stage_name);
                if (!stage_obj || !stage_obj->is_dict()) return;
                std::string ename = string_field(*stage_obj, "entry");
                if (!ename.empty()) {
                    free(*target);
                    *target = strdup(ename.c_str());
                }
            };
            set_entry("vertex", &shader->vertex_entry);
            set_entry("fragment", &shader->fragment_entry);
        }
    }

    return handle;
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
