#pragma once

#include <string>
#include <vector>

#include "termin/render/render_export.hpp"

namespace termin {

struct GraphNodeSocketDef {
    std::string name;
    std::string socket_type;
};

struct GraphNodeDef {
    std::string node_type;
    std::string title;
    bool executable = true;
    std::vector<GraphNodeSocketDef> inputs;
    std::vector<GraphNodeSocketDef> outputs;
};

RENDER_API const GraphNodeDef* graph_node_def_find(const std::string& node_type);
RENDER_API const std::vector<GraphNodeDef>& graph_node_defs();

} // namespace termin

