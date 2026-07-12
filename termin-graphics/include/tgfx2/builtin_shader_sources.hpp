#pragma once

#include "tgfx2/tgfx2_api.h"

#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

struct BuiltinShaderProgramSource {
    std::string name;
    std::string source;
};

struct BuiltinShaderStageMetadata {
    std::string uuid;
    std::string name;
    std::string language;
    std::string path;
    std::string entry_point;
};

TGFX2_API std::string load_builtin_shader_source(const char* filename, const char* debug_name);
TGFX2_API std::vector<std::filesystem::path> builtin_shader_roots();
TGFX2_API tc_shader_handle register_builtin_fragment_shader(
    const char* filename,
    const char* name,
    const char* uuid);
TGFX2_API tc_shader_handle register_builtin_vertex_fragment_shader(
    const char* vertex_filename,
    const char* fragment_filename,
    const char* name,
    const char* uuid);
TGFX2_API std::string load_builtin_shader_stage_source_from_catalog(
    const char* uuid,
    const char* stage_name);
TGFX2_API BuiltinShaderStageMetadata load_builtin_shader_stage_metadata_from_catalog(
    const char* uuid,
    const char* stage_name);
TGFX2_API tc_shader_handle register_builtin_shader_from_catalog(const char* uuid);
TGFX2_API BuiltinShaderProgramSource load_builtin_shader_program_from_catalog(const char* uuid);

} // namespace tgfx
