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

TGFX2_API const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader();

} // namespace tgfx
