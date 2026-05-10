#include "termin/render/graph_node_def.hpp"

namespace termin {

namespace {

const std::vector<GraphNodeDef>& builtin_defs() {
    static const std::vector<GraphNodeDef> defs = {
        GraphNodeDef{
            "resource",
            "Resource",
            false,
            {},
            {{"fbo", "fbo"}},
        },
        GraphNodeDef{
            "external_rt",
            "External RT",
            false,
            {},
            {{"fbo", "fbo"}},
        },
        GraphNodeDef{
            "render_target_input",
            "Render Target Input",
            false,
            {},
            {{"color", "fbo"}},
        },
        GraphNodeDef{
            "pipeline_output",
            "Pipeline Output",
            false,
            {{"color", "fbo"}},
            {},
        },
        GraphNodeDef{
            "output",
            "Render Target",
            false,
            {{"color", "fbo"}, {"depth", "fbo"}},
            {},
        },
        GraphNodeDef{
            "fbo_split",
            "FBO Split",
            false,
            {{"fbo", "fbo"}},
            {{"color", "color_texture"}, {"depth", "depth_texture"}},
        },
        GraphNodeDef{
            "fbo_join",
            "FBO Join",
            false,
            {{"color", "color_texture"}, {"depth", "depth_texture"}},
            {{"fbo", "fbo"}},
        },
    };
    return defs;
}

} // namespace

const GraphNodeDef* graph_node_def_find(const std::string& node_type) {
    for (const auto& def : builtin_defs()) {
        if (def.node_type == node_type) {
            return &def;
        }
    }
    return nullptr;
}

const std::vector<GraphNodeDef>& graph_node_defs() {
    return builtin_defs();
}

} // namespace termin

