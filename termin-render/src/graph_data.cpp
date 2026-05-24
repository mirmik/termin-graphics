#include "termin/render/graph_data.hpp"

#include <tcbase/tc_log.hpp>
#include <tc_inspect_cpp.hpp>
#include "termin/render/graph_node_def.hpp"
#include <trent/json.h>

extern "C" {
#include "inspect/tc_inspect.h"
}

namespace tc {

static bool is_graph_boundary_node_type(const std::string& node_type) {
    const termin::GraphNodeDef* def = termin::graph_node_def_find(node_type);
    return def && !def->executable;
}

static bool is_pass_node_type(const std::string& node_type) {
    return !is_graph_boundary_node_type(node_type);
}

static bool has_socket(const std::vector<SocketData>& sockets, const std::string& name) {
    for (const auto& socket : sockets) {
        if (socket.name == name) {
            return true;
        }
    }
    return false;
}

static bool graph_data_is_target_socket_name(const std::string& name) {
    return name.size() > 7 && name.substr(name.size() - 7) == "_target";
}

static std::string target_socket_type_for(
    const NodeData& node,
    const std::string& target_socket_name
) {
    if (!graph_data_is_target_socket_name(target_socket_name)) {
        return "fbo";
    }
    std::string output_name = target_socket_name.substr(0, target_socket_name.size() - 7);
    for (const auto& output : node.outputs) {
        if (output.name == output_name) {
            return output.socket_type;
        }
    }
    return "fbo";
}

static void add_sockets_from_graph_node_def(NodeData& node, const termin::GraphNodeDef& def) {
    for (const auto& input : def.inputs) {
        if (!has_socket(node.inputs, input.name)) {
            node.inputs.push_back({input.name, input.socket_type, true});
        }
    }
    for (const auto& output : def.outputs) {
        if (!has_socket(node.outputs, output.name)) {
            node.outputs.push_back({output.name, output.socket_type, false});
        }
    }
}

static std::string graph_data_resource_type_for_node(const NodeData& node) {
    if (node.params.contains("resource_type") && node.params["resource_type"].is_string()) {
        return node.params["resource_type"].as_string();
    }
    if (node.pass_class == "Color Texture") {
        return "color_texture";
    }
    if (node.pass_class == "Depth Texture") {
        return "depth_texture";
    }
    if (node.pass_class == "Shadow Maps") {
        return "shadow_map_array";
    }
    return "fbo";
}

static void configure_resource_node_sockets(NodeData& node) {
    if (node.node_type != "resource") {
        return;
    }

    node.outputs.clear();
    std::string resource_type = graph_data_resource_type_for_node(node);
    if (resource_type == "shadow_map_array") {
        node.outputs.push_back({"shadow", "shadow", false});
    } else if (resource_type == "color_texture") {
        node.outputs.push_back({"color", "color_texture", false});
    } else if (resource_type == "depth_texture") {
        node.outputs.push_back({"depth", "depth_texture", false});
    } else {
        node.outputs.push_back({"fbo", "fbo", false});
    }
}

static void add_socket_from_tc_value(std::vector<SocketData>& sockets, const tc_value* value, bool is_input) {
    if (!value || value->type != TC_VALUE_LIST || value->data.list.count < 2) {
        return;
    }
    const tc_value* name_value = &value->data.list.items[0];
    const tc_value* type_value = &value->data.list.items[1];
    if (name_value->type != TC_VALUE_STRING || !name_value->data.s) {
        return;
    }
    std::string name = name_value->data.s;
    if (name.empty() || has_socket(sockets, name)) {
        return;
    }

    std::string socket_type = "fbo";
    if (type_value->type == TC_VALUE_STRING && type_value->data.s && type_value->data.s[0]) {
        socket_type = type_value->data.s;
    }
    sockets.push_back({name, socket_type, is_input});
}

static void add_sockets_from_metadata(NodeData& node) {
    if (node.pass_class.empty()) {
        return;
    }

    tc::init_cpp_inspect_vtable();
    tc_value metadata = tc_inspect_get_type_metadata(node.pass_class.c_str());
    if (metadata.type != TC_VALUE_DICT) {
        tc_value_free(&metadata);
        return;
    }

    tc_value* graph = tc_value_dict_get(&metadata, "graph");
    if (!graph || graph->type != TC_VALUE_DICT) {
        tc_value_free(&metadata);
        return;
    }

    tc_value* inputs = tc_value_dict_get(graph, "node_inputs");
    if (inputs && inputs->type == TC_VALUE_LIST) {
        for (size_t i = 0; i < inputs->data.list.count; i++) {
            add_socket_from_tc_value(node.inputs, &inputs->data.list.items[i], true);
        }
    }

    tc_value* outputs = tc_value_dict_get(graph, "node_outputs");
    if (outputs && outputs->type == TC_VALUE_LIST) {
        for (size_t i = 0; i < outputs->data.list.count; i++) {
            add_socket_from_tc_value(node.outputs, &outputs->data.list.items[i], false);
        }
    }

    tc_value_free(&metadata);
}

NodeData* GraphData::get_node(const std::string& id) {
    for (auto& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

const NodeData* GraphData::get_node(const std::string& id) const {
    for (const auto& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

GraphData GraphData::from_trent(const nos::trent& t) {
    GraphData graph;

    if (t.contains("nodes") && t["nodes"].is_list()) {
        const auto& nodes_list = t["nodes"].as_list();
        for (size_t i = 0; i < nodes_list.size(); ++i) {
            const auto& node_t = nodes_list[i];
            NodeData node;

            node.id = std::to_string(i);

            if (node_t.contains("type") && node_t["type"].is_string()) {
                node.pass_class = node_t["type"].as_string();
            }
            if (node_t.contains("node_type") && node_t["node_type"].is_string()) {
                node.node_type = node_t["node_type"].as_string();
            } else {
                node.node_type = "pass";
            }
            if (node_t.contains("name") && node_t["name"].is_string()) {
                node.name = node_t["name"].as_string();
            }
            if (node_t.contains("params") && node_t["params"].is_dict()) {
                node.params = node_t["params"];
            }
            if (node_t.contains("x") && node_t["x"].is_numer()) {
                node.x = static_cast<float>(node_t["x"].as_numer());
            }
            if (node_t.contains("y") && node_t["y"].is_numer()) {
                node.y = static_cast<float>(node_t["y"].as_numer());
            }

            if (const termin::GraphNodeDef* def = termin::graph_node_def_find(node.node_type)) {
                add_sockets_from_graph_node_def(node, *def);
                configure_resource_node_sockets(node);
            } else if (is_pass_node_type(node.node_type)) {
                add_sockets_from_metadata(node);

                if (node_t.contains("dynamic_inputs") && node_t["dynamic_inputs"].is_list()) {
                    for (const auto& dyn : node_t["dynamic_inputs"].as_list()) {
                        if (!dyn.is_list() || dyn.as_list().size() < 2) {
                            continue;
                        }
                        std::string dyn_name = dyn[0].as_string();
                        std::string dyn_type = dyn[1].as_string();
                        if (!dyn_name.empty() && !has_socket(node.inputs, dyn_name)) {
                            node.inputs.push_back({dyn_name, dyn_type.empty() ? "fbo" : dyn_type, true});
                        }
                    }
                }
            }

            graph.nodes.push_back(std::move(node));
        }
    }

    if (t.contains("connections") && t["connections"].is_list()) {
        for (const auto& conn_t : t["connections"].as_list()) {
            ConnectionData conn;

            if (conn_t.contains("from_node")) {
                if (conn_t["from_node"].is_numer()) {
                    conn.from_node_id = std::to_string(static_cast<int>(conn_t["from_node"].as_numer()));
                } else if (conn_t["from_node"].is_string()) {
                    conn.from_node_id = conn_t["from_node"].as_string();
                }
            }
            if (conn_t.contains("from_socket") && conn_t["from_socket"].is_string()) {
                conn.from_socket = conn_t["from_socket"].as_string();
            }
            if (conn_t.contains("to_node")) {
                if (conn_t["to_node"].is_numer()) {
                    conn.to_node_id = std::to_string(static_cast<int>(conn_t["to_node"].as_numer()));
                } else if (conn_t["to_node"].is_string()) {
                    conn.to_node_id = conn_t["to_node"].as_string();
                }
            }
            if (conn_t.contains("to_socket") && conn_t["to_socket"].is_string()) {
                conn.to_socket = conn_t["to_socket"].as_string();
            }

            graph.connections.push_back(std::move(conn));
        }
    }

    for (const auto& conn : graph.connections) {
        NodeData* from_node = graph.get_node(conn.from_node_id);
        if (from_node && is_pass_node_type(from_node->node_type) && !conn.from_socket.empty()) {
            if (!has_socket(from_node->outputs, conn.from_socket)) {
                tc::Log::warn(
                    "GraphData: pass '%s' has no registered output socket '%s'; keeping socket from connection",
                    from_node->pass_class.c_str(),
                    conn.from_socket.c_str()
                );
                from_node->outputs.push_back({conn.from_socket, "fbo", false});
            }
        }

        NodeData* to_node = graph.get_node(conn.to_node_id);
        if (to_node && is_pass_node_type(to_node->node_type) && !conn.to_socket.empty()) {
            if (!has_socket(to_node->inputs, conn.to_socket)) {
                if (!graph_data_is_target_socket_name(conn.to_socket)) {
                    tc::Log::warn(
                        "GraphData: pass '%s' has no registered input socket '%s'; keeping socket from connection",
                        to_node->pass_class.c_str(),
                        conn.to_socket.c_str()
                    );
                }
                std::string socket_type = target_socket_type_for(*to_node, conn.to_socket);
                if (graph_data_is_target_socket_name(conn.to_socket)) {
                    NodeData* from_node = graph.get_node(conn.from_node_id);
                    if (from_node) {
                        for (const auto& output : from_node->outputs) {
                            if (output.name == conn.from_socket) {
                                socket_type = output.socket_type;
                                break;
                            }
                        }
                    }
                }
                to_node->inputs.push_back({
                    conn.to_socket,
                    socket_type,
                    true
                });
            }
        }
    }

    if (t.contains("viewport_frames") && t["viewport_frames"].is_list()) {
        for (const auto& vf_t : t["viewport_frames"].as_list()) {
            ViewportFrameData vf;
            if (vf_t.contains("viewport_name") && vf_t["viewport_name"].is_string()) {
                vf.viewport_name = vf_t["viewport_name"].as_string();
            }
            if (vf_t.contains("x") && vf_t["x"].is_numer()) {
                vf.x = static_cast<float>(vf_t["x"].as_numer());
            }
            if (vf_t.contains("y") && vf_t["y"].is_numer()) {
                vf.y = static_cast<float>(vf_t["y"].as_numer());
            }
            if (vf_t.contains("width") && vf_t["width"].is_numer()) {
                vf.width = static_cast<float>(vf_t["width"].as_numer());
            }
            if (vf_t.contains("height") && vf_t["height"].is_numer()) {
                vf.height = static_cast<float>(vf_t["height"].as_numer());
            }
            graph.viewport_frames.push_back(std::move(vf));
        }
    }

    return graph;
}

} // namespace tc
