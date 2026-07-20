#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin/render/graph_data.hpp"
#include "termin/render/resource_aliases.hpp"
#include "termin/render/render_export.hpp"
#include "termin/render/render_pipeline.hpp"

namespace tc {

class GraphCompileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ResourceNaming {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> socket_names;
    std::unordered_map<std::string, std::string> resource_types;
    std::unordered_map<std::string, std::vector<std::string>> target_aliases;
    std::unordered_map<std::string, std::string> external_resources;
    std::unordered_map<std::string, termin::ResourceView> resource_views;
    std::unordered_map<std::string, termin::FboComposition> fbo_compositions;
};

struct PipelineTemplateTarget {
    std::string viewport_name;
    std::string export_name;
    int32_t width = 0;
    int32_t height = 0;
};

// Publish a fully compiled execution candidate into a stable canonical
// template. All descriptor storage is prepared before the registry swaps the
// live payload, so failure leaves the previous template version untouched.
RENDER_API bool publish_pipeline_template(
    termin::RenderPipeline& instance,
    const termin::TcPipelineTemplate& pipeline_template,
    const std::vector<std::string>& pass_parameters = {},
    const std::vector<PipelineTemplateTarget>& targets = {}
);

RENDER_API std::vector<NodeData*> topological_sort(GraphData& graph);

RENDER_API ResourceNaming assign_resource_names(const GraphData& graph);

RENDER_API std::unordered_map<std::string, std::string> build_node_viewport_map(
    const std::vector<NodeData>& nodes,
    const std::vector<ViewportFrameData>& frames
);

RENDER_API const ViewportFrameData* find_containing_frame(
    const NodeData& node,
    const std::vector<ViewportFrameData>& frames
);

RENDER_API termin::RenderPipeline* compile_graph(GraphData& graph);
RENDER_API termin::RenderPipeline* compile_graph(
    GraphData& graph,
    const termin::TcPipelineTemplate& pipeline_template
);
RENDER_API termin::RenderPipeline* compile_graph(const nos::trent& graph_trent);
RENDER_API termin::RenderPipeline* compile_graph(const std::string& json_str);
RENDER_API termin::RenderPipeline* compile_graph(
    const std::string& json_str,
    const termin::TcPipelineTemplate& pipeline_template
);

} // namespace tc
