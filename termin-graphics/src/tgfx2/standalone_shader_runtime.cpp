#include "tgfx2/standalone_shader_runtime.hpp"

#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/graphics_host.hpp"
#include "tgfx2/shader_artifact_resolver.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tcbase/settings.h>
#include <tcbase/tc_log.h>

namespace tgfx {

    namespace {

        namespace fs = std::filesystem;

        bool is_file(const fs::path& path) {
            std::error_code error;
            return fs::is_regular_file(path, error);
        }

        fs::path normalized_path(const fs::path& path) {
            std::error_code error;
            const fs::path canonical = fs::weakly_canonical(path, error);
            return error ? fs::absolute(path).lexically_normal() : canonical;
        }

        fs::path executable_path(fs::path path) {
#ifdef _WIN32
            if (path.extension().empty()) {
                path += ".exe";
            }
#endif
            return path;
        }

        std::vector<fs::path> split_path_environment() {
            std::vector<fs::path> result;
            const char* value = std::getenv("PATH");
            if (!value || value[0] == '\0') {
                return result;
            }
#ifdef _WIN32
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            const std::string text(value);
            size_t begin = 0;
            while (begin <= text.size()) {
                const size_t end = text.find(separator, begin);
                const std::string item = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!item.empty()) {
                    result.emplace_back(item);
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
            return result;
        }

        std::optional<fs::path> configured_tool(const char* environment_name, const char* tool_name) {
            const char* value = std::getenv(environment_name);
            if (!value || value[0] == '\0') {
                return std::nullopt;
            }
            const fs::path candidate = executable_path(value);
            if (!is_file(candidate)) {
                tc_log_error("[StandaloneShaderRuntime] %s points to missing %s: '%s'",
                             environment_name,
                             tool_name,
                             candidate.string().c_str());
                return fs::path{};
            }
            return normalized_path(candidate);
        }

        std::optional<fs::path> settings_tool(const char* settings_key, const char* tool_name) {
            const tc::Settings settings("termin");
            const nos::trent& value = settings.get(settings_key);
            if (value.is_nil()) {
                return std::nullopt;
            }
            if (!value.is_string()) {
                tc_log_error("[StandaloneShaderRuntime] %s must be a string", settings_key);
                return fs::path{};
            }
            if (value.as_string().empty()) {
                return std::nullopt;
            }
            const fs::path candidate = executable_path(value.as_string());
            if (!is_file(candidate)) {
                tc_log_error("[StandaloneShaderRuntime] %s points to missing %s: '%s'",
                             settings_key,
                             tool_name,
                             candidate.string().c_str());
                return fs::path{};
            }
            return normalized_path(candidate);
        }

        void append_sdk_tool_candidates(std::vector<fs::path>& candidates, const char* tool_name) {
            if (const char* sdk = std::getenv("TERMIN_SDK"); sdk && sdk[0] != '\0') {
                candidates.push_back(executable_path(fs::path(sdk) / "bin" / tool_name));
            }

            for (const fs::path& builtin_root : builtin_shader_roots()) {
                // <sdk>/share/termin/builtin_shaders -> <sdk>/bin
                const fs::path sdk_root = builtin_root.parent_path().parent_path().parent_path();
                candidates.push_back(executable_path(sdk_root / "bin" / tool_name));
            }

            for (const fs::path& directory : split_path_environment()) {
                candidates.push_back(executable_path(directory / tool_name));
            }
        }

        std::optional<fs::path>
        resolve_tool(const char* environment_name, const char* tool_name, const char* settings_key) {
            const std::optional<fs::path> configured = configured_tool(environment_name, tool_name);
            if (configured.has_value()) {
                if (configured->empty()) {
                    return std::nullopt;
                }
                return configured;
            }

            const std::optional<fs::path> from_settings = settings_tool(settings_key, tool_name);
            if (from_settings.has_value()) {
                if (from_settings->empty()) {
                    return std::nullopt;
                }
                return from_settings;
            }

            std::vector<fs::path> candidates;
            append_sdk_tool_candidates(candidates, tool_name);
            for (const fs::path& candidate : candidates) {
                if (is_file(candidate)) {
                    return normalized_path(candidate);
                }
            }

            tc_log_error("[StandaloneShaderRuntime] %s not found; set %s, install it under "
                         "TERMIN_SDK/bin, or add it to PATH",
                         tool_name,
                         environment_name);
            return std::nullopt;
        }

        bool valid_label(std::string_view label) {
            if (label.empty()) {
                return false;
            }
            for (const unsigned char value : label) {
                if (!std::isalnum(value) && value != '-' && value != '_' && value != '.') {
                    return false;
                }
            }
            return label != "." && label != "..";
        }

        std::optional<fs::path> platform_cache_root(const char* label) {
            if (const char* configured = std::getenv("TERMIN_SDK_SHADER_CACHE_ROOT");
                configured && configured[0] != '\0') {
                return fs::path(configured) / label;
            }

#ifdef _WIN32
            if (const char* local_app_data = std::getenv("LOCALAPPDATA"); local_app_data && local_app_data[0] != '\0') {
                return fs::path(local_app_data) / "Termin" / "Cache" / (std::string(label) + "-shaders");
            }
            if (const char* user_profile = std::getenv("USERPROFILE"); user_profile && user_profile[0] != '\0') {
                return fs::path(user_profile) / "AppData" / "Local" / "Termin" / "Cache" /
                       (std::string(label) + "-shaders");
            }
#else
            if (const char* xdg_cache = std::getenv("XDG_CACHE_HOME"); xdg_cache && xdg_cache[0] != '\0') {
                return fs::path(xdg_cache) / "termin" / (std::string(label) + "-shaders");
            }
            if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
                return fs::path(home) / ".cache" / "termin" / (std::string(label) + "-shaders");
            }
#endif

            tc_log_error("[StandaloneShaderRuntime] user cache directory is unavailable; "
                         "set TERMIN_SDK_SHADER_CACHE_ROOT");
            return std::nullopt;
        }

        bool create_directory(const fs::path& path, const char* label) {
            std::error_code error;
            fs::create_directories(path, error);
            if (error) {
                tc_log_error("[StandaloneShaderRuntime] failed to create %s '%s': %s",
                             label,
                             path.string().c_str(),
                             error.message().c_str());
                return false;
            }
            return true;
        }

        bool set_slang_compiler_environment(const fs::path& path) {
            static std::mutex environment_mutex;
            const std::lock_guard<std::mutex> lock(environment_mutex);
#ifdef _WIN32
            if (_putenv_s("TERMIN_SLANGC", path.string().c_str()) != 0) {
#else
            if (setenv("TERMIN_SLANGC", path.string().c_str(), 1) != 0) {
#endif
                tc_log_error("[StandaloneShaderRuntime] failed to configure TERMIN_SLANGC='%s'", path.string().c_str());
                return false;
            }
            return true;
        }

    } // anonymous namespace

    bool configure_default_standalone_shader_runtime(GraphicsHost& host, const char* label) {
        const std::string_view label_view = label ? label : "";
        if (!valid_label(label_view)) {
            tc_log_error("[StandaloneShaderRuntime] cache label must contain only letters, "
                         "digits, '.', '_' or '-': '%s'",
                         label ? label : "<null>");
            return false;
        }
        if (host.is_closed()) {
            tc_log_error("[StandaloneShaderRuntime] cannot configure a closed GraphicsHost");
            return false;
        }

        const std::optional<fs::path> shader_compiler =
            resolve_tool("TERMIN_SHADERC", "termin_shaderc", "Build/shaderCompiler");
        if (!shader_compiler) {
            return false;
        }
        const std::optional<fs::path> slang_compiler = resolve_tool("TERMIN_SLANGC", "slangc", "Shader/slangCompiler");
        if (!slang_compiler) {
            return false;
        }
        const std::optional<fs::path> runtime_root = platform_cache_root(label);
        if (!runtime_root) {
            return false;
        }

        const fs::path artifact_root = *runtime_root / "artifacts";
        const fs::path cache_root = *runtime_root / "cache";
        if (!create_directory(artifact_root, "shader artifact directory") ||
            !create_directory(cache_root, "shader cache directory") ||
            !set_slang_compiler_environment(*slang_compiler)) {
            return false;
        }

        host.configure_shader_artifacts(termin::ShaderArtifactResolver(normalized_path(artifact_root).string(),
                                                                       normalized_path(cache_root).string(),
                                                                       shader_compiler->string(),
                                                                       true));
        tc_log_info("[StandaloneShaderRuntime] %s configured: artifact_root='%s', "
                    "cache_root='%s', termin_shaderc='%s', slangc='%s'",
                    label,
                    normalized_path(artifact_root).string().c_str(),
                    normalized_path(cache_root).string().c_str(),
                    shader_compiler->string().c_str(),
                    slang_compiler->string().c_str());
        return true;
    }

} // namespace tgfx
