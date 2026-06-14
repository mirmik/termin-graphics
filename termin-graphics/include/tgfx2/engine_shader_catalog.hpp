#pragma once

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

#include <cstddef>

namespace tgfx {

struct EngineShaderStageIo {
    const char* name;
    const char* semantic;
    int location;
};

struct EngineShaderResourceBinding {
    const char* name;
    const char* logical_name;
    const char* kind;
    int binding;
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
    const EngineShaderResourceBinding* resources;
    std::size_t resource_count;
};

TGFX2_API const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader();

} // namespace tgfx
