#include "tgfx2/builtin_shader_sources.hpp"

#include <cstdlib>
#include <cstring>
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

std::string builtin_shader_roots_for_log() {
    return roots_for_log(builtin_shader_roots());
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

uint32_t resource_kind_from_catalog(const std::string& kind) {
    if (kind == "constant_buffer" || kind == "uniform_buffer") {
        return TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    }
    if (kind == "texture") {
        return TC_SHADER_RESOURCE_TEXTURE;
    }
    if (kind == "combined_sampler2d") {
        return TC_SHADER_RESOURCE_TEXTURE;
    }
    if (kind == "sampler") {
        return TC_SHADER_RESOURCE_SAMPLER;
    }
    if (kind == "storage_buffer") {
        return TC_SHADER_RESOURCE_STORAGE_BUFFER;
    }
    if (kind == "storage_texture") {
        return TC_SHADER_RESOURCE_STORAGE_TEXTURE;
    }
    return TC_SHADER_RESOURCE_NONE;
}

uint32_t resource_scope_from_catalog(const std::string& scope) {
    if (scope == "frame") {
        return TC_SHADER_RESOURCE_SCOPE_FRAME;
    }
    if (scope == "pass") {
        return TC_SHADER_RESOURCE_SCOPE_PASS;
    }
    if (scope == "material") {
        return TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    }
    if (scope == "draw") {
        return TC_SHADER_RESOURCE_SCOPE_DRAW;
    }
    if (scope == "transient") {
        return TC_SHADER_RESOURCE_SCOPE_TRANSIENT;
    }
    return TC_SHADER_RESOURCE_SCOPE_UNKNOWN;
}

uint32_t binding_slot_for_catalog_resource(
    const std::string& name,
    uint32_t kind,
    uint32_t scope,
    uint32_t& next_material_texture_binding,
    uint32_t& next_transient_resource_binding)
{
    if (kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
        if (scope == TC_SHADER_RESOURCE_SCOPE_FRAME &&
            (name == "per_frame" || name == "u_per_frame")) {
            return 2;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_PASS && name == "lighting") {
            return 0;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_PASS && name == "shadow_block") {
            return 3;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_PASS) {
            return 0;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_MATERIAL && name == "material") {
            return 1;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_DRAW &&
            (name == "bone_block" || name == "BoneBlock")) {
            return 16;
        }
        if (scope == TC_SHADER_RESOURCE_SCOPE_DRAW) {
            return 24;
        }
    }
    if (kind == TC_SHADER_RESOURCE_TEXTURE &&
        scope == TC_SHADER_RESOURCE_SCOPE_PASS &&
        (name == "shadow_maps" || name == "u_shadow_map" || name == "u_shadow_maps")) {
        return 8;
    }
    if (kind == TC_SHADER_RESOURCE_TEXTURE &&
        scope == TC_SHADER_RESOURCE_SCOPE_MATERIAL) {
        if (next_material_texture_binding == 8) {
            ++next_material_texture_binding;
        }
        return next_material_texture_binding++;
    }
    if ((kind == TC_SHADER_RESOURCE_TEXTURE ||
         kind == TC_SHADER_RESOURCE_STORAGE_TEXTURE ||
         kind == TC_SHADER_RESOURCE_SAMPLER) &&
        scope == TC_SHADER_RESOURCE_SCOPE_TRANSIENT) {
        return next_transient_resource_binding++;
    }
    if (kind == TC_SHADER_RESOURCE_STORAGE_BUFFER &&
        scope == TC_SHADER_RESOURCE_SCOPE_DRAW) {
        return 25;
    }
    return 0;
}

void apply_catalog_resource_layout(tc_shader* shader, const nos::trent& entry) {
    if (!shader) {
        return;
    }

    const nos::trent* resources = dict_get(entry, "resources");
    if (!resources || resources->get_type() != nos::trent_type::list) {
        return;
    }

    std::vector<tc_shader_resource_binding> bindings;
    uint32_t next_material_texture_binding = 4;
    uint32_t next_transient_resource_binding = 32;

    for (const nos::trent& resource_data : resources->as_list()) {
        if (resource_data.get_type() != nos::trent_type::dict) {
            continue;
        }

        const std::string name = string_field(resource_data, "name");
        const uint32_t kind = resource_kind_from_catalog(string_field(resource_data, "kind"));
        uint32_t scope = resource_scope_from_catalog(string_field(resource_data, "scope"));
        if (scope == TC_SHADER_RESOURCE_SCOPE_UNKNOWN) {
            scope = TC_SHADER_RESOURCE_SCOPE_TRANSIENT;
        }
        if (name.empty() || kind == TC_SHADER_RESOURCE_NONE) {
            tc::Log::warn(
                "[BuiltInShaderCatalog] Shader '%s' has invalid resource entry name='%s'",
                shader->uuid,
                name.c_str());
            continue;
        }

        tc_shader_resource_binding binding{};
        std::strncpy(binding.name, name.c_str(), TC_SHADER_RESOURCE_NAME_MAX - 1);
        binding.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
        binding.kind = kind;
        binding.scope = scope;
        binding.set = TC_SHADER_RESOURCE_SET_DEFAULT;
        binding.binding = binding_slot_for_catalog_resource(
            name,
            kind,
            scope,
            next_material_texture_binding,
            next_transient_resource_binding);
        binding.stage_mask = TC_SHADER_STAGE_ALL_GRAPHICS;
        binding.size = 0;
        bindings.push_back(binding);
    }

    if (bindings.empty()) {
        tc_shader_mark_resource_layout_known(shader);
        return;
    }

    tc_shader_set_resource_layout(
        shader,
        bindings.data(),
        static_cast<uint32_t>(bindings.size()));
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
            tc::Log::error(
                "[BuiltInShaderCatalog] Shader '%s' failed to load vertex stage '%s'. Roots: %s",
                uuid ? uuid : "<null>",
                vertex_path.c_str(),
                builtin_shader_roots_for_log().c_str());
            return tc_shader_handle_invalid();
        }
    }

    std::string fragment_source;
    if (!fragment_path.empty()) {
        fragment_source = load_builtin_shader_source(fragment_path.c_str(), name.c_str());
        if (fragment_source.empty()) {
            tc::Log::error(
                "[BuiltInShaderCatalog] Shader '%s' failed to load fragment stage '%s'. Roots: %s",
                uuid ? uuid : "<null>",
                fragment_path.c_str(),
                builtin_shader_roots_for_log().c_str());
            return tc_shader_handle_invalid();
        }
    }

    const tc_shader_language shader_language =
        is_slang ? TC_SHADER_LANGUAGE_SLANG : TC_SHADER_LANGUAGE_GLSL;
    const tc_shader_artifact_policy artifact_policy =
        is_slang ? TC_SHADER_ARTIFACT_REQUIRED : TC_SHADER_ARTIFACT_OPTIONAL;

    auto stage_entry = [&](const char* stage_name) -> std::string {
        const nos::trent* stages = dict_get(*entry, "stages");
        if (!stages || !stages->is_dict()) return {};
        const nos::trent* stage_obj = dict_get(*stages, stage_name);
        if (!stage_obj || !stage_obj->is_dict()) return {};
        return string_field(*stage_obj, "entry");
    };
    const std::string vertex_entry = stage_entry("vertex");
    const std::string fragment_entry = stage_entry("fragment");

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        vertex_source.empty() ? nullptr : vertex_source.c_str(),
        fragment_source.empty() ? nullptr : fragment_source.c_str(),
        nullptr,
        name.c_str(),
        /*source_path=*/nullptr,
        uuid,
        shader_language,
        artifact_policy,
        vertex_entry.empty() ? nullptr : vertex_entry.c_str(),
        fragment_entry.empty() ? nullptr : fragment_entry.c_str(),
        nullptr);

    if (tc_shader_handle_is_invalid(handle)) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Failed to register shader '%s' "
            "(name='%s', language='%s', vertex='%s' size=%zu, fragment='%s' size=%zu, roots=%s)",
            uuid ? uuid : "<null>",
            name.c_str(),
            language.c_str(),
            vertex_path.c_str(),
            vertex_source.size(),
            fragment_path.c_str(),
            fragment_source.size(),
            builtin_shader_roots_for_log().c_str());
        return handle;
    }

    {
        tc_shader* shader = tc_shader_get(handle);
        if (!shader) {
            tc::Log::error(
                "[BuiltInShaderCatalog] Registered shader '%s' but registry lookup returned null",
                uuid ? uuid : "<null>");
            return tc_shader_handle_invalid();
        }

        if (!fragment_path.empty() && (!shader->fragment_source || shader->fragment_source[0] == '\0')) {
            tc::Log::error(
                "[BuiltInShaderCatalog] Registered shader '%s' has empty fragment source "
                "(name='%s', language='%s', fragment='%s' size=%zu)",
                uuid ? uuid : "<null>",
                name.c_str(),
                language.c_str(),
                fragment_path.c_str(),
                fragment_source.size());
            return tc_shader_handle_invalid();
        }

        if (!shader->is_static) {
            shader->is_static = 1;
            tc_shader_add_ref(shader);
        }
        apply_catalog_resource_layout(shader, *entry);
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
