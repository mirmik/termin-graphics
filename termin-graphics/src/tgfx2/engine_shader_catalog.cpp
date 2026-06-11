#include "tgfx2/engine_shader_catalog.hpp"

#include <iterator>

namespace tgfx {

namespace {

constexpr const char* kFullscreenQuadFallbackGlsl = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 v_uv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
)";

constexpr EngineShaderStageIo kFullscreenQuadInputs[] = {
    {"position", "POSITION", 0},
    {"uv", "TEXCOORD0", 1},
};

constexpr EngineShaderStageIo kFullscreenQuadOutputs[] = {
    {"position", "SV_Position", -1},
    {"uv", "TEXCOORD0", 0},
};

const EngineShaderStageSource kFullscreenQuadVertexShader{
    "termin-engine-fsq",
    "FullscreenQuadEngineVS",
    ShaderStage::Vertex,
    "slang",
    "builtin_shaders/termin-engine-fsq.vert.slang",
    "vs_main",
    kFullscreenQuadFallbackGlsl,
    kFullscreenQuadInputs,
    std::size(kFullscreenQuadInputs),
    kFullscreenQuadOutputs,
    std::size(kFullscreenQuadOutputs),
    nullptr,
    0,
};

} // namespace

const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader() {
    return kFullscreenQuadVertexShader;
}

} // namespace tgfx
