#include "compiler_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <tgfx2/internal/process_runner.hpp>

namespace termin_shaderc::internal {

bool is_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec)
        && !std::filesystem::is_directory(path, ec);
}

static std::vector<std::string> split_paths(const char* value) {
    std::vector<std::string> paths;
    if (!value || value[0] == '\0') {
        return paths;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string text(value);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            paths.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}

static std::vector<std::string> executable_names(const std::string& base) {
#ifdef _WIN32
    if (base.size() >= 4 && base.substr(base.size() - 4) == ".exe") {
        return {base};
    }
    return {base + ".exe", base};
#else
    return {base};
#endif
}

static std::optional<std::string> find_on_path(const std::string& exe_name) {
    for (const std::string& dir : split_paths(std::getenv("PATH"))) {
        for (const std::string& name : executable_names(exe_name)) {
            std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (is_existing_file(candidate)) {
                return candidate.string();
            }
        }
    }
    return std::nullopt;
}

static std::optional<std::string> find_sdk_tool(const std::string& exe_name, const char* argv0) {
    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            for (const std::string& name : executable_names(exe_name)) {
                std::filesystem::path candidate = std::filesystem::path(sdk) / "bin" / name;
                if (is_existing_file(candidate)) {
                    return candidate.string();
                }
            }
        }
    }

    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        std::filesystem::path tool_dir = std::filesystem::absolute(argv0, ec).parent_path();
        if (!ec) {
            for (const std::string& name : executable_names(exe_name)) {
                std::filesystem::path candidate = tool_dir / name;
                if (is_existing_file(candidate)) {
                    return candidate.string();
                }
            }
        }
    }
    return std::nullopt;
}

#ifdef _WIN32
static std::optional<std::string> find_windows_sdk_fxc() {
    std::vector<std::filesystem::path> roots;
    if (const char* program_files_x86 = std::getenv("ProgramFiles(x86)")) {
        if (program_files_x86[0] != '\0') {
            roots.emplace_back(
                std::filesystem::path(program_files_x86) / "Windows Kits" / "10" / "bin");
        }
    }
    roots.emplace_back("C:/Program Files (x86)/Windows Kits/10/bin");

    std::vector<std::filesystem::path> candidates;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::filesystem::path candidate = entry.path() / "x64" / "fxc.exe";
            if (is_existing_file(candidate)) {
                candidates.push_back(candidate);
            }
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates.back().string();
}
#endif

std::optional<std::string> resolve_slangc(const CompileOptions& options, const char* argv0) {
    if (!options.slangc.empty()) {
        if (!is_existing_file(options.slangc)) {
            std::cerr << "termin_shaderc: slangc does not exist: " << options.slangc << "\n";
            return std::nullopt;
        }
        return options.slangc;
    }

    if (const char* env = std::getenv("TERMIN_SLANGC")) {
        if (env[0] != '\0') {
            if (!is_existing_file(env)) {
                std::cerr << "termin_shaderc: TERMIN_SLANGC points to missing slangc: " << env << "\n";
                return std::nullopt;
            }
            return std::string(env);
        }
    }

    if (auto found = find_on_path("slangc")) {
        return found;
    }
    if (auto found = find_sdk_tool("slangc", argv0)) {
        return found;
    }

    std::cerr
        << "termin_shaderc: slangc not found. Set TERMIN_SLANGC, add slangc to PATH, "
        << "or install it under TERMIN_SDK/bin.\n";
    return std::nullopt;
}

std::optional<std::string> resolve_fxc(const CompileOptions& options, const char* argv0) {
    if (!options.fxc.empty()) {
        if (!is_existing_file(options.fxc)) {
            std::cerr << "termin_shaderc: fxc does not exist: " << options.fxc << "\n";
            return std::nullopt;
        }
        return options.fxc;
    }

    if (const char* env = std::getenv("TERMIN_FXC")) {
        if (env[0] != '\0') {
            if (!is_existing_file(env)) {
                std::cerr << "termin_shaderc: TERMIN_FXC points to missing fxc: " << env << "\n";
                return std::nullopt;
            }
            return std::string(env);
        }
    }

    if (auto found = find_on_path("fxc")) {
        return found;
    }
    if (auto found = find_sdk_tool("fxc", argv0)) {
        return found;
    }
#ifdef _WIN32
    if (auto found = find_windows_sdk_fxc()) {
        return found;
    }
#endif

    std::cerr
        << "termin_shaderc: fxc not found. Set TERMIN_FXC, add fxc to PATH, "
        << "install it under TERMIN_SDK/bin, or install the Windows SDK.\n";
    return std::nullopt;
}

static void append_unique_existing_dir(
    std::vector<std::string>& dirs,
    const std::filesystem::path& path
) {
    std::error_code ec;
    if (path.empty() ||
        !std::filesystem::exists(path, ec) ||
        !std::filesystem::is_directory(path, ec)) {
        return;
    }
    std::filesystem::path normalized = std::filesystem::absolute(path, ec);
    std::string text = ec ? path.string() : normalized.lexically_normal().string();
    for (const std::string& existing : dirs) {
        if (existing == text) {
            return;
        }
    }
    dirs.push_back(std::move(text));
}

std::vector<std::string> slang_include_dirs(
    const CompileOptions& options,
    const char* argv0
) {
    std::vector<std::string> dirs;
    for (const std::string& dir : options.include_dirs) {
        append_unique_existing_dir(dirs, dir);
    }

    append_unique_existing_dir(dirs, std::filesystem::path(options.input).parent_path());

    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            append_unique_existing_dir(
                dirs,
                std::filesystem::path(sdk) / "share" / "termin" / "builtin_shaders");
        }
    }

    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        std::filesystem::path tool_dir = std::filesystem::absolute(argv0, ec).parent_path();
        if (!ec) {
            append_unique_existing_dir(
                dirs,
                tool_dir.parent_path() / "share" / "termin" / "builtin_shaders");
            append_unique_existing_dir(
                dirs,
                tool_dir / "share" / "termin" / "builtin_shaders");
        }
    }

    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        append_unique_existing_dir(dirs, cwd / "share" / "termin" / "builtin_shaders");
        append_unique_existing_dir(dirs, cwd / "termin-graphics" / "resources" / "builtin_shaders");
    }

    return dirs;
}

int run_command(const std::vector<std::string>& args) {
    tgfx::internal::ProcessResult result = tgfx::internal::run_process(args, true);
    if (!result.start_error.empty()) {
        std::cerr << "termin_shaderc: failed to run "
                  << (args.empty() ? "<empty>" : args[0])
                  << ": " << result.start_error << "\n";
    }
    if (!result.output.empty()) {
        std::cerr << result.output;
    }
    return result.exit_code;
}

} // namespace termin_shaderc::internal
