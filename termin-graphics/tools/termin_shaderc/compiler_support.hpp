#pragma once

#include "cli.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace termin_shaderc::internal {

bool is_existing_file(const std::filesystem::path& path);
std::optional<std::string> resolve_slangc(const CompileOptions& options, const char* argv0);
std::optional<std::string> resolve_fxc(const CompileOptions& options, const char* argv0);
std::vector<std::string> slang_include_dirs(const CompileOptions& options, const char* argv0);
int run_command(const std::vector<std::string>& args);

} // namespace termin_shaderc::internal
