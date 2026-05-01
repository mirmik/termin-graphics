#include "termin/render/graph_compiler.hpp"
#include <tcbase/tc_log.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/tc_pass.hpp>
#include "termin/render/render_pipeline.hpp"
#include <termin/render/resource_spec.hpp>
#include <trent/json.h>
#include <deque>
#include <unordered_set>

extern "C" {
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

// ============================================================================
// Helper: check if node is a pass (pass, effect, or any non-resource type)
// ============================================================================

static bool is_pass_node(const NodeData& node) {
    // "resource" and "output" are not passes, everything else is
    return node.node_type != "resource" && node.node_type != "output";
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

    // Pass 1: Assign names to FBO resource nodes
    for (const auto& node : graph.nodes) {
        if (node.node_type == "resource") {
            std::string resource_type = "fbo";
            if (node.params.contains("resource_type") && node.params["resource_type"].is_string()) {
                resource_type = node.params["resource_type"].as_string();
            }
            if (resource_type == "fbo") {
                std::string name = node.name.empty()
                    ? "fbo_" + std::to_string(node_index[node.id])
                    : node.name;
                for (const auto& output : node.outputs) {
                    result.socket_names[node.id][output.name] = name;
                    result.resource_types[name] = output.socket_type;
                }
            }
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
    for (const auto& conn : graph.connections) {
        if (conn.to_socket.size() > 7 &&
            conn.to_socket.substr(conn.to_socket.size() - 7) == "_target") {
            auto from_it = result.socket_names.find(conn.from_node_id);
            if (from_it != result.socket_names.end()) {
                auto socket_it = from_it->second.find(conn.from_socket);
                if (socket_it != from_it->second.end()) {
                    result.socket_names[conn.to_node_id][conn.to_socket] = socket_it->second;
                }
            }
        }
    }

    // Pass 3b: Apply _target overrides to output socket names
    // If X_target input is connected, X output should use that resource name
    for (const auto& node : graph.nodes) {
        for (const auto& inp : node.inputs) {
            if (inp.name.size() > 7 && inp.name.substr(inp.name.size() - 7) == "_target") {
                std::string base_name = inp.name.substr(0, inp.name.size() - 7);
                auto& sockets = result.socket_names[node.id];
                auto target_it = sockets.find(inp.name);
                if (target_it != sockets.end()) {
                    const std::string& target_res = target_it->second;
                    // Override the output socket name
                    sockets[base_name] = target_res;
                    // Also register the resource type
                    result.resource_types[target_res] = "fbo";
                }
            }
        }
    }

    // Pass 3c: Propagate all other connections (now output names are correct)
    for (const auto& conn : graph.connections) {
        // Skip _target connections (already handled)
        if (conn.to_socket.size() > 7 &&
            conn.to_socket.substr(conn.to_socket.size() - 7) == "_target") {
            continue;
        }
        auto from_it = result.socket_names.find(conn.from_node_id);
        if (from_it != result.socket_names.end()) {
            auto socket_it = from_it->second.find(conn.from_socket);
            if (socket_it != from_it->second.end()) {
                result.socket_names[conn.to_node_id][conn.to_socket] = socket_it->second;
            }
        }
    }

    // Pass 4: Default names for unconnected inputs
    for (const auto& node : graph.nodes) {
        int idx = node_index[node.id];
        for (const auto& inp : node.inputs) {
            auto& sockets = result.socket_names[node.id];
            if (sockets.find(inp.name) == sockets.end()) {
                std::string node_name = node.name.empty() ? node.pass_class : node.name;
                std::string name = "empty_" + node_name + "_" + std::to_string(idx) + "_" + inp.name;
                sockets[inp.name] = name;
                result.resource_types[name] = inp.socket_type;
            }
        }
    }

    return result;
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

    tc_value tc_val = trent_to_tc_value(value);
    bool result = pass_ref.set_field(field_name, tc_val);
    tc_value_free(&tc_val);
    return result;
}

// ============================================================================
// Collect FBO nodes
// ============================================================================

static std::unordered_map<std::string, const NodeData*> collect_fbo_nodes(const GraphData& graph) {
    std::unordered_map<std::string, const NodeData*> result;

    for (const auto& node : graph.nodes) {
        if (node.node_type == "resource") {
            std::string resource_type = "fbo";
            if (node.params.contains("resource_type") && node.params["resource_type"].is_string()) {
                resource_type = node.params["resource_type"].as_string();
            }
            if (resource_type == "fbo") {
                std::string name = node.name.empty() ? node.id : node.name;
                result[name] = &node;
            }
        }
    }

    return result;
}

// ============================================================================
// Infer ResourceSpec from FBO node
// ============================================================================

static ResourceSpec infer_resource_spec(
    const std::string& resource_name,
    const std::unordered_map<std::string, const NodeData*>& fbo_nodes
) {
    ResourceSpec spec;
    spec.resource = resource_name;

    auto it = fbo_nodes.find(resource_name);
    if (it != fbo_nodes.end()) {
        const NodeData* node = it->second;
        const auto& params = node->params;

        // Format
        if (params.contains("format") && params["format"].is_string()) {
            spec.format = params["format"].as_string();
        }

        // Samples
        if (params.contains("samples") && params["samples"].is_numer()) {
            spec.samples = static_cast<int>(params["samples"].as_numer());
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
        if (params.contains("clear_color") && params["clear_color"].is_numer() &&
            params["clear_color"].as_numer() != 0.0) {
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
        if (params.contains("clear_depth") && params["clear_depth"].is_numer() &&
            params["clear_depth"].as_numer() != 0.0) {
            double depth_value = 1.0;
            if (params.contains("clear_depth_value") && params["clear_depth_value"].is_numer()) {
                depth_value = params["clear_depth_value"].as_numer();
            }
            spec.clear_depth = static_cast<float>(depth_value);
        }

        return spec;
    }

    // No FBO node found - return empty spec (no automatic heuristics)
    return spec;
}

// ============================================================================
// Main Compilation Function
// ============================================================================

RenderPipeline* compile_graph(GraphData& graph) {
    // 1. Topological sort
    auto sorted_nodes = topological_sort(graph);

    // 2. Assign resource names
    auto naming = assign_resource_names(graph);

    // 3. Build viewport map
    auto viewport_map = build_node_viewport_map(graph.nodes, graph.viewport_frames);

    // 4. Collect FBO nodes for ResourceSpec inference
    auto fbo_nodes = collect_fbo_nodes(graph);

    // 5. Create pipeline
    auto* pipeline = new RenderPipeline("compiled_graph");

    // 6. Add passes
    for (const auto* node : sorted_nodes) {
        if (!is_pass_node(*node)) continue;

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
            tc_pass_drop(pass_ptr);
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
            // Skip _target sockets
            if (socket_name.size() > 7 &&
                socket_name.substr(socket_name.size() - 7) == "_target") {
                continue;
            }
            nos::trent res_name_trent(resource_name);
            set_pass_property(pass_ref, socket_name, res_name_trent);
        }

        // Set additional params from node
        if (node->params.is_dict()) {
            for (const auto& [key, value] : node->params.as_dict()) {
                set_pass_property(pass_ref, key, value);
            }
        }

        pipeline->add_pass(pass_ptr);
    }

    // 7. Add ResourceSpecs (only for FBO resources, not shadow maps)
    std::unordered_set<std::string> seen_resources;
    for (const auto* node : sorted_nodes) {
        if (!is_pass_node(*node)) continue;

        for (const auto& inp : node->inputs) {
            auto socket_it = naming.socket_names[node->id].find(inp.name);
            if (socket_it == naming.socket_names[node->id].end()) continue;

            const std::string& res_name = socket_it->second;
            if (seen_resources.count(res_name)) continue;
            seen_resources.insert(res_name);

            // Skip non-FBO resources (shadow maps are managed by ShadowPass)
            auto type_it = naming.resource_types.find(res_name);
            if (type_it != naming.resource_types.end() && type_it->second != "fbo") {
                continue;
            }

            auto spec = infer_resource_spec(res_name, fbo_nodes);

            if (!spec.resource.empty()) {
                pipeline->add_spec(spec);
            }
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
