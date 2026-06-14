#include "tgfx2/engine_shader_catalog.hpp"

#include <iterator>

namespace tgfx {

namespace {

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
