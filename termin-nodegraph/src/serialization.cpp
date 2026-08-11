#include <termin/nodegraph/graph.hpp>

#include <cmath>
#include <string>

#include <tcbase/tc_log.h>
#include <tcbase/tc_trent_json.hpp>

namespace termin::nodegraph {
    namespace {

        Result<void> invalid_document(const std::string& message) {
            tc_log_error("NodeGraph: rejected invalid serialized graph: %s", message.c_str());
            return Result<void>::failure(ErrorCode::InvalidValue, message);
        }

        bool finite_number(tc::trent_view value) {
            return value.is_numer() && std::isfinite(value.as_numer());
        }

        bool required_string(tc::trent_view object, const char* key, std::string& out, bool non_empty = true) {
            const tc::trent_view value = object.get(key);
            if (!value.is_string())
                return false;
            out = value.as_string();
            return !non_empty || !out.empty();
        }

        bool optional_string(tc::trent_view object, const char* key, std::string& out, const std::string& fallback) {
            const tc::trent_view value = object.get(key);
            if (!value) {
                out = fallback;
                return true;
            }
            if (!value.is_string())
                return false;
            out = value.as_string();
            return true;
        }

        bool optional_number(tc::trent_view object, const char* key, float& out, float fallback) {
            const tc::trent_view value = object.get(key);
            if (!value) {
                out = fallback;
                return true;
            }
            if (!finite_number(value))
                return false;
            out = static_cast<float>(value.as_numer());
            return std::isfinite(out);
        }

        bool optional_dict(tc::trent_view object, const char* key, tc::trent& out) {
            const tc::trent_view value = object.get(key);
            if (!value) {
                out = tc::trent::dict();
                return true;
            }
            if (!value.is_dict())
                return false;
            out = tc::trent::copy_of(*value.raw());
            return true;
        }

        tc::trent socket_to_value(const Socket& socket, bool is_input) {
            tc::trent value = tc::trent::dict();
            value["name"] = socket.name;
            value["socket_type"] = socket.type;
            value["is_input"] = is_input;
            value["multi"] = socket.multi;
            return value;
        }

        tc::trent node_to_value(const Node& node) {
            tc::trent value = tc::trent::dict();
            value["id"] = node.id;
            value["kind"] = node.kind;
            value["title"] = node.title;
            value["x"] = node.x;
            value["y"] = node.y;
            value["width"] = node.width;
            value["height"] = node.height;
            value["params"] = node.params;
            value["data"] = node.data;
            value["inputs"] = tc::trent::list();
            for (const Socket& socket : node.inputs)
                value["inputs"].push_back(socket_to_value(socket, true));
            value["outputs"] = tc::trent::list();
            for (const Socket& socket : node.outputs)
                value["outputs"].push_back(socket_to_value(socket, false));
            return value;
        }

        tc::trent edge_to_value(const Edge& edge, const Graph& graph) {
            tc::trent value = tc::trent::dict();
            value["id"] = edge.id;
            const std::optional<Node> source = graph.node(edge.source_node);
            const std::optional<Node> destination = graph.node(edge.destination_node);
            value["src_node_id"] = source ? source->id : std::string();
            value["src_socket"] = edge.source_socket;
            value["dst_node_id"] = destination ? destination->id : std::string();
            value["dst_socket"] = edge.destination_socket;
            return value;
        }

        tc::trent group_to_value(const Group& group) {
            tc::trent value = tc::trent::dict();
            value["id"] = group.id;
            value["title"] = group.title;
            value["x"] = group.x;
            value["y"] = group.y;
            value["width"] = group.width;
            value["height"] = group.height;
            value["data"] = group.data;
            return value;
        }

        Result<void> parse_sockets(tc::trent_view value,
                                   bool is_input,
                                   const std::string& node_id,
                                   std::vector<Socket>& out) {
            if (!value.is_list())
                return invalid_document("node '" + node_id + "' sockets must be a list");
            for (tc::trent_view encoded : value.as_list()) {
                if (!encoded.is_dict())
                    return invalid_document("node '" + node_id + "' socket must be an object");
                Socket socket;
                if (!required_string(encoded, "name", socket.name))
                    return invalid_document("node '" + node_id + "' socket name must be a non-empty string");
                if (!optional_string(encoded, "socket_type", socket.type, "any") || socket.type.empty())
                    return invalid_document("node '" + node_id + "' socket type must be a non-empty string");
                const tc::trent_view multi = encoded.get("multi");
                if (multi && !multi.is_bool())
                    return invalid_document("node '" + node_id + "' socket multi must be boolean");
                socket.multi = multi ? multi.as_bool() : !is_input;
                const tc::trent_view encoded_direction = encoded.get("is_input");
                if (encoded_direction && (!encoded_direction.is_bool() || encoded_direction.as_bool() != is_input))
                    return invalid_document("node '" + node_id + "' socket direction disagrees with its collection");
                out.push_back(std::move(socket));
            }
            return Result<void>::success();
        }

        Result<void> parse_nodes(Graph& graph, tc::trent_view nodes) {
            if (!nodes.is_list())
                return invalid_document("nodes must be a list");
            for (tc::trent_view encoded : nodes.as_list()) {
                if (!encoded.is_dict())
                    return invalid_document("node must be an object");
                NodeDescriptor descriptor;
                if (!required_string(encoded, "id", descriptor.id))
                    return invalid_document("node id must be a non-empty string");
                if (!optional_string(encoded, "kind", descriptor.kind, "") ||
                    !optional_string(encoded, "title", descriptor.title, descriptor.kind))
                    return invalid_document("node '" + descriptor.id + "' kind and title must be strings");
                if (!optional_number(encoded, "x", descriptor.x, 0.0f) ||
                    !optional_number(encoded, "y", descriptor.y, 0.0f) ||
                    !optional_number(encoded, "width", descriptor.width, 190.0f) ||
                    !optional_number(encoded, "height", descriptor.height, 120.0f))
                    return invalid_document("node '" + descriptor.id + "' geometry must contain finite numbers");
                if (!optional_dict(encoded, "params", descriptor.params) ||
                    !optional_dict(encoded, "data", descriptor.data))
                    return invalid_document("node '" + descriptor.id + "' params and data must be objects");
                const tc::trent_view inputs = encoded.get("inputs");
                const tc::trent_view outputs = encoded.get("outputs");
                Result<void> parsed_inputs = parse_sockets(inputs ? inputs : tc::trent::list().view(),
                                                           true,
                                                           descriptor.id,
                                                           descriptor.inputs);
                if (!parsed_inputs)
                    return parsed_inputs;
                Result<void> parsed_outputs = parse_sockets(outputs ? outputs : tc::trent::list().view(),
                                                            false,
                                                            descriptor.id,
                                                            descriptor.outputs);
                if (!parsed_outputs)
                    return parsed_outputs;
                const auto created = graph.create_node(std::move(descriptor));
                if (!created)
                    return invalid_document(created.message);
            }
            return Result<void>::success();
        }

        Result<void> parse_edges(Graph& graph, tc::trent_view edges) {
            if (!edges.is_list())
                return invalid_document("edges must be a list");
            for (tc::trent_view encoded : edges.as_list()) {
                if (!encoded.is_dict())
                    return invalid_document("edge must be an object");
                std::string edge_id;
                std::string source_id;
                std::string source_socket;
                std::string destination_id;
                std::string destination_socket;
                if (!required_string(encoded, "id", edge_id) ||
                    !required_string(encoded, "src_node_id", source_id) ||
                    !required_string(encoded, "src_socket", source_socket) ||
                    !required_string(encoded, "dst_node_id", destination_id) ||
                    !required_string(encoded, "dst_socket", destination_socket))
                    return invalid_document("edge identity and endpoints must be non-empty strings");
                const auto source = graph.find_node(source_id);
                const auto destination = graph.find_node(destination_id);
                if (!source || !destination)
                    return invalid_document("edge '" + edge_id + "' references a missing node");
                auto connected = graph.connect({*source,
                                                std::move(source_socket),
                                                *destination,
                                                std::move(destination_socket),
                                                std::move(edge_id)});
                if (!connected)
                    return invalid_document(connected.message);
                if (!connected.value.replaced_edges.empty())
                    return invalid_document("serialized edges violate socket cardinality");
            }
            return Result<void>::success();
        }

        Result<void> parse_groups(Graph& graph, tc::trent_view groups) {
            if (!groups.is_list())
                return invalid_document("groups must be a list");
            for (tc::trent_view encoded : groups.as_list()) {
                if (!encoded.is_dict())
                    return invalid_document("group must be an object");
                GroupDescriptor descriptor;
                if (!required_string(encoded, "id", descriptor.id))
                    return invalid_document("group id must be a non-empty string");
                if (!optional_string(encoded, "title", descriptor.title, "") ||
                    !optional_number(encoded, "x", descriptor.x, 0.0f) ||
                    !optional_number(encoded, "y", descriptor.y, 0.0f) ||
                    !optional_number(encoded, "width", descriptor.width, 0.0f) ||
                    !optional_number(encoded, "height", descriptor.height, 0.0f))
                    return invalid_document("group '" + descriptor.id + "' has invalid title or geometry");
                if (!optional_dict(encoded, "data", descriptor.data))
                    return invalid_document("group '" + descriptor.id + "' data must be an object");
                const auto created = graph.create_group(std::move(descriptor));
                if (!created)
                    return invalid_document(created.message);
            }
            return Result<void>::success();
        }

    } // namespace

    tc::trent Graph::to_value() const {
        tc::trent value = tc::trent::dict();
        value["nodes"] = tc::trent::list();
        for (const Node& node : nodes())
            value["nodes"].push_back(node_to_value(node));
        value["edges"] = tc::trent::list();
        for (const Edge& edge : edges())
            value["edges"].push_back(edge_to_value(edge, *this));
        value["groups"] = tc::trent::list();
        for (const Group& group : groups())
            value["groups"].push_back(group_to_value(group));
        value["data"] = data();
        return value;
    }

    Result<void> Graph::replace_from_value(const tc_value& value) {
        const tc::trent_view root(value);
        if (!root.is_dict())
            return invalid_document("graph root must be an object");
        const tc::trent_view nodes_value = root.get("nodes");
        const tc::trent_view edges_value = root.get("edges");
        const tc::trent_view groups_value = root.get("groups");
        if (!nodes_value || !edges_value || !groups_value)
            return invalid_document("graph root must contain nodes, edges and groups");

        Graph replacement(connection_validator());
        tc::trent graph_data;
        if (!optional_dict(root, "data", graph_data))
            return invalid_document("graph data must be an object");
        Result<void> result = replacement.set_data(graph_data.get());
        if (!result)
            return result;
        result = parse_nodes(replacement, nodes_value);
        if (!result)
            return result;
        result = parse_edges(replacement, edges_value);
        if (!result)
            return result;
        result = parse_groups(replacement, groups_value);
        if (!result)
            return result;
        *this = std::move(replacement);
        return Result<void>::success();
    }

    std::string Graph::to_json(int indent) const {
        return tc::json::dump(to_value(), indent);
    }

    Result<void> Graph::replace_from_json(const std::string& json) {
        try {
            tc::trent value = tc::json::parse(json);
            return replace_from_value(value.get());
        } catch (const std::exception& error) {
            return invalid_document(std::string("invalid JSON: ") + error.what());
        }
    }

} // namespace termin::nodegraph
