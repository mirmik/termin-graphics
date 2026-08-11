#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <termin/nodegraph/export.h>

namespace termin::nodegraph {

    template <typename Tag>
    struct Handle {
        std::uint64_t graph_id = 0;
        std::uint32_t index = UINT32_MAX;
        std::uint32_t generation = 0;

        bool valid() const {
            return graph_id != 0 && index != UINT32_MAX && generation != 0;
        }

        auto operator<=>(const Handle&) const = default;
    };

    struct NodeHandleTag;
    struct EdgeHandleTag;
    struct GroupHandleTag;
    using NodeHandle = Handle<NodeHandleTag>;
    using EdgeHandle = Handle<EdgeHandleTag>;
    using GroupHandle = Handle<GroupHandleTag>;

    enum class ErrorCode {
        None,
        InvalidHandle,
        DuplicateId,
        InvalidId,
        NodeNotFound,
        EdgeNotFound,
        GroupNotFound,
        SocketNotFound,
        DuplicateSocket,
        SelfLink,
        TypeMismatch,
        CardinalityViolation,
    };

    template <typename T>
    struct Result {
        ErrorCode error = ErrorCode::None;
        std::string message;
        T value{};

        bool ok() const {
            return error == ErrorCode::None;
        }

        explicit operator bool() const {
            return ok();
        }

        static Result success(T value) {
            return Result{ErrorCode::None, {}, std::move(value)};
        }

        static Result failure(ErrorCode error, std::string message) {
            return Result{error, std::move(message), {}};
        }
    };

    template <>
    struct Result<void> {
        ErrorCode error = ErrorCode::None;
        std::string message;

        bool ok() const {
            return error == ErrorCode::None;
        }

        explicit operator bool() const {
            return ok();
        }

        static Result success() {
            return {};
        }

        static Result failure(ErrorCode error, std::string message) {
            return Result{error, std::move(message)};
        }
    };

    struct Socket {
        std::string name;
        std::string type = "any";
        bool multi = false;
    };

    struct NodeDescriptor {
        std::string id;
        std::string kind;
        std::string title;
        float x = 0.0f;
        float y = 0.0f;
        float width = 190.0f;
        float height = 120.0f;
        std::vector<Socket> inputs;
        std::vector<Socket> outputs;
    };

    struct Node : NodeDescriptor {
        NodeHandle handle;
    };

    struct Edge {
        EdgeHandle handle;
        std::string id;
        NodeHandle source_node;
        std::string source_socket;
        NodeHandle destination_node;
        std::string destination_socket;
    };

    struct GroupDescriptor {
        std::string id;
        std::string title;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct Group : GroupDescriptor {
        GroupHandle handle;
    };

    struct ConnectionProposal {
        std::string source_node_id;
        std::string source_socket;
        std::string source_type;
        std::string destination_node_id;
        std::string destination_socket;
        std::string destination_type;
    };

    class TERMIN_NODEGRAPH_CORE_API ConnectionValidator {
    public:
        virtual ~ConnectionValidator() = default;
        virtual bool accepts(const ConnectionProposal& proposal) const = 0;
    };

    class TERMIN_NODEGRAPH_CORE_API DefaultConnectionValidator final : public ConnectionValidator {
    public:
        bool accepts(const ConnectionProposal& proposal) const override;
    };

    struct ConnectRequest {
        NodeHandle source_node;
        std::string source_socket;
        NodeHandle destination_node;
        std::string destination_socket;
        std::string edge_id;
    };

    struct ConnectOutcome {
        EdgeHandle edge;
        std::vector<EdgeHandle> replaced_edges;
    };

    class TERMIN_NODEGRAPH_CORE_API Graph {
    public:
        explicit Graph(std::shared_ptr<const ConnectionValidator> validator = {});
        ~Graph();

        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;
        Graph(Graph&&) noexcept;
        Graph& operator=(Graph&&) noexcept;

        std::uint64_t id() const;
        std::uint64_t revision() const;

        Result<NodeHandle> create_node(NodeDescriptor descriptor);
        Result<void> remove_node(NodeHandle node);
        Result<void> move_node(NodeHandle node, float x, float y);
        Result<void> add_input(NodeHandle node, Socket socket);
        Result<void> add_output(NodeHandle node, Socket socket);

        Result<ConnectOutcome> connect(ConnectRequest request);
        Result<void> remove_edge(EdgeHandle edge);

        Result<GroupHandle> create_group(GroupDescriptor descriptor);
        Result<void> remove_group(GroupHandle group);
        Result<void> move_group(GroupHandle group, float x, float y);

        std::optional<NodeHandle> find_node(const std::string& id) const;
        std::optional<EdgeHandle> find_edge(const std::string& id) const;
        std::optional<GroupHandle> find_group(const std::string& id) const;

        std::optional<Node> node(NodeHandle handle) const;
        std::optional<Edge> edge(EdgeHandle handle) const;
        std::optional<Group> group(GroupHandle handle) const;
        std::vector<Node> nodes() const;
        std::vector<Edge> edges() const;
        std::vector<Group> groups() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace termin::nodegraph

