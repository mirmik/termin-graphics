#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin/render/graph_data.hpp"
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

std::vector<NodeData*> topological_sort(GraphData& graph);

ResourceNaming assign_resource_names(const GraphData& graph);

std::unordered_map<std::string, std::string> build_node_viewport_map(
    const std::vector<NodeData>& nodes,
    const std::vector<ViewportFrameData>& frames
);

const ViewportFrameData* find_containing_frame(
    const NodeData& node,
    const std::vector<ViewportFrameData>& frames
);

termin::RenderPipeline* compile_graph(GraphData& graph);
termin::RenderPipeline* compile_graph(const nos::trent& graph_trent);
termin::RenderPipeline* compile_graph(const std::string& json_str);

} // namespace tc
