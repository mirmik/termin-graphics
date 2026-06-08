#include "tgfx2/engine_shader_catalog.hpp"

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

const EngineShaderStageSource kFullscreenQuadVertexShader{
    "termin-engine-fsq",
    "FullscreenQuadEngineVS",
    ShaderStage::Vertex,
    "slang",
    "builtin_shaders/termin-engine-fsq.vert.slang",
    kFullscreenQuadFallbackGlsl,
};

} // namespace

const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader() {
    return kFullscreenQuadVertexShader;
}

} // namespace tgfx
