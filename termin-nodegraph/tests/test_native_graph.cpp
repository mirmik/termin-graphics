#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>

#include <termin/nodegraph/graph.hpp>

namespace ng = termin::nodegraph;

namespace {

    ng::NodeDescriptor source_descriptor(std::string id, bool multi = true) {
        ng::NodeDescriptor result;
        result.id = std::move(id);
        result.kind = "source";
        result.outputs.push_back({"value", "float", multi});
        return result;
    }

    ng::NodeDescriptor sink_descriptor(std::string id, bool multi = false) {
        ng::NodeDescriptor result;
        result.id = std::move(id);
        result.kind = "sink";
        result.inputs.push_back({"value", "float", multi});
        return result;
    }

    ng::ConnectRequest connection(ng::NodeHandle source, ng::NodeHandle destination, std::string id = {}) {
        return {source, "value", destination, "value", std::move(id)};
    }

    class RejectAll final : public ng::ConnectionValidator {
    public:
        bool accepts(const ng::ConnectionProposal&) const override {
            return false;
        }
    };

} // namespace

int main() {
    ng::Graph graph;
    assert(graph.revision() == 0);

    auto source = graph.create_node(source_descriptor("source"));
    auto sink_a = graph.create_node(sink_descriptor("sink_a"));
    auto sink_b = graph.create_node(sink_descriptor("sink_b"));
    assert(source && sink_a && sink_b);
    assert(graph.revision() == 3);

    const std::uint64_t before_duplicate = graph.revision();
    auto duplicate = graph.create_node(source_descriptor("source"));
    assert(!duplicate && duplicate.error == ng::ErrorCode::DuplicateId);
    assert(graph.revision() == before_duplicate);
    assert(graph.nodes().size() == 3);

    ng::NodeDescriptor bad_sockets = source_descriptor("bad");
    bad_sockets.outputs.push_back({"value", "float", true});
    auto bad_node = graph.create_node(std::move(bad_sockets));
    assert(!bad_node && bad_node.error == ng::ErrorCode::DuplicateSocket);
    assert(graph.find_node("bad") == std::nullopt);

    auto first = graph.connect(connection(source.value, sink_a.value, "edge_a"));
    assert(first && first.value.replaced_edges.empty());
    assert(graph.edges().size() == 1);

    const std::uint64_t before_duplicate_edge = graph.revision();
    auto duplicate_edge = graph.connect(connection(source.value, sink_a.value, "edge_a"));
    assert(!duplicate_edge && duplicate_edge.error == ng::ErrorCode::DuplicateId);
    assert(graph.revision() == before_duplicate_edge);
    assert(graph.edge(first.value.edge).has_value());

    auto replacement = graph.connect(connection(source.value, sink_a.value, "edge_b"));
    assert(replacement && replacement.value.replaced_edges.size() == 1);
    assert(graph.edge(first.value.edge) == std::nullopt);
    assert(graph.edges().size() == 1);

    auto second_sink = graph.connect(connection(source.value, sink_b.value, "edge_c"));
    assert(second_sink);
    assert(graph.edges().size() == 2);

    const std::uint64_t before_failure = graph.revision();
    auto self = graph.connect(connection(source.value, source.value));
    assert(!self && self.error == ng::ErrorCode::SelfLink);
    assert(graph.revision() == before_failure);
    assert(graph.edges().size() == 2);

    ng::NodeDescriptor integer_sink = sink_descriptor("integer_sink");
    integer_sink.inputs[0].type = "int";
    auto incompatible = graph.create_node(std::move(integer_sink));
    assert(incompatible);
    auto mismatch = graph.connect(connection(source.value, incompatible.value));
    assert(!mismatch && mismatch.error == ng::ErrorCode::TypeMismatch);

    const std::uint64_t before_cascade = graph.revision();
    assert(graph.remove_node(source.value));
    assert(graph.revision() == before_cascade + 1);
    assert(graph.edges().empty());
    assert(!graph.remove_node(source.value));

    auto replacement_source = graph.create_node(source_descriptor("replacement"));
    assert(replacement_source);
    assert(replacement_source.value.index == source.value.index);
    assert(replacement_source.value.generation != source.value.generation);
    assert(!graph.move_node(source.value, 1.0f, 2.0f));

    ng::Graph foreign;
    auto foreign_node = foreign.create_node(source_descriptor("foreign"));
    assert(foreign_node);
    assert(!graph.move_node(foreign_node.value, 1.0f, 2.0f));

    auto exclusive_source = graph.create_node(source_descriptor("exclusive_source", false));
    auto exclusive_sink_a = graph.create_node(sink_descriptor("exclusive_sink_a"));
    auto exclusive_sink_b = graph.create_node(sink_descriptor("exclusive_sink_b"));
    assert(exclusive_source && exclusive_sink_a && exclusive_sink_b);
    auto exclusive_first = graph.connect(connection(exclusive_source.value, exclusive_sink_a.value));
    auto exclusive_second = graph.connect(connection(exclusive_source.value, exclusive_sink_b.value));
    assert(exclusive_first && exclusive_second);
    assert(exclusive_second.value.replaced_edges.size() == 1);
    assert(graph.edge(exclusive_first.value.edge) == std::nullopt);

    const std::uint64_t before_missing_socket = graph.revision();
    auto missing_socket_request = connection(exclusive_source.value, exclusive_sink_a.value);
    missing_socket_request.source_socket = "missing";
    auto missing_socket = graph.connect(std::move(missing_socket_request));
    assert(!missing_socket && missing_socket.error == ng::ErrorCode::SocketNotFound);
    assert(graph.revision() == before_missing_socket);

    assert(!graph.add_input(exclusive_sink_a.value, {"value", "float", false}));
    assert(!graph.add_output(exclusive_source.value, {"", "float", true}));

    auto group = graph.create_group({"frame", "Frame", 1.0f, 2.0f, 300.0f, 200.0f});
    assert(group);
    assert(graph.move_group(group.value, 5.0f, 6.0f));
    assert(graph.group(group.value)->x == 5.0f);
    assert(graph.remove_group(group.value));
    assert(!graph.remove_group(group.value));

    auto first_named_group = graph.create_group({"duplicate_group", "One"});
    auto duplicate_named_group = graph.create_group({"duplicate_group", "Two"});
    assert(first_named_group);
    assert(!duplicate_named_group && duplicate_named_group.error == ng::ErrorCode::DuplicateId);

    ng::Graph rejected(std::make_shared<RejectAll>());
    auto rejected_source = rejected.create_node(source_descriptor("source"));
    auto rejected_sink = rejected.create_node(sink_descriptor("sink"));
    assert(rejected_source && rejected_sink);
    auto rejected_connection = rejected.connect(connection(rejected_source.value, rejected_sink.value));
    assert(!rejected_connection && rejected_connection.error == ng::ErrorCode::TypeMismatch);
    assert(rejected.edges().empty());

    ng::Graph any_graph;
    ng::NodeDescriptor any_source = source_descriptor("any_source");
    any_source.outputs[0].type = "any";
    ng::NodeDescriptor any_sink = sink_descriptor("any_sink");
    any_sink.inputs[0].type = "texture";
    auto any_source_handle = any_graph.create_node(std::move(any_source));
    auto any_sink_handle = any_graph.create_node(std::move(any_sink));
    assert(any_source_handle && any_sink_handle);
    assert(any_graph.connect(connection(any_source_handle.value, any_sink_handle.value)));

    return 0;
}
