#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin/render/graph_data.hpp"
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
};

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
RENDER_API termin::RenderPipeline* compile_graph(const nos::trent& graph_trent);
RENDER_API termin::RenderPipeline* compile_graph(const std::string& json_str);

} // namespace tc
