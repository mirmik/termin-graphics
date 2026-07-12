#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/builtin_shader_sources.hpp"

#include <stdexcept>
#include <string>

namespace tgfx {

namespace {

struct OwnedEngineShaderStage {
    BuiltinShaderStageMetadata metadata;
    std::string resource_path;
    EngineShaderStageSource view{};

    OwnedEngineShaderStage(const char* uuid, const char* stage_name, ShaderStage stage)
        : metadata(load_builtin_shader_stage_metadata_from_catalog(uuid, stage_name))
    {
        if (metadata.path.empty()) {
            throw std::runtime_error(
                std::string("missing built-in shader manifest entry: ") + uuid + ":" + stage_name);
        }
        resource_path = "builtin_shaders/" + metadata.path;
        view = {
            metadata.uuid.c_str(),
            metadata.name.c_str(),
            stage,
            metadata.language.c_str(),
            resource_path.c_str(),
            metadata.entry_point.empty() ? nullptr : metadata.entry_point.c_str(),
        };
    }
};

} // namespace

const EngineShaderStageSource& engine_fullscreen_quad_vertex_shader() {
    static const OwnedEngineShaderStage shader =
        OwnedEngineShaderStage("termin-engine-fsq", "vertex", ShaderStage::Vertex);
    return shader.view;
}

} // namespace tgfx
