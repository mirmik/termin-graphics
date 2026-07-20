// tc_shader_bridge.cpp - TcShader ↔ tgfx2 interop.
#include "tgfx2/tc_shader_bridge.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/internal/shader_logging.hpp"
#include "tgfx2/internal/process_runner.hpp"
#include "tgfx2/shader_artifact_resolver.hpp"
#include <tcbase/trent/json.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include <tcbase/tc_log.h>
}

namespace termin {

ShaderArtifactResolver::ShaderArtifactResolver(
    std::string artifact_root,
    std::string cache_root,
    std::string compiler_path,
    bool dev_compile_enabled,
    bool environment_fallback
) : artifact_root_(std::move(artifact_root)),
    cache_root_(std::move(cache_root)),
    compiler_path_(std::move(compiler_path)),
    dev_compile_enabled_(dev_compile_enabled),
    environment_fallback_(environment_fallback) {
}

const std::string& ShaderArtifactResolver::artifact_root() const {
    if (!artifact_root_.empty() || !environment_fallback_) return artifact_root_;
    const char* value = std::getenv("TERMIN_SHADER_ARTIFACT_ROOT");
    environment_artifact_root_ = value ? value : "";
    return environment_artifact_root_;
}

const std::string& ShaderArtifactResolver::cache_root() const {
    if (!cache_root_.empty() || !environment_fallback_) return cache_root_;
    const char* value = std::getenv("TERMIN_SHADER_CACHE_ROOT");
    environment_cache_root_ = value ? value : "";
    return environment_cache_root_;
}

const std::string& ShaderArtifactResolver::compiler_path() const {
    if (!compiler_path_.empty() || !environment_fallback_) return compiler_path_;
    const char* value = std::getenv("TERMIN_SHADERC");
    environment_compiler_path_ = value ? value : "";
    return environment_compiler_path_;
}

bool ShaderArtifactResolver::dev_compile_enabled() const {
    if (dev_compile_enabled_) return true;
    if (!environment_fallback_) return false;
    const char* value = std::getenv("TERMIN_SHADER_DEV_COMPILE");
    return value && value[0] == '1';
}

void ShaderArtifactResolver::configure(
    std::string artifact_root,
    std::string cache_root,
    std::string compiler_path,
    bool dev_compile_enabled
) {
    artifact_root_ = std::move(artifact_root);
    cache_root_ = std::move(cache_root);
    compiler_path_ = std::move(compiler_path);
    dev_compile_enabled_ = dev_compile_enabled;
    ++revision_;
}

void ShaderArtifactResolver::set_artifact_root(std::string value) {
    artifact_root_ = std::move(value);
    ++revision_;
}

void ShaderArtifactResolver::set_cache_root(std::string value) {
    cache_root_ = std::move(value);
    ++revision_;
}

void ShaderArtifactResolver::set_compiler_path(std::string value) {
    compiler_path_ = std::move(value);
    ++revision_;
}

void ShaderArtifactResolver::set_dev_compile_enabled(bool value) {
    dev_compile_enabled_ = value;
    ++revision_;
}

ShaderArtifactResolver& tgfx2_legacy_shader_artifact_resolver() {
    static ShaderArtifactResolver resolver("", "", "", false, true);
    return resolver;
}

// Bump when termin_shaderc reflected resource placement or sidecar semantics
// change in a way that requires recompiling cached shader artifacts.
static constexpr uint32_t kShaderArtifactLayoutSchemaVersion = 5;
static constexpr uint32_t kShaderArtifactMetadataSchemaVersion = 2;
static constexpr const char* kShaderArtifactMetadataSuffix = ".artifact";
static constexpr const char* kLegacyShaderArtifactMetadataSuffix = ".meta";

void tgfx2_set_shader_artifact_root(const char* root) {
    tgfx2_legacy_shader_artifact_resolver().set_artifact_root(root ? root : "");
}

const char* tgfx2_get_shader_artifact_root(void) {
    return tgfx2_legacy_shader_artifact_resolver().artifact_root().c_str();
}

void tgfx2_set_shader_cache_root(const char* root) {
    tgfx2_legacy_shader_artifact_resolver().set_cache_root(root ? root : "");
}

const char* tgfx2_get_shader_cache_root(void) {
    return tgfx2_legacy_shader_artifact_resolver().cache_root().c_str();
}

void tgfx2_set_shader_compiler_path(const char* path) {
    tgfx2_legacy_shader_artifact_resolver().set_compiler_path(path ? path : "");
}

const char* tgfx2_get_shader_compiler_path(void) {
    return tgfx2_legacy_shader_artifact_resolver().compiler_path().c_str();
}

void tgfx2_set_shader_dev_compile_enabled(bool enabled) {
    tgfx2_legacy_shader_artifact_resolver().set_dev_compile_enabled(enabled);
}

bool tgfx2_get_shader_dev_compile_enabled(void) {
    return tgfx2_legacy_shader_artifact_resolver().dev_compile_enabled();
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
               backend == tgfx::BackendType::OpenGL ||
               backend == tgfx::BackendType::D3D11;
    }
    if (language == TC_SHADER_LANGUAGE_HLSL) {
        return backend == tgfx::BackendType::D3D11;
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

static void fnv1a_update(uint64_t& hash, std::string_view text) {
    for (const unsigned char value : text) {
        hash ^= static_cast<uint64_t>(value);
        hash *= 1099511628211ULL;
    }
}

static bool collect_slang_dependency_hash(
    std::string_view source,
    const std::vector<std::filesystem::path>& include_roots,
    std::unordered_set<std::string>& visited,
    uint64_t& hash,
    std::string& error
) {
    static const std::regex import_re(
        R"(\bimport\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*;)");
    const std::string source_text(source);
    for (std::sregex_iterator it(source_text.begin(), source_text.end(), import_re), end;
         it != end;
         ++it) {
        const std::string module_name = (*it)[1].str();
        std::string module_path = module_name;
        std::replace(module_path.begin(), module_path.end(), '.', '/');

        std::filesystem::path resolved;
        for (const auto& root : include_roots) {
            for (const std::string& candidate_name :
                 {module_name + ".slang", module_path + ".slang"}) {
                std::error_code ec;
                const std::filesystem::path candidate = root / candidate_name;
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    resolved = candidate;
                    break;
                }
            }
            if (!resolved.empty()) break;
        }
        if (resolved.empty()) {
            error = "unresolved Slang import '" + module_name + "'";
            return false;
        }

        std::error_code ec;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(resolved, ec);
        const std::string key = (ec ? resolved.lexically_normal() : canonical).string();
        if (!visited.insert(key).second) continue;

        std::ifstream in(resolved, std::ios::binary);
        if (!in) {
            error = "failed to read Slang import '" + module_name + "' at '" +
                resolved.string() + "'";
            return false;
        }
        const std::string dependency{
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
        fnv1a_update(hash, module_name);
        fnv1a_update(hash, "\0");
        fnv1a_update(hash, dependency);
        fnv1a_update(hash, "\0");
        if (!collect_slang_dependency_hash(
                dependency, include_roots, visited, hash, error)) {
            return false;
        }
    }
    return true;
}

static bool shader_dependency_fingerprint(
    tc_shader_language language,
    std::string_view source,
    std::string& out
) {
    if (language != TC_SHADER_LANGUAGE_SLANG) {
        out = "none";
        return true;
    }
    const auto roots = tgfx::builtin_shader_roots();
    std::unordered_set<std::string> visited;
    uint64_t hash = 1469598103934665603ULL;
    std::string error;
    if (!collect_slang_dependency_hash(source, roots, visited, hash, error)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dependency scan: %s; include roots=%zu",
               error.c_str(),
               roots.size());
        return false;
    }
    char encoded[17];
    std::snprintf(encoded, sizeof(encoded), "%016llx",
                  static_cast<unsigned long long>(hash));
    out = encoded;
    return true;
}

static bool shader_program_dependency_fingerprint(
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::string& out
) {
    const tc_shader_language language = (tc_shader_language)shader->language;
    if (backend != tgfx::BackendType::D3D11 ||
        language != TC_SHADER_LANGUAGE_SLANG) {
        const char* source = shader_stage_source(shader, stage);
        return shader_dependency_fingerprint(
            language,
            source ? std::string_view(source) : std::string_view(),
            out);
    }

    std::string program_dependencies;
    for (const tgfx::ShaderStage program_stage : {
             tgfx::ShaderStage::Vertex,
             tgfx::ShaderStage::Fragment,
             tgfx::ShaderStage::Geometry}) {
        const char* source = shader_stage_source(shader, program_stage);
        if (!source || source[0] == '\0') {
            continue;
        }
        std::string stage_fingerprint;
        if (!shader_dependency_fingerprint(
                language,
                source,
                stage_fingerprint)) {
            return false;
        }
        program_dependencies += stage_name(program_stage);
        program_dependencies += '=';
        program_dependencies += stage_fingerprint;
        program_dependencies += ';';
    }
    out = fnv1a_hash_text(program_dependencies.c_str());
    return true;
}

static std::filesystem::path shader_cache_source_path(
    const ShaderArtifactResolver& resolver,
    const tc_shader* shader,
    tgfx::ShaderStage stage
) {
    const std::string& cache_root = resolver.cache_root();
    if (!cache_root.empty()) {
        return std::filesystem::path(cache_root) / "source" /
            (std::string(shader->uuid) + "." + stage_extension(stage) + "." +
             shader_source_extension((tc_shader_language)shader->language));
    }

    const std::string& artifact_root = resolver.artifact_root();
    return std::filesystem::path(artifact_root) / ".build" / "shaders" / "source" /
        (std::string(shader->uuid) + "." + stage_extension(stage) + "." +
         shader_source_extension((tc_shader_language)shader->language));
}

static std::string artifact_metadata_text(
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    const char* entry = "main";
    if (stage == tgfx::ShaderStage::Vertex && shader->vertex_entry && shader->vertex_entry[0]) {
        entry = shader->vertex_entry;
    } else if (stage == tgfx::ShaderStage::Fragment && shader->fragment_entry && shader->fragment_entry[0]) {
        entry = shader->fragment_entry;
    } else if (stage == tgfx::ShaderStage::Geometry && shader->geometry_entry && shader->geometry_entry[0]) {
        entry = shader->geometry_entry;
    }

    std::ostringstream out;
    out << "artifact_metadata_schema=" << kShaderArtifactMetadataSchemaVersion << "\n";
    out << "layout_schema=" << kShaderArtifactLayoutSchemaVersion << "\n";
    out << "shader_compiler=" << compiler_fingerprint << "\n";
    out << "dependency_hash=" << dependency_fingerprint << "\n";
    out << "source_hash=" << shader->source_hash << "\n";
    out << "language=" << shader_language_name((tc_shader_language)shader->language) << "\n";
    out << "target=" << backend_name(backend) << "\n";
    out << "stage=" << stage_name(stage) << "\n";
    out << "entry=" << entry << "\n";
    out << "shader_uuid=" << shader->uuid << "\n";
    out << "shader_version=" << shader->version << "\n";
    return out.str();
}

static std::string engine_shader_artifact_metadata_text(
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    std::ostringstream out;
    out << "artifact_metadata_schema=" << kShaderArtifactMetadataSchemaVersion << "\n";
    out << "layout_schema=" << kShaderArtifactLayoutSchemaVersion << "\n";
    out << "shader_compiler=" << compiler_fingerprint << "\n";
    out << "dependency_hash=" << dependency_fingerprint << "\n";
    out << "source_hash=" << source_hash << "\n";
    out << "language=" << (shader.language ? shader.language : "") << "\n";
    out << "target=" << backend_name(backend) << "\n";
    out << "stage=" << stage_name(shader.stage) << "\n";
    out << "entry=" << (shader.entry_point ? shader.entry_point : "main") << "\n";
    out << "shader_uuid=" << (shader.uuid ? shader.uuid : "") << "\n";
    out << "shader_name=" << (shader.name ? shader.name : "") << "\n";
    return out.str();
}

static std::filesystem::path shader_artifact_metadata_path(
    const std::filesystem::path& artifact_path
) {
    return std::filesystem::path(artifact_path.string() + kShaderArtifactMetadataSuffix);
}

static std::filesystem::path legacy_shader_artifact_metadata_path(
    const std::filesystem::path& artifact_path
) {
    return std::filesystem::path(artifact_path.string() + kLegacyShaderArtifactMetadataSuffix);
}

static std::string shader_compiler_fingerprint(
    const std::filesystem::path& compiler_path
) {
    std::error_code ec;
    const std::filesystem::path canonical_path =
        std::filesystem::weakly_canonical(compiler_path, ec);
    const std::filesystem::path stable_path = ec ? compiler_path : canonical_path;

    ec.clear();
    const auto mtime = std::filesystem::last_write_time(stable_path, ec);
    const auto mtime_ticks = ec ? 0 : mtime.time_since_epoch().count();

    ec.clear();
    const uintmax_t size = std::filesystem::file_size(stable_path, ec);

    std::ostringstream out;
    out << stable_path.string()
        << "|mtime=" << mtime_ticks
        << "|size=" << (ec ? 0 : size);
    return out.str();
}

static bool artifact_metadata_current(
    const std::filesystem::path& artifact_path,
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    std::ifstream in(shader_artifact_metadata_path(artifact_path), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    return text == artifact_metadata_text(
        shader, backend, stage, compiler_fingerprint, dependency_fingerprint);
}

static bool engine_shader_artifact_metadata_current(
    const std::filesystem::path& artifact_path,
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    std::ifstream in(shader_artifact_metadata_path(artifact_path), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    return text == engine_shader_artifact_metadata_text(
        shader,
        source_hash,
        backend,
        compiler_fingerprint,
        dependency_fingerprint);
}

static bool write_artifact_metadata(
    const std::filesystem::path& artifact_path,
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    std::ofstream out(shader_artifact_metadata_path(artifact_path), std::ios::binary);
    if (!out) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: failed to open metadata for '%s'",
               artifact_path.string().c_str());
        return false;
    }
    out << artifact_metadata_text(
        shader, backend, stage, compiler_fingerprint, dependency_fingerprint);
    std::error_code ec;
    std::filesystem::remove(legacy_shader_artifact_metadata_path(artifact_path), ec);
    return static_cast<bool>(out);
}

static bool write_engine_shader_artifact_metadata(
    const std::filesystem::path& artifact_path,
    const tgfx::EngineShaderStageSource& shader,
    const std::string& source_hash,
    tgfx::BackendType backend,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
) {
    std::ofstream out(shader_artifact_metadata_path(artifact_path), std::ios::binary);
    if (!out) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: failed to open metadata for '%s'",
               artifact_path.string().c_str());
        return false;
    }
    out << engine_shader_artifact_metadata_text(
        shader,
        source_hash,
        backend,
        compiler_fingerprint,
        dependency_fingerprint);
    std::error_code ec;
    std::filesystem::remove(legacy_shader_artifact_metadata_path(artifact_path), ec);
    return static_cast<bool>(out);
}

static std::filesystem::path shader_resource_layout_sidecar_path(
    const std::filesystem::path& artifact_path
) {
    return std::filesystem::path(artifact_path.string() + ".layout.json");
}

static bool read_text_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

static uint32_t shader_resource_kind_from_name(const std::string& name) {
    if (name == "constant_buffer") return TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    if (name == "texture") return TC_SHADER_RESOURCE_TEXTURE;
    if (name == "sampler") return TC_SHADER_RESOURCE_SAMPLER;
    if (name == "storage_buffer") return TC_SHADER_RESOURCE_STORAGE_BUFFER;
    if (name == "storage_texture") return TC_SHADER_RESOURCE_STORAGE_TEXTURE;
    return TC_SHADER_RESOURCE_NONE;
}

static uint32_t shader_resource_scope_from_name(const std::string& name) {
    if (name == "frame") return TC_SHADER_RESOURCE_SCOPE_FRAME;
    if (name == "pass") return TC_SHADER_RESOURCE_SCOPE_PASS;
    if (name == "material") return TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    if (name == "draw") return TC_SHADER_RESOURCE_SCOPE_DRAW;
    if (name == "transient") return TC_SHADER_RESOURCE_SCOPE_TRANSIENT;
    if (name == "unscoped") return TC_SHADER_RESOURCE_SCOPE_UNSCOPED;
    if (name == "unknown") return TC_SHADER_RESOURCE_SCOPE_UNKNOWN;
    return TC_SHADER_RESOURCE_SCOPE_UNKNOWN;
}

static uint32_t shader_d3d11_register_class_from_name(const std::string& name) {
    if (name == "b") return TC_SHADER_D3D11_REGISTER_B;
    if (name == "t") return TC_SHADER_D3D11_REGISTER_T;
    if (name == "s") return TC_SHADER_D3D11_REGISTER_S;
    if (name == "u") return TC_SHADER_D3D11_REGISTER_U;
    return TC_SHADER_D3D11_REGISTER_NONE;
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

static bool parse_shader_resource_layout_sidecar(
    const std::string& text,
    std::vector<tc_shader_resource_binding>& out_bindings
) {
    nos::trent root;
    try {
        root = nos::json::parse(text);
    } catch (const std::exception&) {
        return false;
    }
    const nos::trent* resources = trent_dict_get(root, "resources");
    if (!resources || !resources->is_list()) {
        return false;
    }
    std::string target;
    const bool has_target = trent_string_field(root, "target", target);
    const bool require_d3d11_placement = has_target && target == "d3d11";

    for (const nos::trent& object : resources->as_list()) {
        if (!object.is_dict()) {
            return false;
        }
        std::string name;
        std::string kind_name;
        std::string scope_name;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t stage_mask = TC_SHADER_STAGE_NONE;
        uint32_t size = 0;
        if (!trent_string_field(object, "name", name) ||
            !trent_string_field(object, "kind", kind_name) ||
            !trent_uint_field(object, "set", set) ||
            !trent_uint_field(object, "binding", binding) ||
            !trent_uint_field(object, "stage_mask", stage_mask)) {
            return false;
        }
        trent_uint_field(object, "size", size);

        const uint32_t kind = shader_resource_kind_from_name(kind_name);
        if (kind == TC_SHADER_RESOURCE_NONE || name.empty()) {
            return false;
        }
        if (stage_mask == TC_SHADER_STAGE_NONE) {
            return false;
        }
        uint32_t scope = TC_SHADER_RESOURCE_SCOPE_UNSCOPED;
        if (trent_string_field(object, "scope", scope_name)) {
            scope = shader_resource_scope_from_name(scope_name);
        }

        tc_shader_resource_binding resource{};
        std::snprintf(resource.name, sizeof(resource.name), "%s", name.c_str());
        resource.kind = kind;
        resource.scope = scope;
        resource.set = set;
        resource.binding = binding;
        resource.stage_mask = stage_mask;
        resource.size = size;

        const nos::trent* d3d11 = trent_dict_get(object, "d3d11");
        if (d3d11) {
            std::string register_class_name;
            uint32_t register_index = 0;
            if (!d3d11->is_dict() ||
                !trent_string_field(*d3d11, "register_class", register_class_name) ||
                !trent_uint_field(*d3d11, "register_index", register_index)) {
                return false;
            }
            const uint32_t register_class =
                shader_d3d11_register_class_from_name(register_class_name);
            if (register_class == TC_SHADER_D3D11_REGISTER_NONE) {
                return false;
            }
            resource.has_d3d11_placement = 1;
            resource.d3d11.register_class = register_class;
            resource.d3d11.register_index = register_index;
            bool scalar_sampler_for_texture_array = false;
            if (trent_bool_field(
                    *d3d11,
                    "scalar_sampler_for_texture_array",
                    scalar_sampler_for_texture_array)) {
                resource.d3d11_scalar_sampler_for_texture_array =
                    scalar_sampler_for_texture_array ? 1 : 0;
            }
        } else if (require_d3d11_placement) {
            return false;
        }

        // Parse per-field layout for constant buffers.
        const nos::trent* fields_list = trent_dict_get(object, "fields");
        if (fields_list && fields_list->is_list()) {
            const auto& fl = fields_list->as_list();
            resource.field_count = static_cast<uint32_t>(fl.size());
            resource.fields = static_cast<tc_shader_resource_field*>(
                std::malloc(resource.field_count * sizeof(tc_shader_resource_field)));
            if (resource.fields) {
                size_t fi = 0;
                for (const nos::trent& field_obj : fl) {
                    if (!field_obj.is_dict()) continue;
                    tc_shader_resource_field& f = resource.fields[fi];
                    std::string fname;
                    std::string ftype;
                    trent_string_field(field_obj, "name", fname);
                    trent_string_field(field_obj, "type", ftype);
                    std::snprintf(f.name, sizeof(f.name), "%s", fname.c_str());
                    std::snprintf(f.type, sizeof(f.type), "%s", ftype.c_str());
                    trent_uint_field(field_obj, "offset", f.offset);
                    trent_uint_field(field_obj, "size", f.size);
                    ++fi;
                }
                resource.field_count = static_cast<uint32_t>(fi);
            }
        }

        out_bindings.push_back(resource);
    }
    return true;
}

static void merge_shader_resource_binding(
    std::vector<tc_shader_resource_binding>& bindings,
    const tc_shader_resource_binding& incoming
) {
    auto clone_binding = [](const tc_shader_resource_binding& src) {
        tc_shader_resource_binding dst = src;
        dst.fields = nullptr;
        if (src.field_count > 0 && src.fields) {
            dst.fields = static_cast<tc_shader_resource_field*>(
                std::malloc(src.field_count * sizeof(tc_shader_resource_field)));
            if (dst.fields) {
                std::memcpy(
                    dst.fields,
                    src.fields,
                    src.field_count * sizeof(tc_shader_resource_field));
            } else {
                dst.field_count = 0;
            }
        }
        return dst;
    };

    for (tc_shader_resource_binding& existing : bindings) {
        if (std::strcmp(existing.name, incoming.name) == 0) {
            // Detect binding/set conflicts between shader stages.
            // When vertex and fragment stages declare the same resource with
            // different binding numbers, the merged layout becomes ambiguous.
            if (existing.binding != incoming.binding || existing.set != incoming.set) {
                tc_log(TC_LOG_WARN,
                       "merge_shader_resource_binding: '%s' has conflicting "
                       "placements — existing set=%u binding=%u vs incoming "
                       "set=%u binding=%u (stages 0x%x | 0x%x); using incoming",
                       incoming.name,
                       existing.set, existing.binding,
                       incoming.set, incoming.binding,
                       existing.stage_mask, incoming.stage_mask);
            }
            if (existing.kind != incoming.kind) {
                tc_log(TC_LOG_WARN,
                       "merge_shader_resource_binding: '%s' kind mismatch — "
                       "existing=%u vs incoming=%u",
                       incoming.name, existing.kind, incoming.kind);
            }
            const uint32_t previous_size = existing.size;
            const uint32_t previous_stage_mask = existing.stage_mask;
            const uint32_t previous_scope = existing.scope;
            const uint32_t previous_field_count = existing.field_count;
            tc_shader_resource_field* previous_fields = existing.fields;
            const uint8_t previous_has_d3d11_placement =
                existing.has_d3d11_placement;
            const uint8_t previous_d3d11_scalar_sampler_for_texture_array =
                existing.d3d11_scalar_sampler_for_texture_array;
            const tc_shader_d3d11_placement previous_d3d11 = existing.d3d11;
            if (previous_has_d3d11_placement && incoming.has_d3d11_placement &&
                (previous_d3d11.register_class != incoming.d3d11.register_class ||
                 previous_d3d11.register_index != incoming.d3d11.register_index)) {
                tc_log(TC_LOG_WARN,
                       "merge_shader_resource_binding: '%s' has conflicting "
                       "D3D11 placements — existing class=%u register=%u vs "
                       "incoming class=%u register=%u (stages 0x%x | 0x%x); keeping incoming",
                       incoming.name,
                       previous_d3d11.register_class,
                       previous_d3d11.register_index,
                       incoming.d3d11.register_class,
                       incoming.d3d11.register_index,
                       existing.stage_mask,
                       incoming.stage_mask);
            }
            existing.fields = nullptr;
            existing.field_count = 0;
            existing = clone_binding(incoming);
            existing.name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
            existing.stage_mask |= previous_stage_mask;
            if (!incoming.has_d3d11_placement && previous_has_d3d11_placement) {
                existing.has_d3d11_placement = 1;
                existing.d3d11 = previous_d3d11;
            }
            if (previous_d3d11_scalar_sampler_for_texture_array) {
                existing.d3d11_scalar_sampler_for_texture_array = 1;
            }
            if ((incoming.scope == TC_SHADER_RESOURCE_SCOPE_UNKNOWN ||
                 incoming.scope == TC_SHADER_RESOURCE_SCOPE_UNSCOPED) &&
                previous_scope != TC_SHADER_RESOURCE_SCOPE_UNKNOWN &&
                previous_scope != TC_SHADER_RESOURCE_SCOPE_UNSCOPED) {
                existing.scope = previous_scope;
            }
            if (incoming.size == 0 && previous_size != 0) {
                existing.size = previous_size;
            }
            if (incoming.field_count == 0 && previous_field_count != 0 &&
                previous_fields) {
                existing.fields = previous_fields;
                existing.field_count = previous_field_count;
                previous_fields = nullptr;
            }
            if (previous_fields) {
                std::free(previous_fields);
            }
            return;
        }
    }
    bindings.push_back(clone_binding(incoming));
    bindings.back().name[TC_SHADER_RESOURCE_NAME_MAX - 1] = '\0';
}

static void free_shader_resource_binding_fields(
    std::vector<tc_shader_resource_binding>& bindings
) {
    for (tc_shader_resource_binding& binding : bindings) {
        if (binding.fields) {
            std::free(binding.fields);
            binding.fields = nullptr;
        }
        binding.field_count = 0;
    }
}

static bool attach_shader_contract_from_resource_layout(
    tc_shader* shader,
    const char* source_debug_name)
{
    if (!shader || tc_shader_has_contract(shader)) {
        return true;
    }
    if (!tc_shader_has_resource_layout(shader)) {
        return true;
    }

    const uint32_t binding_count = tc_shader_resource_binding_count(shader);
    const tc_shader_resource_binding* bindings = tc_shader_resource_bindings(shader);

    std::vector<tc_shader_resource_requirement> requirements;
    requirements.reserve(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i) {
        const tc_shader_resource_binding& binding = bindings[i];
        if (binding.name[0] == '\0' ||
            binding.kind == TC_SHADER_RESOURCE_NONE ||
            binding.stage_mask == TC_SHADER_STAGE_NONE) {
            continue;
        }

        tc_shader_resource_requirement requirement{};
        std::snprintf(
            requirement.name,
            sizeof(requirement.name),
            "%s",
            binding.name);
        requirement.kind = binding.kind;
        requirement.scope = binding.scope;
        requirement.stage_mask = binding.stage_mask;
        requirement.size = binding.size;
        requirement.fields = binding.fields;
        requirement.field_count = binding.field_count;
        requirements.push_back(requirement);
    }

    tc_shader_contract_desc desc{};
    desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_REFLECTION;
    desc.resources = requirements.empty() ? nullptr : requirements.data();
    desc.resource_count = static_cast<uint32_t>(requirements.size());
    desc.debug_name = shader->name ? shader->name : shader->uuid;
    desc.source_debug_name = source_debug_name;

    if (!tc_shader_set_contract(shader, &desc)) {
        tc_log(
            TC_LOG_ERROR,
            "tgfx2 shader contract: failed to attach reflected contract for '%s'",
            shader->uuid);
        return false;
    }
    return true;
}

static bool apply_shader_resource_layout_sidecar(
    tc_shader* shader,
    const std::filesystem::path& artifact_path
) {
    if (!shader) {
        return true;
    }

    const std::filesystem::path sidecar = shader_resource_layout_sidecar_path(artifact_path);
    std::error_code ec;
    if (!std::filesystem::exists(sidecar, ec) ||
        std::filesystem::is_directory(sidecar, ec)) {
        return true;
    }

    std::string text;
    if (!read_text_file(sidecar, text)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader resource layout: failed to read '%s'",
               sidecar.string().c_str());
        return false;
    }

    std::vector<tc_shader_resource_binding> incoming;
    if (!parse_shader_resource_layout_sidecar(text, incoming)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader resource layout: malformed sidecar '%s'",
               sidecar.string().c_str());
        return false;
    }
    if (incoming.empty()) {
        tc_shader_mark_resource_layout_known(shader);
        return attach_shader_contract_from_resource_layout(
            shader,
            "shader resource layout sidecar");
    }

    std::vector<tc_shader_resource_binding> merged;
    const uint32_t existing_count = tc_shader_resource_binding_count(shader);
    const tc_shader_resource_binding* existing = tc_shader_resource_bindings(shader);
    if (existing && existing_count > 0) {
        for (uint32_t i = 0; i < existing_count; ++i) {
            merge_shader_resource_binding(merged, existing[i]);
        }
    }
    for (const tc_shader_resource_binding& binding : incoming) {
        merge_shader_resource_binding(merged, binding);
    }
    tc_shader_set_resource_layout(
        shader,
        merged.data(),
        static_cast<uint32_t>(merged.size()));
    const bool contract_attached = attach_shader_contract_from_resource_layout(
        shader,
        "shader resource layout sidecar");
    free_shader_resource_binding_fields(merged);
    free_shader_resource_binding_fields(incoming);
    if (tgfx::internal::shader_verbose_logging_enabled()) {
        tc_log(TC_LOG_DEBUG,
               "tgfx2 shader resource layout: loaded %zu resource(s) from '%s'",
               incoming.size(),
               sidecar.string().c_str());
    }
    return contract_attached;
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

static std::optional<std::filesystem::path> resolve_shader_compiler(
    const ShaderArtifactResolver& resolver
) {
    const std::string& configured = resolver.compiler_path();
    if (!configured.empty()) {
        std::filesystem::path path(configured);
        if (is_existing_file(path)) {
            return path;
        }
        tc_log(TC_LOG_ERROR,
               "tgfx2 shader dev compile: configured termin_shaderc does not exist: %s",
               configured.c_str());
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

static int run_shader_tool(const std::vector<std::string>& args, const char* log_prefix) {
    tgfx::internal::ProcessResult result = tgfx::internal::run_process(args);
    if (!result.start_error.empty()) {
        tc_log(TC_LOG_ERROR,
               "%s: failed to run '%s': %s",
               log_prefix,
               args.empty() ? "<empty>" : args[0].c_str(),
               result.start_error.c_str());
    }
    return result.exit_code;
}

static bool compile_shader_artifact(
    const ShaderArtifactResolver& resolver,
    const tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    const std::filesystem::path& artifact_path,
    const std::filesystem::path& compiler,
    const std::string& compiler_fingerprint,
    const std::string& dependency_fingerprint
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

    const std::filesystem::path source_path = shader_cache_source_path(resolver, shader, stage);
    if (!write_text_file(source_path, source)) {
        return false;
    }

    std::vector<std::filesystem::path> program_source_paths;
    if (backend == tgfx::BackendType::D3D11 &&
        language == TC_SHADER_LANGUAGE_SLANG) {
        for (const tgfx::ShaderStage program_stage : {
                 tgfx::ShaderStage::Vertex,
                 tgfx::ShaderStage::Fragment,
                 tgfx::ShaderStage::Geometry}) {
            const char* program_source = shader_stage_source(shader, program_stage);
            if (!program_source || program_source[0] == '\0') {
                continue;
            }
            const std::filesystem::path program_source_path =
                shader_cache_source_path(resolver, shader, program_stage);
            if (program_source_path != source_path &&
                !write_text_file(program_source_path, program_source)) {
                return false;
            }
            program_source_paths.push_back(program_source_path);
        }
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

    const char* entry = "main";
    if (stage == tgfx::ShaderStage::Vertex && shader->vertex_entry && shader->vertex_entry[0])
        entry = shader->vertex_entry;
    else if (stage == tgfx::ShaderStage::Fragment && shader->fragment_entry && shader->fragment_entry[0])
        entry = shader->fragment_entry;

    std::vector<std::string> args = {
        compiler.string(),
        "compile",
        "--language", shader_language_name(language),
        "--target", backend_name(backend),
        "--stage", stage_name(stage),
        "--entry", entry,
        "--input", source_path.string(),
        "--output", artifact_path.string(),
        "--debug-name",
        std::string(shader->name ? shader->name : shader->uuid) + ":" + stage_name(stage),
    };
    if (language == TC_SHADER_LANGUAGE_SLANG) {
        for (const auto& root : tgfx::builtin_shader_roots()) {
            args.insert(args.end(), {"-I", root.string()});
        }
    }
    for (const std::filesystem::path& program_source_path : program_source_paths) {
        args.insert(args.end(), {"--program-source", program_source_path.string()});
    }
    if (tgfx::internal::shader_verbose_logging_enabled()) {
        tc_log(TC_LOG_DEBUG,
               "tgfx2 shader dev compile: %s stage=%s entry=%s input='%s' output='%s'",
               shader->name ? shader->name : shader->uuid,
               stage_name(stage),
               entry,
               source_path.string().c_str(),
               artifact_path.string().c_str());
    }

    const int rc = run_shader_tool(args, "tgfx2 shader dev compile");
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
    return write_artifact_metadata(
        artifact_path,
        shader,
        backend,
        stage,
        compiler_fingerprint,
        dependency_fingerprint);
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
    const ShaderArtifactResolver& resolver,
    const tgfx::EngineShaderStageSource& shader,
    tc_shader_language language
) {
    const std::string& cache_root = resolver.cache_root();
    if (!cache_root.empty()) {
        return std::filesystem::path(cache_root) / "source" /
            (std::string(shader.uuid) + "." + stage_extension(shader.stage) + "." +
             shader_source_extension(language));
    }

    const std::string& artifact_root = resolver.artifact_root();
    return std::filesystem::path(artifact_root) / ".build" / "shaders" / "source" /
        (std::string(shader.uuid) + "." + stage_extension(shader.stage) + "." +
         shader_source_extension(language));
}

struct EngineShaderStageCompileRequest {
    const tgfx::EngineShaderStageSource& shader;
    tc_shader_language language;
    tgfx::BackendType backend;
    const std::string& source;
    const std::string& source_hash;
    const std::filesystem::path& artifact_path;
    const std::filesystem::path& compiler;
    const std::string& compiler_fingerprint;
    const std::string& dependency_fingerprint;
};

static bool compile_engine_shader_stage_artifact(
    const ShaderArtifactResolver& resolver,
    const EngineShaderStageCompileRequest& request
) {
    if (!shader_language_target_supported(request.language, request.backend)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: unsupported language/target for shader '%s': %s -> %s",
               request.shader.name ? request.shader.name : request.shader.uuid,
               shader_language_name(request.language),
               backend_name(request.backend));
        return false;
    }

    const std::filesystem::path source_path =
        engine_shader_cache_source_path(resolver, request.shader, request.language);
    if (!write_text_file(source_path, request.source)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(request.artifact_path.parent_path(), ec);
    if (ec) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: failed to create artifact directory '%s': %s",
               request.artifact_path.parent_path().string().c_str(),
               ec.message().c_str());
        return false;
    }

    std::vector<std::string> args = {
        request.compiler.string(),
        "compile",
        "--language", shader_language_name(request.language),
        "--target", backend_name(request.backend),
        "--stage", stage_name(request.shader.stage),
        "--entry", request.shader.entry_point && request.shader.entry_point[0]
            ? request.shader.entry_point
            : "main",
        "--input", source_path.string(),
        "--output", request.artifact_path.string(),
        "--debug-name",
        std::string(request.shader.name ? request.shader.name : request.shader.uuid) +
            ":" + stage_name(request.shader.stage),
    };
    if (request.language == TC_SHADER_LANGUAGE_SLANG) {
        for (const auto& root : tgfx::builtin_shader_roots()) {
            args.insert(args.end(), {"-I", root.string()});
        }
    }

    const int rc = run_shader_tool(args, "tgfx2 engine shader dev compile");
    if (rc != 0) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: termin_shaderc failed for '%s' stage=%s target=%s rc=%d",
               request.shader.name ? request.shader.name : request.shader.uuid,
               stage_name(request.shader.stage),
               backend_name(request.backend),
               rc);
        return false;
    }

    if (!is_existing_file(request.artifact_path)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: compiler did not produce '%s'",
               request.artifact_path.string().c_str());
        return false;
    }
    return write_engine_shader_artifact_metadata(
        request.artifact_path,
        request.shader,
        request.source_hash,
        request.backend,
        request.compiler_fingerprint,
        request.dependency_fingerprint);
}

bool tgfx2_shader_artifact_path(
    const ShaderArtifactResolver& resolver,
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::string& out
) {
    const std::string& root = resolver.artifact_root();
    if (!shader_uuid || shader_uuid[0] == '\0') {
        tc_log(TC_LOG_ERROR,
               "tgfx2_shader_artifact_path: missing shader_uuid='%s'",
               shader_uuid ? shader_uuid : "<null>");
        return false;
    }
    if (root.empty()) {
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

    out = root + "/shaders/" + backend_dir + "/"
        + shader_uuid + "." + stage_suffix + "." + artifact_ext;
    return true;
}

bool tgfx2_shader_artifact_path(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::string& out
) {
    return tgfx2_shader_artifact_path(
        tgfx2_legacy_shader_artifact_resolver(),
        shader_uuid,
        backend,
        stage,
        out
    );
}

static bool load_shader_artifact_for_backend(
    const ShaderArtifactResolver& resolver,
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    std::string path;
    if (!tgfx2_shader_artifact_path(resolver, shader_uuid, backend, stage, path)) {
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

bool tgfx2_load_shader_artifact_for_backend(
    const char* shader_uuid,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    return load_shader_artifact_for_backend(
        tgfx2_legacy_shader_artifact_resolver(),
        shader_uuid,
        backend,
        stage,
        out
    );
}

bool tgfx2_load_or_compile_shader_artifact_for_backend(
    const ShaderArtifactResolver& resolver,
    ::tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tgfx2_load_or_compile_shader_artifact_for_backend: shader is NULL");
        return false;
    }

    std::string path_text;
    if (!tgfx2_shader_artifact_path(resolver, shader->uuid, backend, stage, path_text)) {
        return false;
    }
    const std::filesystem::path artifact_path(path_text);

    const bool dev_compile = resolver.dev_compile_enabled();
    if (!dev_compile) {
        if (!load_shader_artifact_for_backend(resolver, shader->uuid, backend, stage, out)) {
            return false;
        }
        return apply_shader_resource_layout_sidecar(shader, artifact_path);
    }

    const tc_shader_language language = (tc_shader_language)shader->language;
    const bool supported = shader_language_target_supported(language, backend);
    std::string dependency_fingerprint;
    if (!shader_program_dependency_fingerprint(
            shader,
            backend,
            stage,
            dependency_fingerprint)) {
        return false;
    }
    std::optional<std::filesystem::path> compiler;
    std::string compiler_fingerprint;
    if (supported) {
        compiler = resolve_shader_compiler(resolver);
        if (compiler) {
            compiler_fingerprint = shader_compiler_fingerprint(*compiler);
        }
    }
    if (read_binary_file(artifact_path, out)) {
        if (compiler &&
            artifact_metadata_current(
                artifact_path,
                shader,
                backend,
                stage,
                compiler_fingerprint,
                dependency_fingerprint)) {
            return apply_shader_resource_layout_sidecar(shader, artifact_path);
        }
        if (!supported) {
            return apply_shader_resource_layout_sidecar(shader, artifact_path);
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

    if (!compiler) {
        return false;
    }
    out.clear();
    if (!compile_shader_artifact(
            resolver,
            shader,
            backend,
            stage,
            artifact_path,
            *compiler,
            compiler_fingerprint,
            dependency_fingerprint)) {
        return false;
    }
    if (!read_binary_file(artifact_path, out)) {
        return false;
    }
    return apply_shader_resource_layout_sidecar(shader, artifact_path);
}

bool tgfx2_load_or_compile_shader_artifact_for_backend(
    ::tc_shader* shader,
    tgfx::BackendType backend,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    return tgfx2_load_or_compile_shader_artifact_for_backend(
        tgfx2_legacy_shader_artifact_resolver(),
        shader,
        backend,
        stage,
        out
    );
}

bool tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
    const ShaderArtifactResolver& resolver,
    const tgfx::EngineShaderStageSource& shader,
    tgfx::BackendType backend,
    std::vector<uint8_t>& out
) {
    std::string path_text;
    if (!tgfx2_shader_artifact_path(resolver, shader.uuid, backend, shader.stage, path_text)) {
        return false;
    }
    const std::filesystem::path artifact_path(path_text);

    tc_shader_language language = TC_SHADER_LANGUAGE_UNSPECIFIED;
    if (!shader_language_from_name(shader.language, language)) {
        tc_log(TC_LOG_ERROR,
               "tgfx2 engine shader dev compile: unsupported source language '%s' for shader '%s'",
               shader.language ? shader.language : "<null>",
               shader.name ? shader.name : shader.uuid);
        return false;
    }

    const bool dev_compile = resolver.dev_compile_enabled();
    if (!dev_compile) {
        return load_shader_artifact_for_backend(resolver, shader.uuid, backend, shader.stage, out);
    }

    const std::string source_filename = builtin_source_filename(shader.source_resource_path);
    const std::string source = tgfx::load_builtin_shader_source(
        source_filename.c_str(),
        shader.name ? shader.name : shader.uuid);
    if (source.empty()) {
        return false;
    }
    const std::string source_hash = fnv1a_hash_text(source.c_str());
    std::string dependency_fingerprint;
    if (!shader_dependency_fingerprint(language, source, dependency_fingerprint)) {
        return false;
    }
    const bool supported = shader_language_target_supported(language, backend);
    std::optional<std::filesystem::path> compiler;
    std::string compiler_fingerprint;
    if (supported) {
        compiler = resolve_shader_compiler(resolver);
        if (compiler) {
            compiler_fingerprint = shader_compiler_fingerprint(*compiler);
        }
    }

    if (read_binary_file(artifact_path, out)) {
        if (compiler &&
            engine_shader_artifact_metadata_current(
                artifact_path,
                shader,
                source_hash,
                backend,
                compiler_fingerprint,
                dependency_fingerprint)) {
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
    if (!compiler) {
        return false;
    }
    const EngineShaderStageCompileRequest compile_request{
        shader,
        language,
        backend,
        source,
        source_hash,
        artifact_path,
        *compiler,
        compiler_fingerprint,
        dependency_fingerprint
    };
    if (!compile_engine_shader_stage_artifact(resolver, compile_request)) {
        return false;
    }
    return read_binary_file(artifact_path, out);
}

bool tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
    const tgfx::EngineShaderStageSource& shader,
    tgfx::BackendType backend,
    std::vector<uint8_t>& out
) {
    return tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
        tgfx2_legacy_shader_artifact_resolver(),
        shader,
        backend,
        out
    );
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
    if (!device->ensure_tc_shader(shader, out_vs, out_fs)) {
        return false;
    }
    if (tc_shader_has_resource_layout(shader) &&
        !tc_shader_sync_reflected_contract_resources(shader)) {
        tc_log(
            TC_LOG_ERROR,
            "tc_shader_ensure_tgfx2: failed to synchronize reflected contract for '%s'",
            shader->name ? shader->name : shader->uuid);
        return false;
    }
    return true;
}

} // namespace termin
extern "C" TGFX2_API void tgfx2_set_shader_artifact_root(const char* root) {
    termin::tgfx2_set_shader_artifact_root(root);
}

extern "C" TGFX2_API const char* tgfx2_get_shader_artifact_root(void) {
    return termin::tgfx2_get_shader_artifact_root();
}
