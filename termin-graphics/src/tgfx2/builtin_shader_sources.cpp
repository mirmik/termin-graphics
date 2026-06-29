#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/engine_shader_catalog.hpp"

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

struct BuiltinLocatedSource {
    std::string filename;
    std::string source;
};

std::optional<BuiltinLocatedSource> try_load_builtin_shader_source(
    const char* filename,
    const char* debug_name)
{
    if (!filename || filename[0] == '\0') {
        return std::nullopt;
    }

    const auto roots = builtin_shader_roots();
    for (const auto& root : roots) {
        const std::filesystem::path path = root / filename;
        std::error_code exists_error;
        if (!std::filesystem::is_regular_file(path, exists_error)) {
            continue;
        }

        std::string source = read_text_file(path);
        if (source.empty()) {
            tc::Log::error(
                "[BuiltInShaderSource] Built-in shader file '%s' for '%s' is empty or unreadable at '%s'",
                filename,
                debug_name ? debug_name : "<unnamed>",
                path.string().c_str());
            return std::nullopt;
        }

        BuiltinLocatedSource result;
        result.filename = filename;
        result.source = std::move(source);
        return result;
    }

    return std::nullopt;
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

std::string builtin_source_filename(const char* source_resource_path) {
    if (!source_resource_path || source_resource_path[0] == '\0') {
        return {};
    }
    std::string path(source_resource_path);
    constexpr const char* kPrefix = "builtin_shaders/";
    if (path.rfind(kPrefix, 0) == 0) {
        path.erase(0, std::strlen(kPrefix));
    }
    return path;
}

struct ConventionStageSource {
    std::string filename;
    std::string source;
    std::string language;
    std::string entry_point;
};

struct ConventionLiveShaderSource {
    std::string uuid;
    std::string name;
    std::string language;
    std::optional<ConventionStageSource> vertex_stage;
    std::optional<ConventionStageSource> fragment_stage;
};

std::optional<ConventionStageSource> try_convention_stage_file(
    const std::string& filename,
    const char* debug_name,
    const char* language,
    const char* entry_point)
{
    std::optional<BuiltinLocatedSource> source =
        try_load_builtin_shader_source(filename.c_str(), debug_name);
    if (!source) {
        return std::nullopt;
    }

    ConventionStageSource stage;
    stage.filename = source->filename;
    stage.source = std::move(source->source);
    stage.language = language ? language : "";
    stage.entry_point = entry_point ? entry_point : "";
    return stage;
}

std::optional<ConventionStageSource> load_convention_shader_stage(
    const char* uuid,
    const char* stage_name)
{
    if (!uuid || uuid[0] == '\0' || !stage_name || stage_name[0] == '\0') {
        return std::nullopt;
    }

    const std::string base(uuid);
    if (std::strcmp(stage_name, "vertex") == 0) {
        if (auto stage = try_convention_stage_file(
                base + ".vert.slang", uuid, "slang", "vs_main")) {
            return stage;
        }
        if (auto stage = try_convention_stage_file(
                base + ".slang", uuid, "slang", "vs_main")) {
            return stage;
        }
        if (auto stage = try_convention_stage_file(
                base + ".vert.glsl", uuid, "glsl", "")) {
            return stage;
        }
        return try_convention_stage_file(base + ".glsl", uuid, "glsl", "");
    }

    if (std::strcmp(stage_name, "fragment") == 0) {
        if (auto stage = try_convention_stage_file(
                base + ".frag.slang", uuid, "slang", "fs_main")) {
            return stage;
        }
        if (auto stage = try_convention_stage_file(
                base + ".slang", uuid, "slang", "fs_main")) {
            return stage;
        }
        if (auto stage = try_convention_stage_file(
                base + ".frag.glsl", uuid, "glsl", "")) {
            return stage;
        }
        return try_convention_stage_file(base + ".glsl", uuid, "glsl", "");
    }

    return std::nullopt;
}

std::optional<ConventionLiveShaderSource> load_convention_live_shader(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        return std::nullopt;
    }

    std::optional<ConventionStageSource> vertex_stage =
        load_convention_shader_stage(uuid, "vertex");
    std::optional<ConventionStageSource> fragment_stage =
        load_convention_shader_stage(uuid, "fragment");
    if (!vertex_stage && !fragment_stage) {
        return std::nullopt;
    }

    ConventionLiveShaderSource shader;
    shader.uuid = uuid;
    shader.name = uuid;
    shader.vertex_stage = std::move(vertex_stage);
    shader.fragment_stage = std::move(fragment_stage);
    shader.language = shader.vertex_stage
        ? shader.vertex_stage->language
        : shader.fragment_stage->language;

    if (shader.vertex_stage && shader.fragment_stage &&
            shader.vertex_stage->language != shader.fragment_stage->language) {
        tc::Log::error(
            "[BuiltInShaderConvention] Shader '%s' mixes vertex language '%s' with fragment language '%s'",
            uuid,
            shader.vertex_stage->language.c_str(),
            shader.fragment_stage->language.c_str());
        shader.language.clear();
    }

    return shader;
}

std::optional<BuiltinShaderProgramSource> load_convention_shader_program(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        return std::nullopt;
    }

    const std::string filename = std::string(uuid) + ".shader";
    std::optional<BuiltinLocatedSource> source =
        try_load_builtin_shader_source(filename.c_str(), uuid);
    if (!source) {
        return std::nullopt;
    }

    BuiltinShaderProgramSource result;
    result.name = uuid;
    result.source = std::move(source->source);
    return result;
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

const EngineShaderStageSource* engine_program_stage(
    const EngineShaderProgramSource& program,
    const char* stage_name)
{
    if (!stage_name || stage_name[0] == '\0') {
        return nullptr;
    }
    if (std::strcmp(stage_name, "vertex") == 0) {
        return program.vertex_stage;
    }
    if (std::strcmp(stage_name, "fragment") == 0) {
        return program.fragment_stage;
    }
    return nullptr;
}

const EngineShaderStageSource* standalone_engine_stage(
    const char* uuid,
    const char* stage_name)
{
    if (!stage_name || stage_name[0] == '\0') {
        return nullptr;
    }
    if (std::strcmp(stage_name, "vertex") == 0) {
        return find_engine_shader_stage(uuid, ShaderStage::Vertex);
    }
    if (std::strcmp(stage_name, "fragment") == 0) {
        return find_engine_shader_stage(uuid, ShaderStage::Fragment);
    }
    return nullptr;
}

std::string load_engine_program_stage_source(
    const EngineShaderProgramSource& program,
    const EngineShaderStageSource& stage)
{
    const std::string filename = builtin_source_filename(stage.source_resource_path);
    if (filename.empty()) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Typed shader '%s' stage has no source path",
            program.uuid ? program.uuid : "<null>");
        return {};
    }
    return load_builtin_shader_source(filename.c_str(), program.name);
}

tc_shader_handle register_engine_shader_program(
    const EngineShaderProgramSource& program)
{
    const bool is_glsl = program.language && std::strcmp(program.language, "glsl") == 0;
    const bool is_slang = program.language && std::strcmp(program.language, "slang") == 0;
    if (!is_glsl && !is_slang) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Typed shader '%s' has unsupported language '%s'",
            program.uuid ? program.uuid : "<null>",
            program.language ? program.language : "<null>");
        return tc_shader_handle_invalid();
    }

    if (!program.name || program.name[0] == '\0') {
        tc::Log::error(
            "[BuiltInShaderCatalog] Typed shader '%s' has no name",
            program.uuid ? program.uuid : "<null>");
        return tc_shader_handle_invalid();
    }

    if (!program.vertex_stage && !program.fragment_stage) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Typed shader '%s' has no live vertex/fragment stage",
            program.uuid ? program.uuid : "<null>");
        return tc_shader_handle_invalid();
    }

    std::string vertex_source;
    if (program.vertex_stage) {
        vertex_source = load_engine_program_stage_source(program, *program.vertex_stage);
        if (vertex_source.empty()) {
            return tc_shader_handle_invalid();
        }
    }

    std::string fragment_source;
    if (program.fragment_stage) {
        fragment_source = load_engine_program_stage_source(program, *program.fragment_stage);
        if (fragment_source.empty()) {
            return tc_shader_handle_invalid();
        }
    }

    const tc_shader_language shader_language =
        is_slang ? TC_SHADER_LANGUAGE_SLANG : TC_SHADER_LANGUAGE_GLSL;
    const tc_shader_artifact_policy artifact_policy =
        is_slang ? TC_SHADER_ARTIFACT_REQUIRED : TC_SHADER_ARTIFACT_OPTIONAL;

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        vertex_source.empty() ? nullptr : vertex_source.c_str(),
        fragment_source.empty() ? nullptr : fragment_source.c_str(),
        nullptr,
        program.name,
        /*source_path=*/nullptr,
        program.uuid,
        shader_language,
        artifact_policy,
        program.vertex_stage && program.vertex_stage->entry_point
            ? program.vertex_stage->entry_point
            : nullptr,
        program.fragment_stage && program.fragment_stage->entry_point
            ? program.fragment_stage->entry_point
            : nullptr,
        nullptr);

    if (tc_shader_handle_is_invalid(handle)) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Failed to register typed shader '%s'",
            program.uuid ? program.uuid : "<null>");
        return handle;
    }

    tc_shader* shader = tc_shader_get(handle);
    if (!shader) {
        tc::Log::error(
            "[BuiltInShaderCatalog] Registered typed shader '%s' but registry lookup returned null",
            program.uuid ? program.uuid : "<null>");
        return tc_shader_handle_invalid();
    }

    if (!shader->is_static) {
        shader->is_static = 1;
        tc_shader_add_ref(shader);
    }
    return handle;
}

tc_shader_handle register_convention_live_shader(
    const ConventionLiveShaderSource& shader_source)
{
    const bool is_glsl = (shader_source.language == "glsl");
    const bool is_slang = (shader_source.language == "slang");
    if (!is_glsl && !is_slang) {
        tc::Log::error(
            "[BuiltInShaderConvention] Shader '%s' has unsupported language '%s'",
            shader_source.uuid.c_str(),
            shader_source.language.c_str());
        return tc_shader_handle_invalid();
    }

    const tc_shader_language shader_language =
        is_slang ? TC_SHADER_LANGUAGE_SLANG : TC_SHADER_LANGUAGE_GLSL;
    const tc_shader_artifact_policy artifact_policy =
        is_slang ? TC_SHADER_ARTIFACT_REQUIRED : TC_SHADER_ARTIFACT_OPTIONAL;

    tc_shader_handle handle = tc_shader_from_sources_with_entries_ex(
        shader_source.vertex_stage
            ? shader_source.vertex_stage->source.c_str()
            : nullptr,
        shader_source.fragment_stage
            ? shader_source.fragment_stage->source.c_str()
            : nullptr,
        nullptr,
        shader_source.name.c_str(),
        /*source_path=*/nullptr,
        shader_source.uuid.c_str(),
        shader_language,
        artifact_policy,
        shader_source.vertex_stage && !shader_source.vertex_stage->entry_point.empty()
            ? shader_source.vertex_stage->entry_point.c_str()
            : nullptr,
        shader_source.fragment_stage && !shader_source.fragment_stage->entry_point.empty()
            ? shader_source.fragment_stage->entry_point.c_str()
            : nullptr,
        nullptr);

    if (tc_shader_handle_is_invalid(handle)) {
        tc::Log::error(
            "[BuiltInShaderConvention] Failed to register shader '%s'",
            shader_source.uuid.c_str());
        return handle;
    }

    tc_shader* shader = tc_shader_get(handle);
    if (!shader) {
        tc::Log::error(
            "[BuiltInShaderConvention] Registered shader '%s' but registry lookup returned null",
            shader_source.uuid.c_str());
        return tc_shader_handle_invalid();
    }

    if (shader_source.fragment_stage &&
            (!shader->fragment_source || shader->fragment_source[0] == '\0')) {
        tc::Log::error(
            "[BuiltInShaderConvention] Registered shader '%s' has empty fragment source",
            shader_source.uuid.c_str());
        return tc_shader_handle_invalid();
    }

    if (!shader->is_static) {
        shader->is_static = 1;
        tc_shader_add_ref(shader);
    }
    return handle;
}

} // namespace

std::string load_builtin_shader_source(const char* filename, const char* debug_name) {
    if (!filename || filename[0] == '\0') {
        tc::Log::error("[BuiltInShaderSource] Missing filename for '%s'",
                       debug_name ? debug_name : "<unnamed>");
        return {};
    }

    if (std::optional<BuiltinLocatedSource> source =
            try_load_builtin_shader_source(filename, debug_name)) {
        return source->source;
    }

    const auto roots = builtin_shader_roots();
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

    if (const EngineShaderProgramSource* program = find_engine_shader_program(uuid)) {
        const EngineShaderStageSource* stage = engine_program_stage(*program, stage_name);
        if (!stage) {
            tc::Log::error(
                "[BuiltInShaderCatalog] Typed shader '%s' has no '%s' stage",
                uuid ? uuid : "<null>",
                stage_name);
            return {};
        }
        return load_engine_program_stage_source(*program, *stage);
    }
    if (const EngineShaderStageSource* stage = standalone_engine_stage(uuid, stage_name)) {
        return load_builtin_shader_source(
            builtin_source_filename(stage->source_resource_path).c_str(),
            stage->name);
    }
    if (std::optional<ConventionStageSource> stage =
            load_convention_shader_stage(uuid, stage_name)) {
        return stage->source;
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
    if (const EngineShaderProgramSource* program = find_engine_shader_program(uuid)) {
        return register_engine_shader_program(*program);
    }
    if (std::optional<ConventionLiveShaderSource> shader_source =
            load_convention_live_shader(uuid)) {
        return register_convention_live_shader(*shader_source);
    }

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

    const tc_shader_language shader_language =
        is_slang ? TC_SHADER_LANGUAGE_SLANG : TC_SHADER_LANGUAGE_GLSL;
    const tc_shader_artifact_policy artifact_policy =
        is_slang ? TC_SHADER_ARTIFACT_REQUIRED : TC_SHADER_ARTIFACT_OPTIONAL;

    auto artifact_only_stage_source = [&](const char* stage_name, const std::string& path) {
        std::string marker = "/* termin artifact-only builtin shader stage: ";
        marker += uuid ? uuid : "<null>";
        marker += ":";
        marker += stage_name ? stage_name : "<stage>";
        marker += " path=";
        marker += path;
        marker += " */\n";
        return marker;
    };

    auto load_catalog_stage_source = [&](const char* stage_name, const std::string& path) {
        if (path.empty()) {
            return std::string();
        }

        if (std::optional<BuiltinLocatedSource> source =
                try_load_builtin_shader_source(path.c_str(), name.c_str())) {
            return std::move(source->source);
        }

        if (artifact_policy == TC_SHADER_ARTIFACT_REQUIRED) {
            return artifact_only_stage_source(stage_name, path);
        }

        tc::Log::error(
            "[BuiltInShaderCatalog] Shader '%s' failed to load %s stage '%s'. Roots: %s",
            uuid ? uuid : "<null>",
            stage_name ? stage_name : "<stage>",
            path.c_str(),
            builtin_shader_roots_for_log().c_str());
        return std::string();
    };

    std::string vertex_source = load_catalog_stage_source("vertex", vertex_path);
    if (!vertex_path.empty() && vertex_source.empty()) {
        return tc_shader_handle_invalid();
    }

    std::string fragment_source = load_catalog_stage_source("fragment", fragment_path);
    if (!fragment_path.empty() && fragment_source.empty()) {
        return tc_shader_handle_invalid();
    }

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
    }

    return handle;
}

BuiltinShaderProgramSource load_builtin_shader_program_from_catalog(const char* uuid) {
    if (std::optional<BuiltinShaderProgramSource> program =
            load_convention_shader_program(uuid)) {
        return *program;
    }

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
