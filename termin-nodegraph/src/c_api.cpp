#include <termin/nodegraph/c_api.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <tcbase/tc_log.h>
#include <termin/nodegraph/graph.hpp>

namespace ng = termin::nodegraph;

namespace {

    class CallbackValidator final : public ng::ConnectionValidator {
    public:
        CallbackValidator(tc_nodegraph_connection_validator callback,
                          void* userdata,
                          tc_nodegraph_userdata_deleter deleter)
            : callback_(callback), userdata_(userdata), deleter_(deleter) {}

        ~CallbackValidator() override {
            if (deleter_ != nullptr)
                deleter_(userdata_);
        }

        bool accepts(const ng::ConnectionProposal& proposal) const override {
            const tc_nodegraph_connection_proposal encoded{
                sizeof(tc_nodegraph_connection_proposal),
                proposal.source_node_id.c_str(),
                proposal.source_socket.c_str(),
                proposal.source_type.c_str(),
                proposal.destination_node_id.c_str(),
                proposal.destination_socket.c_str(),
                proposal.destination_type.c_str(),
            };
            return callback_(userdata_, &encoded);
        }

    private:
        tc_nodegraph_connection_validator callback_;
        void* userdata_;
        tc_nodegraph_userdata_deleter deleter_;
    };

    struct GraphEntry {
        explicit GraphEntry(std::shared_ptr<const ng::ConnectionValidator> validator = {})
            : graph(std::move(validator)) {}

        ng::Graph graph;
        std::string last_error;
    };

    struct GraphSlot {
        std::unique_ptr<GraphEntry> entry;
        std::uint32_t generation = 1;
    };

    std::mutex graph_mutex;
    std::vector<GraphSlot> graph_slots;
    std::vector<std::uint32_t> free_graph_slots;

    GraphEntry* resolve(tc_nodegraph_handle handle) {
        if (tc_nodegraph_handle_is_invalid(handle) || handle.index >= graph_slots.size())
            return nullptr;
        GraphSlot& slot = graph_slots[handle.index];
        return slot.entry && slot.generation == handle.generation ? slot.entry.get() : nullptr;
    }

    template <typename Result, typename Function>
    Result access_graph(tc_nodegraph_handle handle, Result fallback, Function&& function) {
        std::lock_guard lock(graph_mutex);
        GraphEntry* entry = resolve(handle);
        if (entry == nullptr) {
            tc_log_error("NodeGraph C API: invalid graph handle");
            return fallback;
        }
        try {
            return function(*entry);
        } catch (const std::exception& error) {
            entry->last_error = std::string("internal exception: ") + error.what();
            tc_log_error("NodeGraph C API: %s", entry->last_error.c_str());
            return fallback;
        } catch (...) {
            entry->last_error = "unknown internal exception";
            tc_log_error("NodeGraph C API: %s", entry->last_error.c_str());
            return fallback;
        }
    }

    tc_nodegraph_result map_error(ng::ErrorCode error) {
        switch (error) {
        case ng::ErrorCode::None:
            return TC_NODEGRAPH_OK;
        case ng::ErrorCode::InvalidHandle:
            return TC_NODEGRAPH_INVALID_HANDLE;
        case ng::ErrorCode::DuplicateId:
            return TC_NODEGRAPH_DUPLICATE_ID;
        case ng::ErrorCode::InvalidId:
            return TC_NODEGRAPH_INVALID_ID;
        case ng::ErrorCode::NodeNotFound:
            return TC_NODEGRAPH_NODE_NOT_FOUND;
        case ng::ErrorCode::EdgeNotFound:
            return TC_NODEGRAPH_EDGE_NOT_FOUND;
        case ng::ErrorCode::GroupNotFound:
            return TC_NODEGRAPH_GROUP_NOT_FOUND;
        case ng::ErrorCode::SocketNotFound:
            return TC_NODEGRAPH_SOCKET_NOT_FOUND;
        case ng::ErrorCode::DuplicateSocket:
            return TC_NODEGRAPH_DUPLICATE_SOCKET;
        case ng::ErrorCode::SelfLink:
            return TC_NODEGRAPH_SELF_LINK;
        case ng::ErrorCode::TypeMismatch:
            return TC_NODEGRAPH_TYPE_MISMATCH;
        case ng::ErrorCode::CardinalityViolation:
            return TC_NODEGRAPH_CARDINALITY_VIOLATION;
        case ng::ErrorCode::InvalidValue:
            return TC_NODEGRAPH_INVALID_VALUE;
        }
        return TC_NODEGRAPH_INTERNAL_ERROR;
    }

    template <typename T>
    tc_nodegraph_result finish(GraphEntry& entry, const ng::Result<T>& result) {
        if (result) {
            entry.last_error.clear();
            return TC_NODEGRAPH_OK;
        }
        entry.last_error = result.message;
        return map_error(result.error);
    }

    tc_nodegraph_result argument_error(GraphEntry& entry, const char* message) {
        entry.last_error = message;
        tc_log_error("NodeGraph C API: %s", message);
        return TC_NODEGRAPH_INVALID_ARGUMENT;
    }

    ng::NodeHandle from_c(tc_nodegraph_node_handle handle) {
        return {handle.graph_id, handle.index, handle.generation};
    }

    ng::EdgeHandle from_c_edge(tc_nodegraph_edge_handle handle) {
        return {handle.graph_id, handle.index, handle.generation};
    }

    ng::GroupHandle from_c_group(tc_nodegraph_group_handle handle) {
        return {handle.graph_id, handle.index, handle.generation};
    }

    template <typename Handle>
    tc_nodegraph_entity_handle to_c(Handle handle) {
        return {handle.graph_id, handle.index, handle.generation};
    }

    ng::Socket socket_from_c(const tc_nodegraph_socket_desc& socket) {
        return {socket.name ? socket.name : "", socket.socket_type ? socket.socket_type : "any", socket.multi};
    }

    bool socket_descriptor_valid(const tc_nodegraph_socket_desc& socket) {
        return socket.struct_size >= sizeof(tc_nodegraph_socket_desc);
    }

    tc::trent socket_to_value(const ng::Socket& socket, bool is_input) {
        tc::trent value = tc::trent::dict();
        value["name"] = socket.name;
        value["socket_type"] = socket.type;
        value["is_input"] = is_input;
        value["multi"] = socket.multi;
        return value;
    }

    tc::trent node_to_value(const ng::Node& node) {
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
        for (const ng::Socket& socket : node.inputs)
            value["inputs"].push_back(socket_to_value(socket, true));
        value["outputs"] = tc::trent::list();
        for (const ng::Socket& socket : node.outputs)
            value["outputs"].push_back(socket_to_value(socket, false));
        return value;
    }

    tc::trent edge_to_value(const ng::Edge& edge, const ng::Graph& graph) {
        tc::trent value = tc::trent::dict();
        value["id"] = edge.id;
        const auto source = graph.node(edge.source_node);
        const auto destination = graph.node(edge.destination_node);
        value["src_node_id"] = source ? source->id : std::string();
        value["src_socket"] = edge.source_socket;
        value["dst_node_id"] = destination ? destination->id : std::string();
        value["dst_socket"] = edge.destination_socket;
        return value;
    }

    tc::trent group_to_value(const ng::Group& group) {
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

    size_t copy_string(const std::string& value, char* buffer, size_t capacity) {
        const size_t required = value.size() + 1;
        if (buffer != nullptr && capacity > 0) {
            const size_t copied = std::min(value.size(), capacity - 1);
            std::memcpy(buffer, value.data(), copied);
            buffer[copied] = '\0';
        }
        return required;
    }

    template <typename SourceHandle, typename DestinationHandle>
    size_t copy_handles(const std::vector<SourceHandle>& source, DestinationHandle* out, size_t capacity) {
        if (out != nullptr) {
            const size_t copied = std::min(source.size(), capacity);
            for (size_t index = 0; index < copied; ++index)
                out[index] = to_c(source[index]);
        }
        return source.size();
    }

} // namespace

extern "C" {

    tc_nodegraph_handle tc_nodegraph_create(void) {
        return tc_nodegraph_create_with_validator(nullptr, nullptr, nullptr);
    }

    tc_nodegraph_handle tc_nodegraph_create_with_validator(tc_nodegraph_connection_validator validator,
                                                           void* userdata,
                                                           tc_nodegraph_userdata_deleter destroy_userdata) {
        std::lock_guard lock(graph_mutex);
        if (validator == nullptr && destroy_userdata != nullptr) {
            tc_log_error("NodeGraph C API: userdata deleter requires a validator callback");
            destroy_userdata(userdata);
            return tc_nodegraph_handle_invalid();
        }
        try {
            std::uint32_t index;
            if (free_graph_slots.empty()) {
                index = static_cast<std::uint32_t>(graph_slots.size());
                graph_slots.push_back({});
            } else {
                index = free_graph_slots.back();
                free_graph_slots.pop_back();
            }
            GraphSlot& slot = graph_slots[index];
            std::shared_ptr<const ng::ConnectionValidator> policy;
            if (validator != nullptr)
                policy = std::make_shared<CallbackValidator>(validator, userdata, destroy_userdata);
            slot.entry = std::make_unique<GraphEntry>(std::move(policy));
            return {index, slot.generation};
        } catch (const std::exception& error) {
            tc_log_error("NodeGraph C API: graph creation failed: %s", error.what());
            return tc_nodegraph_handle_invalid();
        }
    }

    void tc_nodegraph_destroy(tc_nodegraph_handle graph) {
        std::lock_guard lock(graph_mutex);
        GraphEntry* entry = resolve(graph);
        if (entry == nullptr) {
            tc_log_error("NodeGraph C API: cannot destroy invalid graph handle");
            return;
        }
        GraphSlot& slot = graph_slots[graph.index];
        slot.entry.reset();
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1;
        free_graph_slots.push_back(graph.index);
    }

    bool tc_nodegraph_is_valid(tc_nodegraph_handle graph) {
        std::lock_guard lock(graph_mutex);
        return resolve(graph) != nullptr;
    }

    uint64_t tc_nodegraph_id(tc_nodegraph_handle graph) {
        return access_graph(graph, std::uint64_t{0}, [](GraphEntry& entry) { return entry.graph.id(); });
    }

    uint64_t tc_nodegraph_revision(tc_nodegraph_handle graph) {
        return access_graph(graph, std::uint64_t{0}, [](GraphEntry& entry) { return entry.graph.revision(); });
    }

    size_t tc_nodegraph_copy_last_error(tc_nodegraph_handle graph, char* buffer, size_t capacity) {
        return access_graph(graph, size_t{0}, [&](GraphEntry& entry) {
            return copy_string(entry.last_error, buffer, capacity);
        });
    }

    size_t tc_nodegraph_node_count(tc_nodegraph_handle graph) {
        return access_graph(graph, size_t{0}, [](GraphEntry& entry) { return entry.graph.nodes().size(); });
    }

    size_t tc_nodegraph_edge_count(tc_nodegraph_handle graph) {
        return access_graph(graph, size_t{0}, [](GraphEntry& entry) { return entry.graph.edges().size(); });
    }

    size_t tc_nodegraph_group_count(tc_nodegraph_handle graph) {
        return access_graph(graph, size_t{0}, [](GraphEntry& entry) { return entry.graph.groups().size(); });
    }

    size_t tc_nodegraph_copy_nodes(tc_nodegraph_handle graph,
                                   tc_nodegraph_node_handle* out_handles,
                                   size_t capacity) {
        return access_graph(graph, size_t{0}, [&](GraphEntry& entry) {
            std::vector<ng::NodeHandle> handles;
            for (const ng::Node& node : entry.graph.nodes())
                handles.push_back(node.handle);
            return copy_handles(handles, out_handles, capacity);
        });
    }

    size_t tc_nodegraph_copy_edges(tc_nodegraph_handle graph,
                                   tc_nodegraph_edge_handle* out_handles,
                                   size_t capacity) {
        return access_graph(graph, size_t{0}, [&](GraphEntry& entry) {
            std::vector<ng::EdgeHandle> handles;
            for (const ng::Edge& edge : entry.graph.edges())
                handles.push_back(edge.handle);
            return copy_handles(handles, out_handles, capacity);
        });
    }

    size_t tc_nodegraph_copy_groups(tc_nodegraph_handle graph,
                                    tc_nodegraph_group_handle* out_handles,
                                    size_t capacity) {
        return access_graph(graph, size_t{0}, [&](GraphEntry& entry) {
            std::vector<ng::GroupHandle> handles;
            for (const ng::Group& group : entry.graph.groups())
                handles.push_back(group.handle);
            return copy_handles(handles, out_handles, capacity);
        });
    }

    bool tc_nodegraph_find_node(tc_nodegraph_handle graph, const char* id, tc_nodegraph_node_handle* out_node) {
        return access_graph(graph, false, [&](GraphEntry& entry) {
            if (id == nullptr || out_node == nullptr)
                return false;
            const auto handle = entry.graph.find_node(id);
            if (!handle)
                return false;
            *out_node = to_c(*handle);
            return true;
        });
    }

    bool tc_nodegraph_find_edge(tc_nodegraph_handle graph, const char* id, tc_nodegraph_edge_handle* out_edge) {
        return access_graph(graph, false, [&](GraphEntry& entry) {
            if (id == nullptr || out_edge == nullptr)
                return false;
            const auto handle = entry.graph.find_edge(id);
            if (!handle)
                return false;
            *out_edge = to_c(*handle);
            return true;
        });
    }

    bool tc_nodegraph_find_group(tc_nodegraph_handle graph, const char* id, tc_nodegraph_group_handle* out_group) {
        return access_graph(graph, false, [&](GraphEntry& entry) {
            if (id == nullptr || out_group == nullptr)
                return false;
            const auto handle = entry.graph.find_group(id);
            if (!handle)
                return false;
            *out_group = to_c(*handle);
            return true;
        });
    }

    tc_nodegraph_result tc_nodegraph_create_node(tc_nodegraph_handle graph,
                                                 const tc_nodegraph_node_desc* descriptor,
                                                 tc_nodegraph_node_handle* out_node) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (descriptor == nullptr || out_node == nullptr ||
                descriptor->struct_size < sizeof(tc_nodegraph_node_desc) ||
                (descriptor->input_count > 0 && descriptor->inputs == nullptr) ||
                (descriptor->output_count > 0 && descriptor->outputs == nullptr))
                return argument_error(entry, "invalid node descriptor or output pointer");
            ng::NodeDescriptor value;
            value.id = descriptor->id ? descriptor->id : "";
            value.kind = descriptor->kind ? descriptor->kind : "";
            value.title = descriptor->title ? descriptor->title : "";
            value.x = descriptor->x;
            value.y = descriptor->y;
            value.width = descriptor->width;
            value.height = descriptor->height;
            for (size_t index = 0; index < descriptor->input_count; ++index) {
                if (!socket_descriptor_valid(descriptor->inputs[index]))
                    return argument_error(entry, "input socket descriptor has an incompatible size");
                value.inputs.push_back(socket_from_c(descriptor->inputs[index]));
            }
            for (size_t index = 0; index < descriptor->output_count; ++index) {
                if (!socket_descriptor_valid(descriptor->outputs[index]))
                    return argument_error(entry, "output socket descriptor has an incompatible size");
                value.outputs.push_back(socket_from_c(descriptor->outputs[index]));
            }
            if (descriptor->params != nullptr)
                value.params = tc::trent::copy_of(descriptor->params);
            if (descriptor->data != nullptr)
                value.data = tc::trent::copy_of(descriptor->data);
            const auto created = entry.graph.create_node(std::move(value));
            const tc_nodegraph_result status = finish(entry, created);
            if (created)
                *out_node = to_c(created.value);
            return status;
        });
    }

    tc_nodegraph_result tc_nodegraph_remove_node(tc_nodegraph_handle graph, tc_nodegraph_node_handle node) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            const auto result = entry.graph.remove_node(from_c(node));
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_move_node(tc_nodegraph_handle graph,
                                               tc_nodegraph_node_handle node,
                                               float x,
                                               float y) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            const auto result = entry.graph.move_node(from_c(node), x, y);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_add_input(tc_nodegraph_handle graph,
                                               tc_nodegraph_node_handle node,
                                               const tc_nodegraph_socket_desc* socket) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (socket == nullptr || !socket_descriptor_valid(*socket))
                return argument_error(entry, "input socket descriptor is invalid");
            const auto result = entry.graph.add_input(from_c(node), socket_from_c(*socket));
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_add_output(tc_nodegraph_handle graph,
                                                tc_nodegraph_node_handle node,
                                                const tc_nodegraph_socket_desc* socket) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (socket == nullptr || !socket_descriptor_valid(*socket))
                return argument_error(entry, "output socket descriptor is invalid");
            const auto result = entry.graph.add_output(from_c(node), socket_from_c(*socket));
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_set_node_param(tc_nodegraph_handle graph,
                                                    tc_nodegraph_node_handle node,
                                                    const char* name,
                                                    const tc_value* value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (name == nullptr || value == nullptr)
                return argument_error(entry, "node parameter name and value are required");
            const auto result = entry.graph.set_node_param(from_c(node), name, *value);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_set_node_data(tc_nodegraph_handle graph,
                                                   tc_nodegraph_node_handle node,
                                                   const tc_value* value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (value == nullptr)
                return argument_error(entry, "node data is required");
            const auto result = entry.graph.set_node_data(from_c(node), *value);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_connect(tc_nodegraph_handle graph,
                                             tc_nodegraph_node_handle source_node,
                                             const char* source_socket,
                                             tc_nodegraph_node_handle destination_node,
                                             const char* destination_socket,
                                             const char* edge_id,
                                             tc_nodegraph_edge_handle* out_edge) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (source_socket == nullptr || destination_socket == nullptr || out_edge == nullptr)
                return argument_error(entry, "connection sockets and output edge are required");
            auto result = entry.graph.connect({from_c(source_node),
                                               source_socket,
                                               from_c(destination_node),
                                               destination_socket,
                                               edge_id ? edge_id : ""});
            const tc_nodegraph_result status = finish(entry, result);
            if (result)
                *out_edge = to_c(result.value.edge);
            return status;
        });
    }

    tc_nodegraph_result tc_nodegraph_remove_edge(tc_nodegraph_handle graph, tc_nodegraph_edge_handle edge) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            const auto result = entry.graph.remove_edge(from_c_edge(edge));
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_create_group(tc_nodegraph_handle graph,
                                                  const tc_nodegraph_group_desc* descriptor,
                                                  tc_nodegraph_group_handle* out_group) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (descriptor == nullptr || out_group == nullptr ||
                descriptor->struct_size < sizeof(tc_nodegraph_group_desc))
                return argument_error(entry, "invalid group descriptor or output pointer");
            ng::GroupDescriptor value;
            value.id = descriptor->id ? descriptor->id : "";
            value.title = descriptor->title ? descriptor->title : "";
            value.x = descriptor->x;
            value.y = descriptor->y;
            value.width = descriptor->width;
            value.height = descriptor->height;
            if (descriptor->data != nullptr)
                value.data = tc::trent::copy_of(descriptor->data);
            const auto created = entry.graph.create_group(std::move(value));
            const tc_nodegraph_result status = finish(entry, created);
            if (created)
                *out_group = to_c(created.value);
            return status;
        });
    }

    tc_nodegraph_result tc_nodegraph_remove_group(tc_nodegraph_handle graph, tc_nodegraph_group_handle group) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            const auto result = entry.graph.remove_group(from_c_group(group));
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_move_group(tc_nodegraph_handle graph,
                                                tc_nodegraph_group_handle group,
                                                float x,
                                                float y) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            const auto result = entry.graph.move_group(from_c_group(group), x, y);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_set_group_data(tc_nodegraph_handle graph,
                                                    tc_nodegraph_group_handle group,
                                                    const tc_value* value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (value == nullptr)
                return argument_error(entry, "group data is required");
            const auto result = entry.graph.set_group_data(from_c_group(group), *value);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_set_data(tc_nodegraph_handle graph, const tc_value* value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (value == nullptr)
                return argument_error(entry, "graph data is required");
            const auto result = entry.graph.set_data(*value);
            return finish(entry, result);
        });
    }

    tc_nodegraph_result tc_nodegraph_copy_node_value(tc_nodegraph_handle graph,
                                                     tc_nodegraph_node_handle node,
                                                     tc_value* out_value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (out_value == nullptr)
                return argument_error(entry, "node snapshot output is null");
            const auto snapshot = entry.graph.node(from_c(node));
            if (!snapshot) {
                entry.last_error = "invalid node handle";
                return TC_NODEGRAPH_INVALID_HANDLE;
            }
            *out_value = node_to_value(*snapshot).release();
            entry.last_error.clear();
            return TC_NODEGRAPH_OK;
        });
    }

    tc_nodegraph_result tc_nodegraph_copy_edge_value(tc_nodegraph_handle graph,
                                                     tc_nodegraph_edge_handle edge,
                                                     tc_value* out_value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (out_value == nullptr)
                return argument_error(entry, "edge snapshot output is null");
            const auto snapshot = entry.graph.edge(from_c_edge(edge));
            if (!snapshot) {
                entry.last_error = "invalid edge handle";
                return TC_NODEGRAPH_INVALID_HANDLE;
            }
            *out_value = edge_to_value(*snapshot, entry.graph).release();
            entry.last_error.clear();
            return TC_NODEGRAPH_OK;
        });
    }

    tc_nodegraph_result tc_nodegraph_copy_group_value(tc_nodegraph_handle graph,
                                                      tc_nodegraph_group_handle group,
                                                      tc_value* out_value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (out_value == nullptr)
                return argument_error(entry, "group snapshot output is null");
            const auto snapshot = entry.graph.group(from_c_group(group));
            if (!snapshot) {
                entry.last_error = "invalid group handle";
                return TC_NODEGRAPH_INVALID_HANDLE;
            }
            *out_value = group_to_value(*snapshot).release();
            entry.last_error.clear();
            return TC_NODEGRAPH_OK;
        });
    }

    tc_nodegraph_result tc_nodegraph_serialize(tc_nodegraph_handle graph, tc_value* out_value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (out_value == nullptr)
                return argument_error(entry, "serialization output is null");
            *out_value = entry.graph.to_value().release();
            entry.last_error.clear();
            return TC_NODEGRAPH_OK;
        });
    }

    tc_nodegraph_result tc_nodegraph_replace(tc_nodegraph_handle graph, const tc_value* value) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (value == nullptr)
                return argument_error(entry, "serialized graph value is null");
            const auto result = entry.graph.replace_from_value(*value);
            return finish(entry, result);
        });
    }

    size_t tc_nodegraph_copy_json(tc_nodegraph_handle graph, int indent, char* buffer, size_t capacity) {
        return access_graph(graph, size_t{0}, [&](GraphEntry& entry) {
            const std::string json = entry.graph.to_json(indent);
            entry.last_error.clear();
            return copy_string(json, buffer, capacity);
        });
    }

    tc_nodegraph_result tc_nodegraph_replace_json(tc_nodegraph_handle graph, const char* json) {
        return access_graph(graph, TC_NODEGRAPH_INVALID_HANDLE, [&](GraphEntry& entry) {
            if (json == nullptr)
                return argument_error(entry, "serialized graph JSON is null");
            const auto result = entry.graph.replace_from_json(json);
            return finish(entry, result);
        });
    }

} // extern "C"
