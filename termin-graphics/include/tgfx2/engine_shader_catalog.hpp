#pragma once

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

struct EngineShaderStageSource {
    const char* uuid;
    const char* name;
    ShaderStage stage;
    const char* language;
    const char* source_resource_path;
    const char* entry_point;
};

struct EngineShaderProgramSource {
    const char* uuid;
    const char* name;
    const char* language;
    const EngineShaderStageSource* vertex_stage;
    const EngineShaderStageSource* fragment_stage;
};

TGFX2_API const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader();
TGFX2_API const EngineShaderStageSource* find_engine_shader_stage(
    const char* uuid,
    ShaderStage stage);
TGFX2_API const EngineShaderProgramSource* find_engine_shader_program(const char* uuid);

} // namespace tgfx
