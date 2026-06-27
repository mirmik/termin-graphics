#include "tgfx2/engine_shader_catalog.hpp"

#include <cstring>
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

constexpr EngineShaderStageIo kShadowInputs[] = {
    {"a_position", "POSITION", 0},
};

const EngineShaderStageSource kShadowVertexShader{
    "termin-engine-shadow",
    "ShadowEngineVSFS",
    ShaderStage::Vertex,
    "slang",
    "builtin_shaders/termin-engine-shadow.slang",
    "vs_main",
    kShadowInputs,
    std::size(kShadowInputs),
    nullptr,
    0,
    nullptr,
    0,
};

const EngineShaderStageSource kShadowFragmentShader{
    "termin-engine-shadow",
    "ShadowEngineVSFS",
    ShaderStage::Fragment,
    "slang",
    "builtin_shaders/termin-engine-shadow.slang",
    "fs_main",
    nullptr,
    0,
    nullptr,
    0,
    nullptr,
    0,
};

constexpr EngineShaderResourceDecl kShadowResources[] = {
    {"per_frame", "per_frame", "constant_buffer", "frame", 0, 0},
    {"shadow_draw", "draw", "constant_buffer", "draw", 0, 0},
};

constexpr EngineShaderStageIo kTonemapFragmentInputs[] = {
    {"uv", "TEXCOORD0", 0},
};

constexpr EngineShaderStageIo kTonemapFragmentOutputs[] = {
    {"color", "SV_Target0", 0},
};

const EngineShaderStageSource kTonemapFragmentShader{
    "termin-engine-tonemap",
    "TonemapEngineFS",
    ShaderStage::Fragment,
    "slang",
    "builtin_shaders/termin-engine-tonemap.frag.slang",
    "fs_main",
    kTonemapFragmentInputs,
    std::size(kTonemapFragmentInputs),
    kTonemapFragmentOutputs,
    std::size(kTonemapFragmentOutputs),
    nullptr,
    0,
};

constexpr EngineShaderResourceDecl kTonemapResources[] = {
    {"u_params", "params", "constant_buffer", "transient", 0, 0},
    {"u_input", "input_texture", "combined_sampler2d", "transient", 0, 0},
};

const EngineShaderStageSource* const kEngineShaderStages[] = {
    &kFullscreenQuadVertexShader,
    &kShadowVertexShader,
    &kShadowFragmentShader,
    &kTonemapFragmentShader,
};

constexpr EngineShaderProgramSource kEngineShaderPrograms[] = {
    {
        "termin-engine-shadow",
        "ShadowEngineVSFS",
        "slang",
        &kShadowVertexShader,
        &kShadowFragmentShader,
        kShadowResources,
        std::size(kShadowResources),
    },
    {
        "termin-engine-tonemap",
        "TonemapEngineFS",
        "slang",
        nullptr,
        &kTonemapFragmentShader,
        kTonemapResources,
        std::size(kTonemapResources),
    },
};

} // namespace

const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader() {
    return kFullscreenQuadVertexShader;
}

const EngineShaderStageSource* find_engine_shader_stage(
    const char* uuid,
    ShaderStage stage)
{
    if (!uuid || uuid[0] == '\0') {
        return nullptr;
    }
    for (const EngineShaderStageSource* shader_stage : kEngineShaderStages) {
        if (shader_stage &&
            shader_stage->stage == stage &&
            std::strcmp(shader_stage->uuid, uuid) == 0) {
            return shader_stage;
        }
    }
    return nullptr;
}

const EngineShaderProgramSource* find_engine_shader_program(const char* uuid) {
    if (!uuid || uuid[0] == '\0') {
        return nullptr;
    }
    for (const EngineShaderProgramSource& program : kEngineShaderPrograms) {
        if (std::strcmp(program.uuid, uuid) == 0) {
            return &program;
        }
    }
    return nullptr;
}

} // namespace tgfx
