#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"

#ifdef TGFX2_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#else
extern "C" {
#include <tcbase/tc_log.h>
}
#endif

namespace tgfx::vk {

#ifdef TGFX2_HAS_SHADERC

namespace {

constexpr uint32_t SPIRV_CACHE_MAGIC = 0x54535631u; // "TSV1"
constexpr uint32_t SPIRV_CACHE_VERSION = 2;

static shaderc_shader_kind to_shaderc_kind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return shaderc_vertex_shader;
        case ShaderStage::Fragment: return shaderc_fragment_shader;
        case ShaderStage::Geometry: return shaderc_geometry_shader;
        case ShaderStage::Compute:  return shaderc_compute_shader;
    }
    return shaderc_vertex_shader;
}

static const char* stage_name(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return "vert";
        case ShaderStage::Fragment: return "frag";
        case ShaderStage::Geometry: return "geom";
        case ShaderStage::Compute: return "comp";
    }
    return "unknown";
}

static bool performance_optimization_enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("TGFX2_VULKAN_SHADER_OPT");
        if (!env || env[0] == '\0') return true;
        std::string value(env);
        return value != "zero" && value != "0" && value != "none";
    }();
    return enabled;
}

static uint64_t fnv1a_append(uint64_t h, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ull;
    }
    return h;
}

static std::string hex64(uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[value & 0xfu];
        value >>= 4u;
    }
    return out;
}

static std::filesystem::path shader_cache_dir() {
    const char* explicit_dir = std::getenv("TGFX2_VULKAN_SHADER_CACHE_DIR");
    if (explicit_dir && explicit_dir[0] != '\0') {
        return std::filesystem::path(explicit_dir);
    }

    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "termin" / "tgfx2" / "spirv";
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".cache" / "termin" / "tgfx2" / "spirv";
    }

    return {};
}

static std::filesystem::path shader_cache_path(
    const std::string& source,
    ShaderStage stage,
    const std::string& entry_point
) {
    std::filesystem::path dir = shader_cache_dir();
    if (dir.empty()) return {};

    uint64_t h = 1469598103934665603ull;
    const uint32_t version = SPIRV_CACHE_VERSION;
    const uint32_t stage_u32 = static_cast<uint32_t>(stage);
    const uint32_t opt_u32 = performance_optimization_enabled() ? 1u : 0u;
    h = fnv1a_append(h, &version, sizeof(version));
    h = fnv1a_append(h, &stage_u32, sizeof(stage_u32));
    h = fnv1a_append(h, &opt_u32, sizeof(opt_u32));
    h = fnv1a_append(h, entry_point.data(), entry_point.size());
    h = fnv1a_append(h, "\0", 1);
    h = fnv1a_append(h, source.data(), source.size());

    std::ostringstream name;
    name << "glsl-" << stage_name(stage) << "-" << hex64(h) << ".spvbin";
    return dir / name.str();
}

static bool load_spirv_cache(const std::filesystem::path& path, std::vector<uint32_t>& out) {
    if (path.empty()) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t word_count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&word_count), sizeof(word_count));
    if (!in || magic != SPIRV_CACHE_MAGIC || version != SPIRV_CACHE_VERSION || word_count == 0) {
        return false;
    }

    std::vector<uint32_t> words(word_count);
    in.read(reinterpret_cast<char*>(words.data()), word_count * sizeof(uint32_t));
    if (!in) return false;

    out = std::move(words);
    return true;
}

static void store_spirv_cache(const std::filesystem::path& path, const std::vector<uint32_t>& words) {
    if (path.empty() || words.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;

    const uint32_t magic = SPIRV_CACHE_MAGIC;
    const uint32_t version = SPIRV_CACHE_VERSION;
    const uint32_t word_count = static_cast<uint32_t>(words.size());
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&word_count), sizeof(word_count));
    out.write(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
}

} // namespace

SpirvCompileResult compile_glsl_to_spirv(
    const std::string& source,
    ShaderStage stage,
    const std::string& entry_point
) {
    SpirvCompileResult result;

    std::filesystem::path cache_path = shader_cache_path(source, stage, entry_point);
    if (load_spirv_cache(cache_path, result.spirv)) {
        result.success = true;
        return result;
    }

    static std::mutex compiler_mutex;
    static shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Target Vulkan 1.2 / SPIR-V 1.5 (compatible with validation layers)
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_5);
    options.SetOptimizationLevel(performance_optimization_enabled()
        ? shaderc_optimization_level_performance
        : shaderc_optimization_level_zero);

    // Force GLSL 450 profile to handle #version 330 sources
    options.SetForcedVersionProfile(450, shaderc_profile_core);

    // Auto-assign `layout(location=N)` to `in`/`out` varyings that
    // lack an explicit qualifier. Legacy shaders inherited from the
    // GL 3.3 era declare `in vec3 a_position;` / `out vec3 v_normal;`
    // without locations — SPIR-V requires locations for user inputs
    // and outputs, and shaderc honours this flag to emit them
    // automatically (in declaration order).
    //
    // Auto-bind for resources at the same time: auto-picks binding
    // slots for `uniform sampler2D u_foo;` declarations lacking an
    // explicit `layout(binding=N)`. UBOs/SSBOs we always tag with
    // explicit bindings in the engine, so auto-bind for those would
    // never fire; leaving it on is free.
    options.SetAutoMapLocations(true);
    options.SetAutoBindUniforms(true);

    // Shaders fork their declarations with `#ifdef VULKAN`. shaderc is
    // supposed to auto-define `VULKAN=100` when targeting Vulkan, but in
    // practice that depends on the target env + SPIR-V version combo and
    // proved unreliable here (shaders kept landing in the `#else` branch
    // and trying to bind the GL-emulation UBO at slot 14, breaking
    // pipeline layout validation). Define the macro explicitly — safe
    // because shaderc's own define (if any) uses the same name and
    // compatible numeric value, and in that rare case shaderc silently
    // accepts a matching redefinition.
    options.AddMacroDefinition("VULKAN", "100");

    auto kind = to_shaderc_kind(stage);
    shaderc::SpvCompilationResult module;
    {
        std::lock_guard<std::mutex> lock(compiler_mutex);
        module = compiler.CompileGlslToSpv(source, kind, "shader", entry_point.c_str(), options);
    }

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.success = false;
        result.error_message = module.GetErrorMessage();
        // Dump the offending source with line numbers so the error
        // position ("shader:11:") actually means something. Stderr
        // because the log channel may be routed elsewhere.
        fprintf(stderr, "=== shader compile failed — source dump ===\n");
        size_t line_no = 1;
        size_t pos = 0;
        while (pos < source.size()) {
            size_t eol = source.find('\n', pos);
            size_t end = (eol == std::string::npos) ? source.size() : eol;
            fprintf(stderr, "%3zu: %.*s\n",
                    line_no, static_cast<int>(end - pos), source.data() + pos);
            if (eol == std::string::npos) break;
            pos = eol + 1;
            ++line_no;
        }
        fprintf(stderr, "=== end source dump ===\n");
        return result;
    }

    result.spirv.assign(module.cbegin(), module.cend());
    store_spirv_cache(cache_path, result.spirv);
    result.success = true;
    return result;
}

#else

SpirvCompileResult compile_glsl_to_spirv(
    const std::string& source,
    ShaderStage stage,
    const std::string& entry_point
) {
    (void)source;
    (void)stage;
    (void)entry_point;

    SpirvCompileResult result;
    result.success = false;
    result.error_message =
        "runtime GLSL compilation is disabled in this build; provide SPIR-V bytecode";
    tc_log(TC_LOG_ERROR, "tgfx2 Vulkan: %s", result.error_message.c_str());
    return result;
}

#endif

} // namespace tgfx::vk

#endif // TGFX2_HAS_VULKAN
