#pragma once

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

#include <cstddef>
#include <cstdint>

namespace tgfx {

struct EngineShaderStageIo {
    const char* name;
    const char* semantic;
    int location;
};

struct EngineShaderResourceDecl {
    const char* name;
    const char* logical_name;
    const char* kind;
    const char* scope;
    std::uint32_t size;
    std::uint32_t element_stride;
};

struct EngineShaderStageSource {
    const char* uuid;
    const char* name;
    ShaderStage stage;
    const char* language;
    const char* source_resource_path;
    const char* entry_point;
    const EngineShaderStageIo* inputs;
    std::size_t input_count;
    const EngineShaderStageIo* outputs;
    std::size_t output_count;
    const EngineShaderResourceDecl* resources;
    std::size_t resource_count;
};

struct EngineShaderProgramSource {
    const char* uuid;
    const char* name;
    const char* language;
    const EngineShaderStageSource* vertex_stage;
    const EngineShaderStageSource* fragment_stage;
    const EngineShaderResourceDecl* resources;
    std::size_t resource_count;
};

TGFX2_API const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader();
TGFX2_API const EngineShaderStageSource* find_engine_shader_stage(
    const char* uuid,
    ShaderStage stage);
TGFX2_API const EngineShaderProgramSource* find_engine_shader_program(const char* uuid);

} // namespace tgfx
