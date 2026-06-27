#include "tgfx2/engine_shader_catalog.hpp"

#include <cstring>

namespace tgfx {

namespace {

const EngineShaderStageSource kFullscreenQuadVertexShader{
    "termin-engine-fsq",
    "FullscreenQuadEngineVS",
    ShaderStage::Vertex,
    "slang",
    "builtin_shaders/termin-engine-fsq.vert.slang",
    "vs_main",
};

const EngineShaderStageSource kShadowVertexShader{
    "termin-engine-shadow",
    "ShadowEngineVSFS",
    ShaderStage::Vertex,
    "slang",
    "builtin_shaders/termin-engine-shadow.slang",
    "vs_main",
};

const EngineShaderStageSource kShadowFragmentShader{
    "termin-engine-shadow",
    "ShadowEngineVSFS",
    ShaderStage::Fragment,
    "slang",
    "builtin_shaders/termin-engine-shadow.slang",
    "fs_main",
};

const EngineShaderStageSource kTonemapFragmentShader{
    "termin-engine-tonemap",
    "TonemapEngineFS",
    ShaderStage::Fragment,
    "slang",
    "builtin_shaders/termin-engine-tonemap.frag.slang",
    "fs_main",
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
    },
    {
        "termin-engine-tonemap",
        "TonemapEngineFS",
        "slang",
        nullptr,
        &kTonemapFragmentShader,
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
