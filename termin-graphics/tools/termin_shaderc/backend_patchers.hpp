#pragma once

#include "cli.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace termin_shaderc::internal {

struct ShaderResourceBinding {
    std::string name;
    std::string kind;
    std::string scope;
    std::string slang_glsl_symbol;
    std::string d3d11_register_class;
    uint32_t d3d11_register_index = 0;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t stage_mask = 0;
    uint32_t size = 0;
    bool slang_combined_texture = false;
    bool slang_split_texture = false;
    bool slang_separate_sampler = false;
    bool slang_storage_texture = false;
    bool d3d11_scalar_sampler_for_texture_array = false;

    struct Field {
        std::string name;
        std::string type;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    std::vector<Field> fields;
};

bool read_file(const std::string& path, std::string& out);
bool ensure_parent_directory(const std::filesystem::path& path, const char* label);
bool write_spirv(const std::string& path, const std::vector<uint32_t>& words);
std::string regex_escape(const std::string& value);
bool is_identifier_char(char ch);
uint32_t stage_mask_for_stage(const std::string& stage);
void assign_d3d11_register_placement(std::vector<ShaderResourceBinding>& resources);
void assign_missing_resource_scopes(std::vector<ShaderResourceBinding>& resources);
void apply_default_resource_scope(
    std::vector<ShaderResourceBinding>& resources,
    const std::string& default_scope);
void normalize_scope_first_binding_slots(
    std::vector<ShaderResourceBinding>& resources,
    bool normalize_transient_resources,
    const std::string& target = {});
bool has_resource_named(
    const std::vector<ShaderResourceBinding>& resources,
    const std::string& name);

void annotate_slang_glsl_symbols(
    std::vector<ShaderResourceBinding>& resources,
    const std::string& source);
bool filter_slang_opengl_resources_for_glsl(
    const CompileOptions& options,
    std::vector<ShaderResourceBinding>& resources);
bool patch_slang_opengl_glsl_resource_bindings(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources);
bool legalize_slang_opengl_glsl_builtins(const CompileOptions& options);
bool augment_d3d11_resource_bindings_from_hlsl(
    const CompileOptions& options,
    const std::filesystem::path& hlsl_path,
    const std::vector<ShaderResourceBinding>& declared_resources,
    std::vector<ShaderResourceBinding>& resources);
bool patch_slang_d3d11_hlsl_resource_bindings(
    const CompileOptions& options,
    const std::filesystem::path& hlsl_path,
    const std::vector<ShaderResourceBinding>& resources);
bool filter_slang_vulkan_resources_for_spirv(
    const CompileOptions& options,
    std::vector<ShaderResourceBinding>& resources);
bool patch_slang_vulkan_spirv_descriptor_decorations(
    const CompileOptions& options,
    const std::vector<ShaderResourceBinding>& resources);

} // namespace termin_shaderc::internal
