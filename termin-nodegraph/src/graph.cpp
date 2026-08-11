#include <termin/nodegraph/graph.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <tcbase/tc_log.h>

namespace termin::nodegraph {
    namespace {

        std::atomic_uint64_t next_graph_id{1};

        std::uint64_t allocate_graph_id() {
            for (;;) {
                const std::uint64_t candidate = next_graph_id.fetch_add(1, std::memory_order_relaxed);
                if (candidate != 0)
                    return candidate;
            }
        }

        template <typename T, typename HandleType>
        class Pool {
        public:
            explicit Pool(std::uint64_t graph_id)
                : graph_id_(graph_id) {}

            HandleType insert(T value) {
                std::uint32_t index = 0;
                if (free_.empty()) {
                    index = static_cast<std::uint32_t>(slots_.size());
                    slots_.push_back({});
                } else {
                    index = free_.back();
                    free_.pop_back();
                }
                Slot& slot = slots_[index];
                slot.value = std::move(value);
                HandleType handle{graph_id_, index, slot.generation};
                slot.value->handle = handle;
                return handle;
            }

            bool erase(HandleType handle) {
                Slot* slot = resolve_slot(handle);
                if (slot == nullptr)
                    return false;
                slot->value.reset();
                ++slot->generation;
                if (slot->generation == 0)
                    slot->generation = 1;
                free_.push_back(handle.index);
                return true;
            }

            T* resolve(HandleType handle) {
                Slot* slot = resolve_slot(handle);
                return slot == nullptr ? nullptr : &*slot->value;
            }

            const T* resolve(HandleType handle) const {
                const Slot* slot = resolve_slot(handle);
                return slot == nullptr ? nullptr : &*slot->value;
            }

            std::vector<T> snapshots() const {
                std::vector<T> result;
                result.reserve(slots_.size() - free_.size());
                for (const Slot& slot : slots_) {
                    if (slot.value)
                        result.push_back(*slot.value);
                }
                return result;
            }

        private:
            struct Slot {
                std::optional<T> value;
                std::uint32_t generation = 1;
            };

            Slot* resolve_slot(HandleType handle) {
                if (!handle.valid() || handle.graph_id != graph_id_ || handle.index >= slots_.size())
                    return nullptr;
                Slot& slot = slots_[handle.index];
                return slot.value && slot.generation == handle.generation ? &slot : nullptr;
            }

            const Slot* resolve_slot(HandleType handle) const {
                if (!handle.valid() || handle.graph_id != graph_id_ || handle.index >= slots_.size())
                    return nullptr;
                const Slot& slot = slots_[handle.index];
                return slot.value && slot.generation == handle.generation ? &slot : nullptr;
            }

            std::uint64_t graph_id_;
            std::vector<Slot> slots_;
            std::vector<std::uint32_t> free_;
        };

        template <typename T>
        Result<T> failure(ErrorCode code, const std::string& message) {
            tc_log_error("NodeGraph: %s", message.c_str());
            return Result<T>::failure(code, message);
        }

        Result<void> failure_void(ErrorCode code, const std::string& message) {
            tc_log_error("NodeGraph: %s", message.c_str());
            return Result<void>::failure(code, message);
        }

        bool sockets_valid(const std::vector<Socket>& sockets) {
            std::unordered_set<std::string> names;
            for (const Socket& socket : sockets) {
                if (socket.name.empty() || !names.insert(socket.name).second)
                    return false;
            }
            return true;
        }

        bool value_is_valid(const tc_value& value) {
            switch (value.type) {
            case TC_VALUE_NIL:
            case TC_VALUE_BOOL:
            case TC_VALUE_INT:
            case TC_VALUE_FLOAT:
            case TC_VALUE_DOUBLE:
                return true;
            case TC_VALUE_STRING:
                return value.data.s != nullptr;
            case TC_VALUE_LIST:
                if (value.data.list.count > 0 && value.data.list.items == nullptr)
                    return false;
                for (std::size_t index = 0; index < value.data.list.count; ++index) {
                    if (!value_is_valid(value.data.list.items[index]))
                        return false;
                }
                return true;
            case TC_VALUE_DICT:
                if (value.data.dict.count > 0 && value.data.dict.entries == nullptr)
                    return false;
                for (std::size_t index = 0; index < value.data.dict.count; ++index) {
                    const tc_value_dict_entry& entry = value.data.dict.entries[index];
                    if (entry.key == nullptr || entry.value == nullptr || !value_is_valid(*entry.value))
                        return false;
                }
                return true;
            }
            return false;
        }

        bool dict_is_valid(const tc_value& value) {
            return value.type == TC_VALUE_DICT && value_is_valid(value);
        }

        const Socket* find_socket(const std::vector<Socket>& sockets, const std::string& name) {
            const auto found = std::find_if(sockets.begin(), sockets.end(), [&](const Socket& socket) {
                return socket.name == name;
            });
            return found == sockets.end() ? nullptr : &*found;
        }

    } // namespace

    struct Graph::Impl {
        explicit Impl(std::shared_ptr<const ConnectionValidator> requested_validator)
            : graph_id(allocate_graph_id()),
              nodes(graph_id),
              edges(graph_id),
              groups(graph_id),
              validator(requested_validator ? std::move(requested_validator)
                                            : std::make_shared<DefaultConnectionValidator>()) {
        }

        std::string next_id(const char* prefix, std::uint64_t& counter, const auto& index) {
            for (;;) {
                const std::string candidate = std::string(prefix) + "_" + std::to_string(++counter);
                if (!index.contains(candidate))
                    return candidate;
            }
        }

        void touch() {
            ++revision;
            if (revision == 0)
                revision = 1;
        }

        bool erase_edge(EdgeHandle handle) {
            const Edge* value = edges.resolve(handle);
            if (value == nullptr)
                return false;
            edge_ids.erase(value->id);
            return edges.erase(handle);
        }

        std::uint64_t graph_id;
        std::uint64_t revision = 0;
        Pool<Node, NodeHandle> nodes;
        Pool<Edge, EdgeHandle> edges;
        Pool<Group, GroupHandle> groups;
        std::unordered_map<std::string, NodeHandle> node_ids;
        std::unordered_map<std::string, EdgeHandle> edge_ids;
        std::unordered_map<std::string, GroupHandle> group_ids;
        std::shared_ptr<const ConnectionValidator> validator;
        tc::trent data = tc::trent::dict();
        std::uint64_t node_counter = 0;
        std::uint64_t edge_counter = 0;
        std::uint64_t group_counter = 0;
    };

    bool DefaultConnectionValidator::accepts(const ConnectionProposal& proposal) const {
        return proposal.source_type == "any" || proposal.destination_type == "any" ||
               proposal.source_type == proposal.destination_type;
    }

    Graph::Graph(std::shared_ptr<const ConnectionValidator> validator)
        : impl_(std::make_unique<Impl>(std::move(validator))) {}

    Graph::~Graph() = default;
    Graph::Graph(Graph&&) noexcept = default;
    Graph& Graph::operator=(Graph&&) noexcept = default;

    std::uint64_t Graph::id() const {
        return impl_->graph_id;
    }

    std::uint64_t Graph::revision() const {
        return impl_->revision;
    }

    Result<NodeHandle> Graph::create_node(NodeDescriptor descriptor) {
        if (descriptor.id.empty())
            descriptor.id = impl_->next_id("node", impl_->node_counter, impl_->node_ids);
        if (impl_->node_ids.contains(descriptor.id))
            return failure<NodeHandle>(ErrorCode::DuplicateId, "duplicate node id: " + descriptor.id);
        if (!sockets_valid(descriptor.inputs) || !sockets_valid(descriptor.outputs))
            return failure<NodeHandle>(ErrorCode::DuplicateSocket,
                                       "node has an empty or duplicate socket: " + descriptor.id);
        if (!dict_is_valid(descriptor.params.get()) || !dict_is_valid(descriptor.data.get()))
            return failure<NodeHandle>(ErrorCode::InvalidValue,
                                       "node params and data must be valid dictionaries: " + descriptor.id);
        if (descriptor.title.empty())
            descriptor.title = descriptor.kind;
        Node node;
        static_cast<NodeDescriptor&>(node) = std::move(descriptor);
        const NodeHandle handle = impl_->nodes.insert(std::move(node));
        impl_->node_ids.emplace(impl_->nodes.resolve(handle)->id, handle);
        impl_->touch();
        return Result<NodeHandle>::success(handle);
    }

    Result<void> Graph::remove_node(NodeHandle handle) {
        const Node* value = impl_->nodes.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        std::vector<EdgeHandle> attached;
        for (const Edge& edge : impl_->edges.snapshots()) {
            if (edge.source_node == handle || edge.destination_node == handle)
                attached.push_back(edge.handle);
        }
        for (EdgeHandle edge : attached)
            impl_->erase_edge(edge);
        impl_->node_ids.erase(value->id);
        impl_->nodes.erase(handle);
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::move_node(NodeHandle handle, float x, float y) {
        Node* value = impl_->nodes.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        value->x = x;
        value->y = y;
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::add_input(NodeHandle handle, Socket socket) {
        Node* value = impl_->nodes.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        if (socket.name.empty())
            return failure_void(ErrorCode::InvalidId, "input socket name is empty");
        if (find_socket(value->inputs, socket.name) != nullptr)
            return failure_void(ErrorCode::DuplicateSocket, "duplicate input socket: " + socket.name);
        value->inputs.push_back(std::move(socket));
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::add_output(NodeHandle handle, Socket socket) {
        Node* value = impl_->nodes.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        if (socket.name.empty())
            return failure_void(ErrorCode::InvalidId, "output socket name is empty");
        if (find_socket(value->outputs, socket.name) != nullptr)
            return failure_void(ErrorCode::DuplicateSocket, "duplicate output socket: " + socket.name);
        value->outputs.push_back(std::move(socket));
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::set_node_param(NodeHandle handle, std::string name, const tc_value& value) {
        Node* node = impl_->nodes.resolve(handle);
        if (node == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        if (name.empty())
            return failure_void(ErrorCode::InvalidId, "node parameter name is empty");
        if (!value_is_valid(value))
            return failure_void(ErrorCode::InvalidValue, "node parameter value is malformed");
        node->params[name] = tc::trent::copy_of(value);
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::set_node_data(NodeHandle handle, const tc_value& value) {
        Node* node = impl_->nodes.resolve(handle);
        if (node == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid node handle");
        if (!dict_is_valid(value))
            return failure_void(ErrorCode::InvalidValue, "node data must be a valid dictionary");
        node->data = tc::trent::copy_of(value);
        impl_->touch();
        return Result<void>::success();
    }

    Result<ConnectOutcome> Graph::connect(ConnectRequest request) {
        const Node* source = impl_->nodes.resolve(request.source_node);
        const Node* destination = impl_->nodes.resolve(request.destination_node);
        if (source == nullptr || destination == nullptr)
            return failure<ConnectOutcome>(ErrorCode::NodeNotFound, "connection node not found");
        if (request.source_node == request.destination_node)
            return failure<ConnectOutcome>(ErrorCode::SelfLink, "self-link rejected");
        const Socket* source_socket = find_socket(source->outputs, request.source_socket);
        const Socket* destination_socket = find_socket(destination->inputs, request.destination_socket);
        if (source_socket == nullptr || destination_socket == nullptr)
            return failure<ConnectOutcome>(ErrorCode::SocketNotFound, "connection socket not found");
        const ConnectionProposal proposal{
            source->id,
            request.source_socket,
            source_socket->type,
            destination->id,
            request.destination_socket,
            destination_socket->type,
        };
        if (!impl_->validator->accepts(proposal))
            return failure<ConnectOutcome>(ErrorCode::TypeMismatch, "connection type mismatch");

        std::vector<EdgeHandle> replaced;
        for (const Edge& edge : impl_->edges.snapshots()) {
            const bool replaces_source = !source_socket->multi && edge.source_node == request.source_node &&
                                         edge.source_socket == request.source_socket;
            const bool replaces_destination = !destination_socket->multi &&
                                              edge.destination_node == request.destination_node &&
                                              edge.destination_socket == request.destination_socket;
            if (replaces_source || replaces_destination)
                replaced.push_back(edge.handle);
        }
        if (request.edge_id.empty())
            request.edge_id = impl_->next_id("edge", impl_->edge_counter, impl_->edge_ids);
        if (impl_->edge_ids.contains(request.edge_id))
            return failure<ConnectOutcome>(ErrorCode::DuplicateId, "duplicate edge id: " + request.edge_id);

        Edge edge;
        edge.id = std::move(request.edge_id);
        edge.source_node = request.source_node;
        edge.source_socket = std::move(request.source_socket);
        edge.destination_node = request.destination_node;
        edge.destination_socket = std::move(request.destination_socket);

        for (EdgeHandle handle : replaced)
            impl_->erase_edge(handle);
        const EdgeHandle handle = impl_->edges.insert(std::move(edge));
        impl_->edge_ids.emplace(impl_->edges.resolve(handle)->id, handle);
        impl_->touch();
        return Result<ConnectOutcome>::success({handle, std::move(replaced)});
    }

    Result<void> Graph::remove_edge(EdgeHandle handle) {
        if (!impl_->erase_edge(handle))
            return failure_void(ErrorCode::InvalidHandle, "invalid edge handle");
        impl_->touch();
        return Result<void>::success();
    }

    Result<GroupHandle> Graph::create_group(GroupDescriptor descriptor) {
        if (descriptor.id.empty())
            descriptor.id = impl_->next_id("group", impl_->group_counter, impl_->group_ids);
        if (impl_->group_ids.contains(descriptor.id))
            return failure<GroupHandle>(ErrorCode::DuplicateId, "duplicate group id: " + descriptor.id);
        if (!dict_is_valid(descriptor.data.get()))
            return failure<GroupHandle>(ErrorCode::InvalidValue,
                                        "group data must be a valid dictionary: " + descriptor.id);
        Group group;
        static_cast<GroupDescriptor&>(group) = std::move(descriptor);
        const GroupHandle handle = impl_->groups.insert(std::move(group));
        impl_->group_ids.emplace(impl_->groups.resolve(handle)->id, handle);
        impl_->touch();
        return Result<GroupHandle>::success(handle);
    }

    Result<void> Graph::remove_group(GroupHandle handle) {
        const Group* value = impl_->groups.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid group handle");
        impl_->group_ids.erase(value->id);
        impl_->groups.erase(handle);
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::move_group(GroupHandle handle, float x, float y) {
        Group* value = impl_->groups.resolve(handle);
        if (value == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid group handle");
        value->x = x;
        value->y = y;
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::set_group_data(GroupHandle handle, const tc_value& value) {
        Group* group = impl_->groups.resolve(handle);
        if (group == nullptr)
            return failure_void(ErrorCode::InvalidHandle, "invalid group handle");
        if (!dict_is_valid(value))
            return failure_void(ErrorCode::InvalidValue, "group data must be a valid dictionary");
        group->data = tc::trent::copy_of(value);
        impl_->touch();
        return Result<void>::success();
    }

    Result<void> Graph::set_data(const tc_value& value) {
        if (!dict_is_valid(value))
            return failure_void(ErrorCode::InvalidValue, "graph data must be a valid dictionary");
        impl_->data = tc::trent::copy_of(value);
        impl_->touch();
        return Result<void>::success();
    }

    tc::trent Graph::data() const {
        return impl_->data;
    }

    std::optional<NodeHandle> Graph::find_node(const std::string& id) const {
        const auto found = impl_->node_ids.find(id);
        return found == impl_->node_ids.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<EdgeHandle> Graph::find_edge(const std::string& id) const {
        const auto found = impl_->edge_ids.find(id);
        return found == impl_->edge_ids.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<GroupHandle> Graph::find_group(const std::string& id) const {
        const auto found = impl_->group_ids.find(id);
        return found == impl_->group_ids.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<Node> Graph::node(NodeHandle handle) const {
        const Node* value = impl_->nodes.resolve(handle);
        return value == nullptr ? std::nullopt : std::optional(*value);
    }

    std::optional<Edge> Graph::edge(EdgeHandle handle) const {
        const Edge* value = impl_->edges.resolve(handle);
        return value == nullptr ? std::nullopt : std::optional(*value);
    }

    std::optional<Group> Graph::group(GroupHandle handle) const {
        const Group* value = impl_->groups.resolve(handle);
        return value == nullptr ? std::nullopt : std::optional(*value);
    }

    std::vector<Node> Graph::nodes() const {
        return impl_->nodes.snapshots();
    }

    std::vector<Edge> Graph::edges() const {
        return impl_->edges.snapshots();
    }

    std::vector<Group> Graph::groups() const {
        return impl_->groups.snapshots();
    }

    std::shared_ptr<const ConnectionValidator> Graph::connection_validator() const {
        return impl_->validator;
    }

} // namespace termin::nodegraph
