// tc_shader_bridge.cpp - TcShader ↔ tgfx2 interop.
#include "tgfx2/tc_shader_bridge.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include <tcbase/tc_log.h>
}

namespace termin {

static std::string g_shader_artifact_root;
static std::string g_shader_cache_root;
static std::string g_shader_compiler_path;
static bool g_shader_dev_compile_enabled = false;

void tgfx2_set_shader_artifact_root(const char* root) {
    g_shader_artifact_root = root ? root : "";
}

const char* tgfx2_get_shader_artifact_root(void) {
    if (!g_shader_artifact_root.empty()) {
        return g_shader_artifact_root.c_str();
    }
    const char* env = std::getenv("TERMIN_SHADER_ARTIFACT_ROOT");
    return env ? env : "";
}

void tgfx2_set_shader_cache_root(const char* root) {
    g_shader_cache_root = root ? root : "";
}

const char* tgfx2_get_shader_cache_root(void) {
    if (!g_shader_cache_root.empty()) {
        return g_shader_cache_root.c_str();
    }
    const char* env = std::getenv("TERMIN_SHADER_CACHE_ROOT");
    return env ? env : "";
}

void tgfx2_set_shader_compiler_path(const char* path) {
    g_shader_compiler_path = path ? path : "";
}

const char* tgfx2_get_shader_compiler_path(void) {
    if (!g_shader_compiler_path.empty()) {
        return g_shader_compiler_path.c_str();
    }
    const char* env = std::getenv("TERMIN_SHADERC");
    return env ? env : "";
}

void tgfx2_set_shader_dev_compile_enabled(bool enabled) {
    g_shader_dev_compile_enabled = enabled;
}

bool tgfx2_get_shader_dev_compile_enabled(void) {
    if (g_shader_dev_compile_enabled) {
        return true;
    }
    const char* env = std::getenv("TERMIN_SHADER_DEV_COMPILE");
    return env && env[0] == '1';
}

static const char* stage_extension(tgfx::ShaderStage stage) {
    switch (stage) {
        case tgfx::ShaderStage::Vertex: return "vert";
        case tgfx::ShaderStage::Fragment: return "frag";
        case tgfx::ShaderStage::Geometry: return "geom";
        case tgfx::ShaderStage::Compute: return "comp";
    }
    return "spv";
}

static const char* backend_directory(tgfx::BackendType backend) {
    switch (backend) {
        case tgfx::BackendType::OpenGL: return "opengl";
        case tgfx::BackendType::Vulkan: return "vulkan";
        case tgfx::BackendType::D3D11: return "d3d11";
        case tgfx::BackendType::Metal:
        case tgfx::BackendType::Null:
            return "";
    }
    return "";
}

static const char* backend_stage_suffix(
    tgfx::BackendType backend,
    tgfx::ShaderStage stage
) {
    switch (backend) {
        case tgfx::BackendType::OpenGL:
        case tgfx::BackendType::Vulkan:
            return stage_extension(stage);
        case tgfx::BackendType::D3D11:
            switch (stage) {
                case tgfx::ShaderStage::Vertex: return "vs";
                case tgfx::ShaderStage::Fragment: return "ps";
                case tgfx::ShaderStage::Geometry: return "gs";
                case tgfx::ShaderStage::Compute: return "cs";
            }
            return "";
        case tgfx::BackendType::Metal:
        case tgfx::BackendType::Null:
            return "";
    }
    return "";
}

static const char* backend_artifact_extension(tgfx::BackendType backend) {
    switch (backend) {
        case tgfx::BackendType::OpenGL: return "glsl";
        case tgfx::BackendType::Vulkan: return "spv";
        case tgfx::BackendType::D3D11: return "cso";
        case tgfx::BackendType::Metal:
        case tgfx::BackendType::Null:
            return "";
    }
    return "";
}

static const char* stage_name(tgfx::ShaderStage stage) {
    switch (stage) {
        case tgfx::ShaderStage::Vertex: return "vertex";
        case tgfx::ShaderStage::Fragment: return "fragment";
        case tgfx::ShaderStage::Geometry: return "geometry";
        case tgfx::ShaderStage::Compute: return "compute";
    }
    return "";
}

static const char* backend_name(tgfx::BackendType backend) {
    switch (backend) {
        case tgfx::BackendType::OpenGL: return "opengl";
        case tgfx::BackendType::Vulkan: return "vulkan";
        case tgfx::BackendType::D3D11: return "d3d11";
        case tgfx::BackendType::Metal: return "metal";
        case tgfx::BackendType::Null: return "null";
    }
    return "";
}

static const char* shader_language_name(tc_shader_language language) {
    switch (language) {
        case TC_SHADER_LANGUAGE_GLSL: return "glsl";
        case TC_SHADER_LANGUAGE_SLANG: return "slang";
        case TC_SHADER_LANGUAGE_HLSL: return "hlsl";
    }
    return "";
}

static bool shader_language_from_name(const char* language_name, tc_shader_language& out) {
    if (!language_name || language_name[0] == '\0') {
        return false;
    }
    if (std::string(language_name) == "glsl") {
        out = TC_SHADER_LANGUAGE_GLSL;
        return true;
    }
    if (std::string(language_name) == "slang") {
        out = TC_SHADER_LANGUAGE_SLANG;
        return true;
    }
    if (std::string(language_name) == "hlsl") {
        out = TC_SHADER_LANGUAGE_HLSL;
        return true;
    }
    return false;
}

static const char* shader_source_extension(tc_shader_language language) {
    switch (language) {
        case TC_SHADER_LANGUAGE_SLANG: return "slang";
        case TC_SHADER_LANGUAGE_HLSL: return "hlsl";
        case TC_SHADER_LANGUAGE_GLSL: return "glsl";
    }
    return "glsl";
}

static const char* shader_stage_source(const tc_shader* shader, tgfx::ShaderStage stage) {
    if (!shader) return nullptr;
    switch (stage) {
        case tgfx::ShaderStage::Vertex: return shader->vertex_source;
        case tgfx::ShaderStage::Fragment: return shader->fragment_source;
        case tgfx::ShaderStage::Geometry: return shader->geometry_source;
        case tgfx::ShaderStage::Compute: return nullptr;
    }
    return nullptr;
}

static bool shader_language_target_supported(
    tc_shader_language language,
    tgfx::BackendType backend
) {
    if (language == TC_SHADER_LANGUAGE_GLSL) {
        return backend == tgfx::BackendType::Vulkan;
    }
    if (language == TC_SHADER_LANGUAGE_SLANG) {
        return backend == tgfx::BackendType::Vulkan ||
               backend == tgfx::BackendType::OpenGL;
    }
    return false;
}

static bool read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

static bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: failed to create directory '%s': %s",
               path.parent_path().string().c_str(),
               ec.message().c_str());
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: failed to open source '%s'",
               path.string().c_str());
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

static std::string fnv1a_hash_text(const char* text) {
    uint64_t hash = 1469598103934665603ULL;
    if (text) {
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
            hash ^= static_cast<uint64_t>(*p);
            hash *= 1099511628211ULL;
        }
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(out);
}

static std::filesystem::path shader_cache_source_path(
    const tc_shader* shader,
    tgfx::ShaderStage stage
) {
    const char* cache_root = tgfx2_get_shader_cache_root();
    if (cache_root && cache_root[0] != '\0') {
        return std::filesystem::path(cache_root) / "source" /
            (std::string(shader->uuid) + "." + stage_extension(stage) + "." +
             shader_source_extension((tc_shader_language)shader->language));
    }

    const char* artifact_root = tgfx2_get_shader_artifact_root();
    return std::filesystem::path(artifact_root) / ".build" / "shaders" / "source" /
        (std::string(shader->uuid) + "." + stage_extension(stage) + "." +
         shader_source_extension((tc_shader_language)shader->language));
}

static std::string artifact_metadata_text(
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage
) {
    std::ostringstream out;
    out << "source_hash=" << shader->source_hash << "\n";
    out << "language=" << shader_language_name((tc_shader_language)shader->language) << "\n";
    out << "target=" << backend_name(backend) << "\n";
    out << "stage=" << stage_name(stage) << "\n";
    out << "shader_uuid=" << shader->uuid << "\n";
    out << "shader_version=" << shader->version << "\n";
    return out.str();
}

static std::string engine_shader_artifact_metadata_text(
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend
) {
    std::ostringstream out;
    out << "source_hash=" << source_hash << "\n";
    out << "language=" << (shader.language ? shader.language : "") << "\n";
    out << "target=" << backend_name(backend) << "\n";
    out << "stage=" << stage_name(shader.stage) << "\n";
    out << "shader_uuid=" << (shader.uuid ? shader.uuid : "") << "\n";
    out << "shader_name=" << (shader.name ? shader.name : "") << "\n";
    return out.str();
}

static bool artifact_metadata_current(
    const std::filesystem::path& artifact_path,
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage
) {
    std::ifstream in(artifact_path.string() + ".meta", std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    return text == artifact_metadata_text(shader, backend, stage);
}

static bool engine_shader_artifact_metadata_current(
    const std::filesystem::path& artifact_path,
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend
) {
    std::ifstream in(artifact_path.string() + ".meta", std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    return text == engine_shader_artifact_metadata_text(shader, source_hash, backend);
}

static bool write_artifact_metadata(
    const std::filesystem::path& artifact_path,
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage
) {
    std::ofstream out(artifact_path.string() + ".meta", std::ios::binary);
    if (!out) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: failed to open metadata for '%s'",
               artifact_path.string().c_str());
        return false;
    }
    out << artifact_metadata_text(shader, backend, stage);
    return static_cast<bool>(out);
}

static bool write_engine_shader_artifact_metadata(
    const std::filesystem::path& artifact_path,
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend
) {
    std::ofstream out(artifact_path.string() + ".meta", std::ios::binary);
    if (!out) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: failed to open metadata for '%s'",
               artifact_path.string().c_str());
        return false;
    }
    out << engine_shader_artifact_metadata_text(shader, source_hash, backend);
    return static_cast<bool>(out);
}

static std::vector<std::string> split_paths(const char* value) {
    std::vector<std::string> paths;
    if (!value || value[0] == '\0') return paths;
    std::string text(value);
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) paths.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return paths;
}

static bool is_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec);
}

static std::optional<std::filesystem::path> resolve_shader_compiler() {
    const char* configured = tgfx2_get_shader_compiler_path();
    if (configured && configured[0] != '\0') {
        std::filesystem::path path(configured);
        if (is_existing_file(path)) {
            return path;
        }
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: configured termin_shaderc does not exist: %s",
               configured);
        return std::nullopt;
    }

    for (const std::string& dir : split_paths(std::getenv("PATH"))) {
#ifdef _WIN32
        std::filesystem::path exe_candidate = std::filesystem::path(dir) / "termin_shaderc.exe";
        if (is_existing_file(exe_candidate)) return exe_candidate;
#endif
        std::filesystem::path candidate = std::filesystem::path(dir) / "termin_shaderc";
        if (is_existing_file(candidate)) return candidate;
    }
    tc_log(TC_LOG_ERROR,
           "tgfx2 shader dev compile: termin_shaderc not found; set TERMIN_SHADERC or PATH");
    return std::nullopt;
}

static std::string quote_arg(const std::filesystem::path& value) {
    std::string text = value.string();
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"') out += "\\\"";
        else out.push_back(ch);
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') out += "'\\''";
        else out.push_back(ch);
    }
    out.push_back('\'');
    return out;
#endif
}

static bool compile_shader_artifact(
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    const std::filesystem::path& artifact_path
) {
    const tc_shader_language language = (tc_shader_language)shader->language;
    if (!shader_language_target_supported(language, backend)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: unsupported language/target for shader '%s': %s -> %s",
               shader->name ? shader->name : shader->uuid,
               shader_language_name(language),
               backend_name(backend));
        return false;
    }

    const char* source = shader_stage_source(shader, stage);
    if (!source || source[0] == '\0') {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: missing %s source for shader '%s'",
               stage_name(stage),
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    auto compiler = resolve_shader_compiler();
    if (!compiler) {
        return false;
    }

    const std::filesystem::path source_path = shader_cache_source_path(shader, stage);
    if (!write_text_file(source_path, source)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(artifact_path.parent_path(), ec);
    if (ec) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: failed to create artifact directory '%s': %s",
               artifact_path.parent_path().string().c_str(),
               ec.message().c_str());
        return false;
    }

    std::string cmd =
        quote_arg(*compiler) +
        " compile --language " + shader_language_name(language) +
        " --target " + backend_name(backend) +
        " --stage " + stage_name(stage) +
        " --entry main --input " + quote_arg(source_path) +
        " --output " + quote_arg(artifact_path) +
        " --debug-name " + quote_arg(std::filesystem::path(
            std::string(shader->name ? shader->name : shader->uuid) + ":" + stage_name(stage)));

    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: termin_shaderc failed for '%s' stage=%s target=%s rc=%d",
               shader->name ? shader->name : shader->uuid,
               stage_name(stage),
               backend_name(backend),
               rc);
        return false;
    }

    if (!is_existing_file(artifact_path)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: compiler did not produce '%s'",
               artifact_path.string().c_str());
        return false;
    }
    return write_artifact_metadata(artifact_path, shader, backend, stage);
}

static std::string builtin_source_filename(const char* source_resource_path) {
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

static std::filesystem::path engine_shader_cache_source_path(
    const tgfx::EngineShaderStageSource& shader,
    tc_shader_language language
) {
    const char* cache_root = tgfx2_get_shader_cache_root();
    if (cache_root && cache_root[0] != '\0') {
        return std::filesystem::path(cache_root) / "source" /
            (std::string(shader.uuid) + "." + stage_extension(shader.stage) + "." +
             shader_source_extension(language));
    }

    const char* artifact_root = tgfx2_get_shader_artifact_root();
    return std::filesystem::path(artifact_root) / ".build" / "shaders" / "source" /
        (std::string(shader.uuid) + "." + stage_extension(shader.stage) + "." +
         shader_source_extension(language));
}

static bool compile_engine_shader_stage_artifact(
    const tgfx::EngineShaderStageSource& shader,
    tc_shader_language language,
    tgfx::BackendType backend,
    const std::string& source,
    const std::string& source_hash,
    const std::filesystem::path& artifact_path
) {
    if (!shader_language_target_supported(language, backend)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: unsupported language/target for shader '%s': %s -> %s",
               shader.name ? shader.name : shader.uuid,
               shader_language_name(language),
               backend_name(backend));
        return false;
    }

    auto compiler = resolve_shader_compiler();
    if (!compiler) {
        return false;
    }

    const std::filesystem::path source_path = engine_shader_cache_source_path(shader, language);
    if (!write_text_file(source_path, source)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(artifact_path.parent_path(), ec);
    if (ec) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: failed to create artifact directory '%s': %s",
               artifact_path.parent_path().string().c_str(),
               ec.message().c_str());
        return false;
    }

    std::string cmd =
        quote_arg(*compiler) +
        " compile --language " + shader_language_name(language) +
        " --target " + backend_name(backend) +
        " --stage " + stage_name(shader.stage) +
        " --entry main --input " + quote_arg(source_path) +
        " --output " + quote_arg(artifact_path) +
        " --debug-name " + quote_arg(std::filesystem::path(
            std::string(shader.name ? shader.name : shader.uuid) + ":" + stage_name(shader.stage)));

    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: termin_shaderc failed for '%s' stage=%s target=%s rc=%d",
               shader.name ? shader.name : shader.uuid,
               stage_name(shader.stage),
               backend_name(backend),
               rc);
        return false;
    }

    if (!is_existing_file(artifact_path)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: compiler did not produce '%s'",
               artifact_path.string().c_str());
        return false;
    }
    return write_engine_shader_artifact_metadata(artifact_path, shader, source_hash, backend);
}

bool tgfx2_shader_artifact_path(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::string& out
) {
    const char* root = tgfx2_get_shader_artifact_root();
    if (!shader_uuid || shader_uuid[0] == '\0') {
        tc_log(TC_LOG_ERROR,
               "tgfx2_shader_artifact_path: missing shader_uuid='%s'",
               shader_uuid ? shader_uuid : "<null>");
        return false;
    }
    if (!root || root[0] == '\0') {
        return false;
    }

    const char* backend_dir = backend_directory(backend);
    const char* stage_suffix = backend_stage_suffix(backend, stage);
    const char* artifact_ext = backend_artifact_extension(backend);
    if (!backend_dir[0] || !stage_suffix[0] || !artifact_ext[0]) {
        tc_log(TC_LOG_ERROR,
               "tgfx2_shader_artifact_path: unsupported backend/stage for shader '%s'",
               shader_uuid);
        return false;
    }

    out = std::string(root) + "/shaders/" + backend_dir + "/"
        + shader_uuid + "." + stage_suffix + "." + artifact_ext;
    return true;
}

bool tgfx2_load_shader_artifact_for_backend(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    std::string path;
    if (!tgfx2_shader_artifact_path(shader_uuid, backend, stage, path)) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        tc_log(TC_LOG_ERROR, "tgfx2_load_shader_artifact: missing shader artifact '%s'", path.c_str());
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.empty()) {
        tc_log(TC_LOG_ERROR, "tgfx2_load_shader_artifact: empty shader artifact '%s'", path.c_str());
        return false;
    }
    return true;
}

bool tgfx2_load_or_compile_shader_artifact_for_backend(
    const ::tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tgfx2_load_or_compile_shader_artifact_for_backend: shader is NULL");
        return false;
    }

    std::string path_text;
    if (!tgfx2_shader_artifact_path(shader->uuid, backend, stage, path_text)) {
        return false;
    }
    const std::filesystem::path artifact_path(path_text);

    const bool dev_compile = tgfx2_get_shader_dev_compile_enabled();
    if (!dev_compile) {
        return tgfx2_load_shader_artifact_for_backend(shader->uuid, backend, stage, out);
    }

    const tc_shader_language language = (tc_shader_language)shader->language;
    const bool supported = shader_language_target_supported(language, backend);
    if (read_binary_file(artifact_path, out)) {
        if (artifact_metadata_current(artifact_path, shader, backend, stage)) {
            return true;
        }
        if (!supported) {
            return true;
        }
    } else if (!supported) {
        if (tc_shader_requires_artifacts(shader)) {
            tc_log(TC_LOG_ERROR,
                   "tgfx2 shader dev compile: missing artifact and unsupported language/target for shader '%s': %s -> %s",
                   shader->name ? shader->name : shader->uuid,
                   shader_language_name(language),
                   backend_name(backend));
        }
        return false;
    }

    out.clear();
    if (!compile_shader_artifact(shader, backend, stage, artifact_path)) {
        return false;
    }
    return read_binary_file(artifact_path, out);
}

bool tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
    const tgfx::EngineShaderStageSource& shader,
    tgfx::BackendType backend,
    std::vector<uint8_t>& out
) {
    std::string path_text;
    if (!tgfx2_shader_artifact_path(shader.uuid, backend, shader.stage, path_text)) {
        return false;
    }
    const std::filesystem::path artifact_path(path_text);

    tc_shader_language language = TC_SHADER_LANGUAGE_GLSL;
    if (!shader_language_from_name(shader.language, language)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: unsupported source language '%s' for shader '%s'",
               shader.language ? shader.language : "<null>",
               shader.name ? shader.name : shader.uuid);
        return false;
    }

    const bool dev_compile = tgfx2_get_shader_dev_compile_enabled();
    if (!dev_compile) {
        return tgfx2_load_shader_artifact_for_backend(shader.uuid, backend, shader.stage, out);
    }

    const std::string source_filename = builtin_source_filename(shader.source_resource_path);
    const std::string source = tgfx::load_builtin_shader_source(
        source_filename.c_str(),
        shader.name ? shader.name : shader.uuid);
    if (source.empty()) {
        return false;
    }
    const std::string source_hash = fnv1a_hash_text(source.c_str());
    const bool supported = shader_language_target_supported(language, backend);

    if (read_binary_file(artifact_path, out)) {
        if (engine_shader_artifact_metadata_current(artifact_path, shader, source_hash, backend)) {
            return true;
        }
        if (!supported) {
            return true;
        }
    } else if (!supported) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: missing artifact and unsupported language/target for shader '%s': %s -> %s",
               shader.name ? shader.name : shader.uuid,
               shader_language_name(language),
               backend_name(backend));
        return false;
    }

    out.clear();
    if (!compile_engine_shader_stage_artifact(shader, language, backend, source, source_hash, artifact_path)) {
        return false;
    }
    return read_binary_file(artifact_path, out);
}

bool tgfx2_load_shader_artifact(
    const char* shader_uuid,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    return tgfx2_load_shader_artifact_for_backend(
        shader_uuid,
        tgfx::BackendType::Vulkan,
        stage,
        out);
}

bool tc_shader_ensure_tgfx2(
    ::tc_shader* shader,
    tgfx::IRenderDevice* device,
    tgfx::ShaderHandle* out_vs,
    tgfx::ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: shader is NULL");
        return false;
    }
    if (!device) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: device is NULL");
        return false;
    }
    if (!out_fs) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: out_fs is NULL");
        return false;
    }
    return device->ensure_tc_shader(shader, out_vs, out_fs);
}

} // namespace termin
