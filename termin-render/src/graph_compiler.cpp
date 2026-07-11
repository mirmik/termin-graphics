#include "termin/render/graph_compiler.hpp"
#include <tcbase/tc_log.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/graph_alias_pass.hpp>
#include <termin/render/graph_node_def.hpp>
#include <termin/render/tc_pass.hpp>
#include "termin/render/render_pipeline.hpp"
#include <termin/render/resource_spec.hpp>
#include <trent/json.h>
#include <deque>
#include <optional>
#include <cstdlib>
#include <unordered_set>

extern "C" {
#include "inspect/tc_inspect_pass_adapter.h"
#include "render/tc_pass.h"
#include "tc_value.h"
}

#include "tc_inspect_cpp.hpp"

using termin::CxxFramePass;
using termin::TcPassRef;
using termin::RenderPipeline;
using termin::ResourceSpec;
using termin::TextureFilter;

namespace tc {

static std::optional<int> trent_int_value(const nos::trent& value) {
    if (value.is_numer()) {
        return static_cast<int>(value.as_numer());
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    const std::string str = value.as_string();
    if (str.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    long parsed = std::strtol(str.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

static std::optional<bool> trent_bool_value(const nos::trent& value) {
    if (value.is_bool()) {
        return value.as_bool();
    }
    if (value.is_numer()) {
        return value.as_numer() != 0.0;
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    const std::string str = value.as_string();
    if (str == "true" || str == "True" || str == "1") {
        return true;
    }
    if (str == "false" || str == "False" || str == "0" || str.empty()) {
        return false;
    }
    return std::nullopt;
}

static bool set_pass_property(
    TcPassRef& pass_ref,
    const std::string& field_name,
    const nos::trent& value
);

// ============================================================================
// Helper: check if node is a pass (pass, effect, or any non-resource type)
// ============================================================================

static bool is_pass_node(const NodeData& node) {
    const termin::GraphNodeDef* def = termin::graph_node_def_find(node.node_type);
    if (def) {
        return def->executable;
    }
    return true;
}

static bool is_external_resource_name(const std::string& name) {
    return name == "RT_COLOR" || name == "RT_DEPTH" ||
           name == "OUTPUT" || name == "DISPLAY";
}

static bool is_attachment_texture_type(const std::string& type) {
    return type == "color_texture" || type == "depth_texture";
}

static bool is_direct_target_resource_type(const std::string& type) {
    return type == "shadow" || type == "shadow_map_array";
}

static bool is_graph_alias_node(const NodeData& node) {
    return node.node_type == "render_target_input" ||
           node.node_type == "fbo_split" ||
           node.node_type == "fbo_join" ||
           node.node_type == "pipeline_output";
}

static bool graph_compiler_is_target_socket_name(const std::string& name) {
    return name.size() > 7 && name.substr(name.size() - 7) == "_target";
}

static std::string graph_compiler_resource_type_for_node(const NodeData& node) {
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

static std::string default_resource_name(
    const NodeData& node,
    int node_index,
    const std::string& resource_type
) {
    if (!node.name.empty()) {
        return node.name;
    }
    if (resource_type == "color_texture") {
        return "color_texture_" + std::to_string(node_index);
    }
    if (resource_type == "depth_texture") {
        return "depth_texture_" + std::to_string(node_index);
    }
    if (resource_type == "shadow_map_array") {
        return "shadow_maps_" + std::to_string(node_index);
    }
    return "fbo_" + std::to_string(node_index);
}

static std::string generated_input_name(
    const NodeData& node,
    int node_index,
    const std::string& socket_name
) {
    std::string node_name = node.name.empty() ? node.pass_class : node.name;
    if (node_name.empty()) {
        node_name = node.node_type;
    }
    return "empty_" + node_name + "_" + std::to_string(node_index) + "_" + socket_name;
}

static void propagate_non_target_connections(
    const GraphData& graph,
    ResourceNaming& result
) {
    for (const auto& conn : graph.connections) {
        if (graph_compiler_is_target_socket_name(conn.to_socket)) {
            continue;
        }
        auto from_it = result.socket_names.find(conn.from_node_id);
        if (from_it == result.socket_names.end()) {
            continue;
        }
        auto socket_it = from_it->second.find(conn.from_socket);
        if (socket_it == from_it->second.end()) {
            continue;
        }
        result.socket_names[conn.to_node_id][conn.to_socket] = socket_it->second;
    }
}

static void propagate_target_connections(
    const GraphData& graph,
    ResourceNaming& result
) {
    for (const auto& conn : graph.connections) {
        if (!graph_compiler_is_target_socket_name(conn.to_socket)) {
            continue;
        }
        auto from_it = result.socket_names.find(conn.from_node_id);
        if (from_it == result.socket_names.end()) {
            continue;
        }
        auto socket_it = from_it->second.find(conn.from_socket);
        if (socket_it == from_it->second.end()) {
            continue;
        }
        result.socket_names[conn.to_node_id][conn.to_socket] = socket_it->second;
    }
}

static std::optional<std::string> socket_type_for(
    const NodeData& node,
    const std::string& socket_name,
    bool input
) {
    const std::vector<SocketData>& sockets = input ? node.inputs : node.outputs;
    for (const auto& socket : sockets) {
        if (socket.name == socket_name) {
            return socket.socket_type;
        }
    }
    return std::nullopt;
}

static void apply_target_overrides(
    const GraphData& graph,
    ResourceNaming& result
) {
    for (const auto& node : graph.nodes) {
        for (const auto& inp : node.inputs) {
            if (!graph_compiler_is_target_socket_name(inp.name)) {
                continue;
            }
            std::string base_name = inp.name.substr(0, inp.name.size() - 7);
            auto& sockets = result.socket_names[node.id];
            auto target_it = sockets.find(inp.name);
            if (target_it == sockets.end()) {
                continue;
            }

            const std::string& target_res = target_it->second;

            auto output_it = sockets.find(base_name);
            if (output_it == sockets.end() || output_it->second.empty()) {
                std::string output_res = node.pass_class.empty()
                    ? node.id + "_" + base_name
                    : node.pass_class + "_" + node.id + "_" + base_name;
                sockets[base_name] = output_res;
                output_it = sockets.find(base_name);
            }

            const std::string& output_res = output_it->second;
            auto output_type = socket_type_for(node, base_name, false);
            std::string resolved_output_type = output_type.has_value() ? *output_type : inp.socket_type;
            if (resolved_output_type == "fbo" && inp.socket_type != "fbo") {
                resolved_output_type = inp.socket_type;
            }
            result.resource_types[output_res] = resolved_output_type;

            if (output_res != target_res && is_direct_target_resource_type(resolved_output_type)) {
                sockets[base_name] = target_res;
                result.resource_types[target_res] = resolved_output_type;
                result.resource_types.erase(output_res);
                continue;
            }

            auto target_view_it = result.resource_views.find(target_res);
            if (target_view_it != result.resource_views.end()) {
                if (output_res != target_res) {
                    result.resource_views[output_res] = target_view_it->second;
                }
                continue;
            }

            if (is_external_resource_name(target_res)) {
                if (result.resource_types[output_res] == "fbo" ||
                    result.resource_types[output_res] == "external_color") {
                    result.fbo_compositions[output_res] =
                        termin::FboComposition{target_res, target_res};
                } else if (result.resource_types[output_res] == "depth_texture") {
                    result.resource_views[output_res] = termin::ResourceView{
                        target_res,
                        termin::AttachmentKind::Depth,
                    };
                } else {
                    result.resource_views[output_res] = termin::ResourceView{
                        target_res,
                        termin::AttachmentKind::Color,
                    };
                }
                continue;
            }

            if (output_res != target_res) {
                if (result.resource_types[output_res] == "depth_texture") {
                    result.resource_views[output_res] = termin::ResourceView{
                        target_res,
                        termin::AttachmentKind::Depth,
                    };
                } else if (result.resource_types[output_res] == "color_texture") {
                    result.resource_views[output_res] = termin::ResourceView{
                        target_res,
                        termin::AttachmentKind::Color,
                    };
                } else if (!is_attachment_texture_type(result.resource_types[output_res])) {
                    result.fbo_compositions[output_res] =
                        termin::FboComposition{target_res, target_res};
                }
            }
        }
    }
}

static void validate_connection_types(const GraphData& graph) {
    for (const auto& conn : graph.connections) {
        const NodeData* from_node = graph.get_node(conn.from_node_id);
        const NodeData* to_node = graph.get_node(conn.to_node_id);
        if (!from_node || !to_node) {
            throw GraphCompileError("Connection references missing node");
        }

        auto from_type = socket_type_for(*from_node, conn.from_socket, false);
        auto to_type = socket_type_for(*to_node, conn.to_socket, true);
        if (!from_type || !to_type) {
            throw GraphCompileError(
                "Connection references missing socket: " +
                conn.from_node_id + "." + conn.from_socket + " -> " +
                conn.to_node_id + "." + conn.to_socket);
        }

        if (*from_type != *to_type) {
            throw GraphCompileError(
                "Invalid resource connection type: " +
                conn.from_node_id + "." + conn.from_socket + " (" + *from_type + ") -> " +
                conn.to_node_id + "." + conn.to_socket + " (" + *to_type + ")");
        }
    }
}

// ============================================================================
// Topological Sort (Kahn's algorithm)
// ============================================================================

std::vector<NodeData*> topological_sort(GraphData& graph) {
    // Build adjacency: node_id -> list of dependent node_ids
    std::unordered_map<std::string, std::vector<std::string>> dependents;
    std::unordered_map<std::string, int> in_degree;

    for (auto& node : graph.nodes) {
        dependents[node.id] = {};
        in_degree[node.id] = 0;
    }

    for (const auto& conn : graph.connections) {
        if (dependents.count(conn.from_node_id) && in_degree.count(conn.to_node_id)) {
            dependents[conn.from_node_id].push_back(conn.to_node_id);
            in_degree[conn.to_node_id]++;
        }
    }

    // Kahn's algorithm
    std::deque<std::string> queue;
    for (auto& node : graph.nodes) {
        if (in_degree[node.id] == 0) {
            queue.push_back(node.id);
        }
    }

    std::vector<std::string> sorted_ids;
    while (!queue.empty()) {
        std::string node_id = queue.front();
        queue.pop_front();
        sorted_ids.push_back(node_id);

        for (const auto& dep_id : dependents[node_id]) {
            in_degree[dep_id]--;
            if (in_degree[dep_id] == 0) {
                queue.push_back(dep_id);
            }
        }
    }

    if (sorted_ids.size() != graph.nodes.size()) {
        throw GraphCompileError("Graph has cycles");
    }

    // Return nodes in sorted order
    std::unordered_map<std::string, NodeData*> node_map;
    for (auto& node : graph.nodes) {
        node_map[node.id] = &node;
    }

    std::vector<NodeData*> result;
    result.reserve(sorted_ids.size());
    for (const auto& id : sorted_ids) {
        result.push_back(node_map[id]);
    }

    return result;
}

// ============================================================================
// Assign Resource Names
// ============================================================================

ResourceNaming assign_resource_names(const GraphData& graph) {
    ResourceNaming result;

    // Initialize socket_names for all nodes
    for (const auto& node : graph.nodes) {
        result.socket_names[node.id] = {};
    }

    // Build node index map
    std::unordered_map<std::string, int> node_index;
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        node_index[graph.nodes[i].id] = static_cast<int>(i);
    }

    // Pass 1: Assign names to FBO resource nodes, external RT nodes, and
    // render-target external input nodes.
    for (const auto& node : graph.nodes) {
        if (node.node_type == "resource") {
            std::string resource_type = graph_compiler_resource_type_for_node(node);
            std::string name = default_resource_name(node, node_index[node.id], resource_type);
            for (const auto& output : node.outputs) {
                result.socket_names[node.id][output.name] = name;
                result.resource_types[name] = output.socket_type;
            }
        }
        if (node.node_type == "external_rt") {
            std::string name;
            if (node.params.contains("slot") && node.params["slot"].is_string()) {
                name = node.params["slot"].as_string();
            }
            if (name.empty()) {
                name = node.name;
            }
            if (name.empty()) {
                name = "unnamed";
                tc::Log::warn(
                    "compile_graph: External RT node '%s' has empty slot/name; using '%s'",
                    node.id.c_str(),
                    name.c_str()
                );
            }
            for (const auto& output : node.outputs) {
                result.socket_names[node.id][output.name] = name;
                result.resource_types[name] = "external_color";
                result.external_resources[name] = "external_texture";
            }
        }
        if (node.node_type == "render_target_input") {
            for (const auto& output : node.outputs) {
                if (output.name == "color") {
                    result.socket_names[node.id][output.name] = "RT_COLOR";
                    result.resource_types["RT_COLOR"] = "external_color";
                    result.external_resources["RT_COLOR"] = "render_target_color";
                }
            }
        }
        if (node.node_type == "pipeline_output") {
            result.resource_types["OUTPUT"] = "external_color";
            result.external_resources["OUTPUT"] = "render_target_color";
        }
    }

    // Pass 2: Assign names to output sockets of pass nodes
    for (const auto& node : graph.nodes) {
        if (is_pass_node(node)) {
            int idx = node_index[node.id];
            for (const auto& output : node.outputs) {
                if (result.socket_names[node.id].find(output.name) == result.socket_names[node.id].end()) {
                    std::string name = node.pass_class + "_" + std::to_string(idx) + "_" + output.name;
                    result.socket_names[node.id][output.name] = name;
                    result.resource_types[name] = output.socket_type;
                }
            }
        }
    }

    // Pass 3a: Propagate _target connections first (FBO -> pass._target)
    propagate_target_connections(graph, result);

    // Pass 3b: Apply _target overrides to output socket names
    // If X_target input is connected, X output should use that resource name
    apply_target_overrides(graph, result);

    // Pass 3c: Propagate all other connections (now output names are correct)
    propagate_non_target_connections(graph, result);

    // Pass 3d: Apply FboSplit nodes. They create attachment view resources.
    for (const auto& node : graph.nodes) {
        auto& sockets = result.socket_names[node.id];
        int idx = node_index[node.id];

        if (node.node_type == "fbo_split") {
            auto input_it = sockets.find("fbo");
            if (input_it == sockets.end()) {
                std::string name = generated_input_name(node, idx, "fbo");
                sockets["fbo"] = name;
                result.resource_types[name] = "fbo";
                input_it = sockets.find("fbo");
            }

            const std::string& parent = input_it->second;
            std::string color_name = parent + ".color";
            std::string depth_name = parent + ".depth";

            sockets["color"] = color_name;
            sockets["depth"] = depth_name;
            result.resource_types[color_name] = "color_texture";
            result.resource_types[depth_name] = "depth_texture";
            result.resource_views[color_name] = termin::ResourceView{
                parent,
                termin::AttachmentKind::Color,
            };
            result.resource_views[depth_name] = termin::ResourceView{
                parent,
                termin::AttachmentKind::Depth,
            };
        }
    }

    // Pass 3f: Propagate FboSplit outputs before FboJoin consumes them.
    propagate_non_target_connections(graph, result);
    propagate_target_connections(graph, result);
    apply_target_overrides(graph, result);
    propagate_non_target_connections(graph, result);

    // Pass 3g: Apply FboJoin nodes. They create composed FBO views.
    for (const auto& node : graph.nodes) {
        auto& sockets = result.socket_names[node.id];
        int idx = node_index[node.id];

        if (node.node_type == "fbo_join") {
            auto color_it = sockets.find("color");
            if (color_it == sockets.end()) {
                std::string name = generated_input_name(node, idx, "color");
                sockets["color"] = name;
                result.resource_types[name] = "color_texture";
                color_it = sockets.find("color");
            }

            auto depth_it = sockets.find("depth");
            if (depth_it == sockets.end()) {
                std::string name = generated_input_name(node, idx, "depth");
                sockets["depth"] = name;
                result.resource_types[name] = "depth_texture";
                depth_it = sockets.find("depth");
            }

            std::string output_name = node.name.empty()
                ? "fbo_join_" + std::to_string(idx)
                : node.name;
            sockets["fbo"] = output_name;
            result.resource_types[output_name] = "fbo";
            result.fbo_compositions[output_name] = termin::FboComposition{
                color_it->second,
                depth_it->second,
            };
        }
    }

    // Pass 3h: Propagate outputs from compile-time utility nodes.
    propagate_non_target_connections(graph, result);

    // Pass 3i: Re-propagate downstream connections after compile-time utility
    // nodes have assigned their final output names.
    propagate_non_target_connections(graph, result);

    // Pass 4: Default names for unconnected inputs
    for (const auto& node : graph.nodes) {
        int idx = node_index[node.id];
        for (const auto& inp : node.inputs) {
            auto& sockets = result.socket_names[node.id];
            if (sockets.find(inp.name) == sockets.end()) {
                if (graph_compiler_is_target_socket_name(inp.name)) {
                    continue;
                }
                std::string name = generated_input_name(node, idx, inp.name);
                sockets[inp.name] = name;
                result.resource_types[name] = inp.socket_type;
            }
        }
    }

    return result;
}

// ============================================================================
// Graph alias marker passes
// ============================================================================

static std::string graph_alias_pass_name(const NodeData& node) {
    if (!node.name.empty()) {
        return node.name;
    }
    if (!node.pass_class.empty()) {
        return node.pass_class + "#" + node.id;
    }
    if (!node.node_type.empty()) {
        return node.node_type + "#" + node.id;
    }
    return "GraphAlias#" + node.id;
}

static void push_resource_if_present(
    std::vector<std::string>& resources,
    const std::unordered_map<std::string, std::string>& sockets,
    const std::string& socket_name
) {
    auto it = sockets.find(socket_name);
    if (it == sockets.end() || it->second.empty()) {
        return;
    }
    resources.push_back(it->second);
}

static void push_alias_pair(
    std::vector<std::string>& aliases,
    const std::string& source,
    const std::string& target
) {
    if (source.empty() || target.empty() || source == target) {
        return;
    }
    aliases.push_back(source);
    aliases.push_back(target);
}

static void add_graph_alias_pass(
    RenderPipeline* pipeline,
    const NodeData& node,
    const ResourceNaming& naming
) {
    if (!pipeline) {
        return;
    }

    auto socket_map_it = naming.socket_names.find(node.id);
    if (socket_map_it == naming.socket_names.end()) {
        tc::Log::error(
            "compile_graph: alias node '%s' has no resolved sockets",
            node.id.c_str()
        );
        return;
    }

    const auto& sockets = socket_map_it->second;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    std::vector<std::string> aliases;

    if (node.node_type == "render_target_input") {
        push_resource_if_present(writes, sockets, "color");
    } else if (node.node_type == "fbo_split") {
        push_resource_if_present(reads, sockets, "fbo");
        push_resource_if_present(writes, sockets, "color");
        push_resource_if_present(writes, sockets, "depth");
        auto fbo_it = sockets.find("fbo");
        auto color_it = sockets.find("color");
        auto depth_it = sockets.find("depth");
        if (fbo_it != sockets.end()) {
            if (color_it != sockets.end()) {
                push_alias_pair(aliases, fbo_it->second, color_it->second);
            }
            if (depth_it != sockets.end()) {
                push_alias_pair(aliases, fbo_it->second, depth_it->second);
            }
        }
    } else if (node.node_type == "fbo_join") {
        push_resource_if_present(reads, sockets, "color");
        push_resource_if_present(reads, sockets, "depth");
        push_resource_if_present(writes, sockets, "fbo");
        auto fbo_it = sockets.find("fbo");
        if (fbo_it != sockets.end()) {
            std::optional<std::string> parent;
            for (const char* input_socket : {"color", "depth"}) {
                auto input_it = sockets.find(input_socket);
                if (input_it == sockets.end()) {
                    continue;
                }
                auto view_it = naming.resource_views.find(input_it->second);
                if (view_it == naming.resource_views.end()) {
                    continue;
                }
                if (!parent.has_value()) {
                    parent = view_it->second.parent;
                } else if (*parent != view_it->second.parent) {
                    parent.reset();
                    break;
                }
            }
            if (parent.has_value()) {
                push_alias_pair(aliases, *parent, fbo_it->second);
            }
        }
    } else if (node.node_type == "pipeline_output") {
        push_resource_if_present(reads, sockets, "color");
        writes.push_back("OUTPUT");
        auto color_it = sockets.find("color");
        if (color_it != sockets.end()) {
            push_alias_pair(aliases, color_it->second, "OUTPUT");
        }
    } else {
        return;
    }

    if (reads.empty() && writes.empty()) {
        tc::Log::warn(
            "compile_graph: alias node '%s' produced no read/write resources",
            node.id.c_str()
        );
        return;
    }

    auto* pass = new termin::GraphAliasPass(
        std::move(reads),
        std::move(writes),
        std::move(aliases),
        graph_alias_pass_name(node)
    );
    pipeline->add_pass(pass->tc_pass_ptr());
}

// ============================================================================
// Viewport Frame Mapping
// ============================================================================

const ViewportFrameData* find_containing_frame(
    const NodeData& node,
    const std::vector<ViewportFrameData>& frames
) {
    if (frames.empty()) {
        return nullptr;
    }

    float node_cx = node.x + 100.0f;  // Approximate node center
    float node_cy = node.y + 50.0f;

    for (const auto& frame : frames) {
        if (frame.x <= node_cx && node_cx <= frame.x + frame.width &&
            frame.y <= node_cy && node_cy <= frame.y + frame.height) {
            return &frame;
        }
    }

    return nullptr;
}

std::unordered_map<std::string, std::string> build_node_viewport_map(
    const std::vector<NodeData>& nodes,
    const std::vector<ViewportFrameData>& frames
) {
    std::unordered_map<std::string, std::string> result;

    for (const auto& node : nodes) {
        const auto* frame = find_containing_frame(node, frames);
        result[node.id] = frame ? frame->viewport_name : "";
    }

    return result;
}

// ============================================================================
// Convert trent to tc_value
// ============================================================================

static tc_value trent_to_tc_value(const nos::trent& t) {
    if (t.is_nil()) {
        return tc_value_nil();
    }
    if (t.is_bool()) {
        return tc_value_bool(t.as_bool());
    }
    if (t.is_numer()) {
        double val = t.as_numer();
        // Check if it's an integer
        if (val == static_cast<int64_t>(val)) {
            return tc_value_int(static_cast<int64_t>(val));
        }
        return tc_value_double(val);
    }
    if (t.is_string()) {
        return tc_value_string(t.as_string().c_str());
    }
    if (t.is_list()) {
        tc_value list = tc_value_list_new();
        for (const auto& item : t.as_list()) {
            tc_value v = trent_to_tc_value(item);
            tc_value_list_push(&list, v);
        }
        return list;
    }
    if (t.is_dict()) {
        tc_value dict = tc_value_dict_new();
        for (const auto& [key, val] : t.as_dict()) {
            tc_value v = trent_to_tc_value(val);
            tc_value_dict_set(&dict, key.c_str(), v);
        }
        return dict;
    }
    return tc_value_nil();
}

// ============================================================================
// Set pass property via TcPassRef
// ============================================================================

static bool set_pass_property(
    TcPassRef& pass_ref,
    const std::string& field_name,
    const nos::trent& value
) {
    if (!pass_ref.valid()) return false;

    tc::init_cpp_inspect_vtable();

    tc_value tc_val = trent_to_tc_value(value);
    bool result = pass_ref.set_field(field_name, tc_val);
    tc_value_free(&tc_val);
    if (!result) {
        tc::Log::warn(
            "compile_graph: failed to set field '%s.%s'",
            pass_ref.type_name().c_str(),
            field_name.c_str()
        );
    }
    return result;
}

static bool set_pass_resource_socket(
    TcPassRef& pass_ref,
    const std::string& socket_name,
    const std::string& resource_name
) {
    tc_pass* pass = pass_ref.ptr();
    if (pass && pass->kind == TC_NATIVE_PASS) {
        CxxFramePass* cxx_pass = CxxFramePass::from_tc(pass);
        if (cxx_pass && cxx_pass->set_graph_resource_input(socket_name, resource_name)) {
            return true;
        }
    }

    nos::trent res_name_trent(resource_name);
    tc_value tc_val = trent_to_tc_value(res_name_trent);
    bool field_set = pass_ref.set_field(socket_name, tc_val);
    tc_value_free(&tc_val);
    if (field_set) {
        return true;
    }

    return false;
}

// ============================================================================
// Collect FBO nodes
// ============================================================================

static std::unordered_map<std::string, const NodeData*> collect_resource_nodes(const GraphData& graph) {
    std::unordered_map<std::string, const NodeData*> result;
    std::unordered_map<std::string, int> node_index;
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        node_index[graph.nodes[i].id] = static_cast<int>(i);
    }

    for (const auto& node : graph.nodes) {
        if (node.node_type == "resource") {
            std::string resource_type = graph_compiler_resource_type_for_node(node);
            std::string name = default_resource_name(node, node_index[node.id], resource_type);
            result[name] = &node;
        }
    }

    return result;
}

// ============================================================================
// Infer ResourceSpec from FBO node
// ============================================================================

static ResourceSpec infer_resource_spec(
    const std::string& resource_name,
    const std::string& resource_type,
    const std::unordered_map<std::string, const NodeData*>& resource_nodes
) {
    ResourceSpec spec;
    spec.resource = resource_name;
    spec.resource_type = resource_type;
    if (resource_type == "color_texture") {
        spec.format = "rgba8";
    } else if (resource_type == "depth_texture") {
        spec.format = "depth32f";
    }

    auto it = resource_nodes.find(resource_name);
    if (it != resource_nodes.end()) {
        const NodeData* node = it->second;
        const auto& params = node->params;

        // Format
        if (params.contains("format") && params["format"].is_string()) {
            spec.format = params["format"].as_string();
        }

        // Samples
        if (params.contains("samples")) {
            if (auto samples = trent_int_value(params["samples"])) {
                spec.samples = *samples;
            } else {
                tc::Log::warn(
                    "compile_graph: resource '%s' has invalid samples value",
                    resource_name.c_str()
                );
            }
        }

        // Filter
        if (params.contains("filter") && params["filter"].is_string()) {
            std::string filter_mode = params["filter"].as_string();
            spec.filter = (filter_mode == "nearest")
                ? TextureFilter::NEAREST
                : TextureFilter::LINEAR;
        }

        // Size
        if (params.contains("size_mode") && params["size_mode"].is_string() &&
            params["size_mode"].as_string() == "fixed") {
            int width = 0, height = 0;
            if (params.contains("width") && params["width"].is_numer()) {
                width = static_cast<int>(params["width"].as_numer());
            }
            if (params.contains("height") && params["height"].is_numer()) {
                height = static_cast<int>(params["height"].as_numer());
            }
            if (width > 0 && height > 0) {
                spec.size = std::make_pair(width, height);
            }
        } else {
            // Scale
            if (params.contains("scale") && params["scale"].is_numer()) {
                float scale_value = static_cast<float>(params["scale"].as_numer());
                if (scale_value != 1.0f) {
                    spec.scale = scale_value;
                }
            }
        }

        // Clear color
        if (params.contains("clear_color") &&
            trent_bool_value(params["clear_color"]).value_or(false)) {
            double r = 0, g = 0, b = 0, a = 1;
            if (params.contains("clear_color_r") && params["clear_color_r"].is_numer()) {
                r = params["clear_color_r"].as_numer();
            }
            if (params.contains("clear_color_g") && params["clear_color_g"].is_numer()) {
                g = params["clear_color_g"].as_numer();
            }
            if (params.contains("clear_color_b") && params["clear_color_b"].is_numer()) {
                b = params["clear_color_b"].as_numer();
            }
            if (params.contains("clear_color_a") && params["clear_color_a"].is_numer()) {
                a = params["clear_color_a"].as_numer();
            }
            spec.clear_color = std::array<double, 4>{r, g, b, a};
        }

        // Clear depth
        if (params.contains("clear_depth") &&
            trent_bool_value(params["clear_depth"]).value_or(false)) {
            double depth_value = 1.0;
            if (params.contains("clear_depth_value") && params["clear_depth_value"].is_numer()) {
                depth_value = params["clear_depth_value"].as_numer();
            }
            spec.clear_depth = static_cast<float>(depth_value);
        }

        if (resource_type == "depth_texture" && (!spec.format || spec.format == "rgba8")) {
            spec.format = "depth32f";
        }

        return spec;
    }

    // No explicit resource node found; keep the type/default format inferred
    // from graph sockets so RenderEngine allocates the right resource kind.
    return spec;
}

// ============================================================================
// Main Compilation Function
// ============================================================================

RenderPipeline* compile_graph(GraphData& graph) {
    validate_connection_types(graph);

    // 1. Topological sort
    auto sorted_nodes = topological_sort(graph);

    // 2. Assign resource names
    auto naming = assign_resource_names(graph);

    // 3. Build viewport map
    auto viewport_map = build_node_viewport_map(graph.nodes, graph.viewport_frames);

    // 4. Collect explicit resource nodes for ResourceSpec inference
    auto resource_nodes = collect_resource_nodes(graph);

    // 5. Create pipeline
    auto* pipeline = new RenderPipeline("compiled_graph");
    pipeline->cache().resource_views = naming.resource_views;
    pipeline->cache().fbo_compositions = naming.fbo_compositions;

    // 6. Add passes
    bool had_pass_nodes = false;
    for (const auto* node : sorted_nodes) {
        if (!is_pass_node(*node)) {
            if (is_graph_alias_node(*node)) {
                add_graph_alias_pass(pipeline, *node, naming);
            }
            continue;
        }
        had_pass_nodes = true;

        // Check if pass type is registered
        if (!tc_pass_registry_has(node->pass_class.c_str())) {
            tc::Log::error("compile_graph: Unknown pass class '%s'", node->pass_class.c_str());
            continue;
        }

        // Create pass via registry (works for both native C++ and Python passes)
        tc_pass* pass_ptr = tc_pass_registry_create(node->pass_class.c_str());
        if (!pass_ptr) {
            tc::Log::error("compile_graph: Failed to create pass '%s'", node->pass_class.c_str());
            continue;
        }

        // Wrap in TcPassRef for all operations
        TcPassRef pass_ref(pass_ptr);

        if (!pass_ref.object_ptr()) {
            tc::Log::error("compile_graph: Failed to get object pointer for '%s'", node->pass_class.c_str());
            tc_pass_delete_unowned(pass_ptr);
            continue;
        }

        // Set pass name from node name
        if (!node->name.empty()) {
            pass_ref.set_pass_name(node->name);
        }

        // Set viewport name via TcPassRef
        const std::string& viewport_name = viewport_map[node->id];
        if (!viewport_name.empty()) {
            pass_ref.set_viewport_name(viewport_name);
        }

        // Set resource names from sockets via TcPassRef
        auto& socket_map = naming.socket_names[node->id];

        // Set socket-based properties (input_res, output_res, shadow_res, etc.)
        for (const auto& [socket_name, resource_name] : socket_map) {
            nos::trent res_name_trent(resource_name);
            if (graph_compiler_is_target_socket_name(socket_name)) {
                tc_value tc_val = trent_to_tc_value(res_name_trent);
                pass_ref.set_field(socket_name, tc_val);
                tc_value_free(&tc_val);
            } else {
                if (!set_pass_resource_socket(pass_ref, socket_name, resource_name)) {
                    tc::Log::warn(
                        "compile_graph: failed to set resource socket '%s.%s'",
                        pass_ref.type_name().c_str(),
                        socket_name.c_str()
                    );
                }
            }
        }

        // Set additional params from node
        if (node->params.is_dict()) {
            for (const auto& [key, value] : node->params.as_dict()) {
                set_pass_property(pass_ref, key, value);
            }
        }

        if (!tc_pipeline_adopt_pass(
                pipeline->handle(), pass_ptr, pass_ptr->deleter)) {
            tc::Log::error(
                "compile_graph: Failed to adopt pass '%s'",
                node->pass_class.c_str()
            );
            tc_pass_delete_unowned(pass_ptr);
        }
    }

    if (pipeline->pass_count() == 0 && had_pass_nodes) {
        tc::Log::error(
            "compile_graph: graph compiled to 0 passes from pass nodes (nodes=%zu, connections=%zu)",
            graph.nodes.size(),
            graph.connections.size()
        );
    }

    // 8. Add ResourceSpecs for every concrete resource kind. This also
    // covers generated resources for unconnected inputs.
    std::unordered_set<std::string> live_resources;
    for (const auto& [node_id, sockets] : naming.socket_names) {
        (void)node_id;
        for (const auto& [socket_name, resource_name] : sockets) {
            (void)socket_name;
            live_resources.insert(resource_name);
        }
    }

    std::unordered_set<std::string> seen_resources;
    for (const auto& res_name : live_resources) {
        if (seen_resources.count(res_name)) {
            continue;
        }
        seen_resources.insert(res_name);
        auto type_it = naming.resource_types.find(res_name);
        if (type_it == naming.resource_types.end()) {
            tc::Log::warn(
                "compile_graph: resource '%s' has no inferred type; skipping ResourceSpec",
                res_name.c_str()
            );
            continue;
        }
        const std::string& resource_type = type_it->second;
        if (resource_type == "shadow" || resource_type == "shadow_map_array") {
            continue;
        }
        if (is_external_resource_name(res_name)) {
            continue;
        }
        if (naming.resource_views.find(res_name) != naming.resource_views.end()) {
            continue;
        }
        if (naming.fbo_compositions.find(res_name) != naming.fbo_compositions.end()) {
            continue;
        }

        auto spec = infer_resource_spec(res_name, resource_type, resource_nodes);
        if (!spec.resource.empty()) {
            pipeline->add_spec(spec);
        }
    }

    return pipeline;
}

RenderPipeline* compile_graph(const nos::trent& graph_trent) {
    GraphData graph = GraphData::from_trent(graph_trent);
    return compile_graph(graph);
}

RenderPipeline* compile_graph(const std::string& json_str) {
    nos::trent data = nos::json::parse(json_str);
    return compile_graph(data);
}

} // namespace tc
