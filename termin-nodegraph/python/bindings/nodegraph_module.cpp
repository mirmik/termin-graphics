#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <memory>
#include <stdexcept>
#include <string>

#include <tcbase/tc_value.h>
#include <termin/nodegraph/graph.hpp>

namespace nb = nanobind;
using namespace nb::literals;
namespace ng = termin::nodegraph;

namespace {

    tc::trent python_to_value(nb::handle value) {
        if (value.is_none())
            return tc::trent::nil();
        if (nb::isinstance<nb::bool_>(value))
            return tc::trent(nb::cast<bool>(value));
        if (nb::isinstance<nb::int_>(value))
            return tc::trent(nb::cast<int64_t>(value));
        if (nb::isinstance<nb::float_>(value))
            return tc::trent(nb::cast<double>(value));
        if (nb::isinstance<nb::str>(value))
            return tc::trent(nb::cast<std::string>(value));
        if (nb::isinstance<nb::list>(value) || nb::isinstance<nb::tuple>(value)) {
            tc::trent result = tc::trent::list();
            for (nb::handle item : nb::borrow<nb::iterable>(value))
                result.push_back(python_to_value(item));
            return result;
        }
        if (nb::isinstance<nb::dict>(value)) {
            tc::trent result = tc::trent::dict();
            for (auto item : nb::cast<nb::dict>(value)) {
                if (!nb::isinstance<nb::str>(item.first))
                    throw nb::type_error("nodegraph dictionaries require string keys");
                result[nb::cast<std::string>(item.first)] = python_to_value(item.second);
            }
            return result;
        }
        throw nb::type_error("nodegraph values must be JSON-compatible");
    }

    nb::object value_to_python(const tc_value& value) {
        switch (value.type) {
        case TC_VALUE_NIL:
            return nb::none();
        case TC_VALUE_BOOL:
            return nb::bool_(value.data.b);
        case TC_VALUE_INT:
            return nb::int_(value.data.i);
        case TC_VALUE_FLOAT:
            return nb::float_(value.data.f);
        case TC_VALUE_DOUBLE:
            return nb::float_(value.data.d);
        case TC_VALUE_STRING:
            return nb::str(value.data.s ? value.data.s : "");
        case TC_VALUE_LIST: {
            nb::list result;
            for (size_t index = 0; index < value.data.list.count; ++index)
                result.append(value_to_python(value.data.list.items[index]));
            return result;
        }
        case TC_VALUE_DICT: {
            nb::dict result;
            for (size_t index = 0; index < value.data.dict.count; ++index) {
                const tc_value_dict_entry& entry = value.data.dict.entries[index];
                result[entry.key ? entry.key : ""] = value_to_python(*entry.value);
            }
            return result;
        }
        }
        throw nb::value_error("malformed native nodegraph value");
    }

    nb::object value_to_python(const tc::trent& value) {
        return value_to_python(value.get());
    }

    template <typename T>
    T dict_value(const nb::dict& value, const char* key, T fallback) {
        if (!value.contains(key))
            return fallback;
        return nb::cast<T>(value[key]);
    }

    class PythonValidator final : public ng::ConnectionValidator {
    public:
        bool accepts(const ng::ConnectionProposal& proposal) const override {
            if (validator_.is_none())
                return proposal.source_type == "any" || proposal.destination_type == "any" ||
                       proposal.source_type == proposal.destination_type;
            return nb::cast<bool>(validator_.attr("validate")(
                proposal.source_type,
                proposal.destination_type,
                "src_node_id"_a = proposal.source_node_id,
                "src_socket"_a = proposal.source_socket,
                "dst_node_id"_a = proposal.destination_node_id,
                "dst_socket"_a = proposal.destination_socket));
        }

        void set(nb::object validator) {
            validator_ = std::move(validator);
        }

    private:
        nb::object validator_ = nb::none();
    };

    class NativeGraph {
    public:
        NativeGraph()
            : validator_(std::make_shared<PythonValidator>()), graph_(validator_) {}

        uint64_t revision() const {
            return graph_.revision();
        }

        void set_validator(nb::object validator) {
            validator_->set(std::move(validator));
        }

        nb::dict serialize() const {
            return nb::cast<nb::dict>(value_to_python(graph_.to_value()));
        }

        void replace(nb::handle value) {
            tc::trent encoded = python_to_value(value);
            const auto result = graph_.replace_from_value(encoded.get());
            require(result);
        }

        std::string to_json(int indent) const {
            return graph_.to_json(indent);
        }

        void replace_json(const std::string& json) {
            require(graph_.replace_from_json(json));
        }

        nb::dict create_node(const nb::dict& descriptor) {
            ng::NodeDescriptor value;
            value.id = dict_value<std::string>(descriptor, "id", "");
            value.kind = dict_value<std::string>(descriptor, "kind", "");
            value.title = dict_value<std::string>(descriptor, "title", value.kind);
            value.x = dict_value<float>(descriptor, "x", 0.0f);
            value.y = dict_value<float>(descriptor, "y", 0.0f);
            value.width = dict_value<float>(descriptor, "width", 190.0f);
            value.height = dict_value<float>(descriptor, "height", 120.0f);
            if (descriptor.contains("params"))
                value.params = python_to_value(descriptor["params"]);
            if (descriptor.contains("data"))
                value.data = python_to_value(descriptor["data"]);
            append_sockets(descriptor, "inputs", value.inputs, false);
            append_sockets(descriptor, "outputs", value.outputs, true);
            const auto result = graph_.create_node(std::move(value));
            require(result);
            return node_snapshot(result.value);
        }

        bool remove_node(const std::string& id) {
            const auto handle = graph_.find_node(id);
            if (!handle)
                return false;
            require(graph_.remove_node(*handle));
            return true;
        }

        bool move_node(const std::string& id, float x, float y) {
            const auto handle = graph_.find_node(id);
            if (!handle)
                return false;
            require(graph_.move_node(*handle, x, y));
            return true;
        }

        bool add_socket(const std::string& id,
                        const std::string& name,
                        const std::string& type,
                        bool multi,
                        bool input) {
            const auto handle = graph_.find_node(id);
            if (!handle)
                return false;
            const auto result = input ? graph_.add_input(*handle, {name, type, multi})
                                      : graph_.add_output(*handle, {name, type, multi});
            if (!result)
                return false;
            return true;
        }

        bool set_node_param(const std::string& id, const std::string& name, nb::handle value) {
            const auto handle = graph_.find_node(id);
            if (!handle)
                return false;
            tc::trent encoded = python_to_value(value);
            require(graph_.set_node_param(*handle, name, encoded.get()));
            return true;
        }

        bool set_node_data(const std::string& id, nb::handle value) {
            const auto handle = graph_.find_node(id);
            if (!handle)
                return false;
            tc::trent encoded = python_to_value(value);
            require(graph_.set_node_data(*handle, encoded.get()));
            return true;
        }

        nb::dict update_node(const std::string& id, const nb::dict& changes) {
            nb::dict document = serialize();
            nb::list nodes = nb::cast<nb::list>(document["nodes"]);
            for (nb::handle item : nodes) {
                nb::dict node = nb::cast<nb::dict>(item);
                if (nb::cast<std::string>(node["id"]) != id)
                    continue;
                static constexpr const char* fields[] = {
                    "title", "x", "y", "width", "height", "params", "data", "inputs", "outputs"};
                for (const char* field : fields) {
                    if (changes.contains(field))
                        node[field] = changes[field];
                }
                tc::trent encoded = python_to_value(document);
                require(graph_.replace_from_value(encoded.get()));
                const auto handle = graph_.find_node(id);
                if (!handle)
                    throw std::runtime_error("updated node snapshot disappeared");
                return node_snapshot(*handle);
            }
            throw nb::key_error(id.c_str());
        }

        nb::dict connect(const std::string& source_id,
                         const std::string& source_socket,
                         const std::string& destination_id,
                         const std::string& destination_socket,
                         const std::string& edge_id) {
            nb::dict encoded;
            const auto source = graph_.find_node(source_id);
            const auto destination = graph_.find_node(destination_id);
            if (!source || !destination) {
                encoded["ok"] = false;
                encoded["reason"] = "node not found";
                return encoded;
            }
            const auto result = graph_.connect({*source, source_socket, *destination, destination_socket, edge_id});
            encoded["ok"] = result.ok();
            encoded["reason"] = result.ok() ? "" : connection_reason(result.error);
            encoded["message"] = result.message;
            encoded["error_code"] = static_cast<int>(result.error);
            encoded["edge_id"] = result.ok() ? graph_.edge(result.value.edge)->id : "";
            return encoded;
        }

        bool remove_edge(const std::string& id) {
            const auto handle = graph_.find_edge(id);
            if (!handle)
                return false;
            require(graph_.remove_edge(*handle));
            return true;
        }

        nb::dict create_group(const nb::dict& descriptor) {
            ng::GroupDescriptor value;
            value.id = dict_value<std::string>(descriptor, "id", "");
            value.title = dict_value<std::string>(descriptor, "title", "");
            value.x = dict_value<float>(descriptor, "x", 0.0f);
            value.y = dict_value<float>(descriptor, "y", 0.0f);
            value.width = dict_value<float>(descriptor, "width", 0.0f);
            value.height = dict_value<float>(descriptor, "height", 0.0f);
            if (descriptor.contains("data"))
                value.data = python_to_value(descriptor["data"]);
            const auto result = graph_.create_group(std::move(value));
            require(result);
            return group_snapshot(result.value);
        }

        bool remove_group(const std::string& id) {
            const auto handle = graph_.find_group(id);
            if (!handle)
                return false;
            require(graph_.remove_group(*handle));
            return true;
        }

        bool move_group(const std::string& id, float x, float y) {
            const auto handle = graph_.find_group(id);
            if (!handle)
                return false;
            require(graph_.move_group(*handle, x, y));
            return true;
        }

        bool set_group_data(const std::string& id, nb::handle value) {
            const auto handle = graph_.find_group(id);
            if (!handle)
                return false;
            tc::trent encoded = python_to_value(value);
            require(graph_.set_group_data(*handle, encoded.get()));
            return true;
        }

        void set_data(nb::handle value) {
            tc::trent encoded = python_to_value(value);
            require(graph_.set_data(encoded.get()));
        }

    private:
        static void require(const ng::Result<void>& result) {
            if (!result)
                throw nb::value_error(result.message.c_str());
        }

        template <typename T>
        static void require(const ng::Result<T>& result) {
            if (!result)
                throw nb::value_error(result.message.c_str());
        }

        static void append_sockets(const nb::dict& descriptor,
                                   const char* key,
                                   std::vector<ng::Socket>& out,
                                   bool default_multi) {
            if (!descriptor.contains(key))
                return;
            for (nb::handle item : nb::borrow<nb::iterable>(descriptor[key])) {
                nb::dict socket = nb::cast<nb::dict>(item);
                out.push_back({dict_value<std::string>(socket, "name", ""),
                               dict_value<std::string>(socket, "socket_type", "any"),
                               dict_value<bool>(socket, "multi", default_multi)});
            }
        }

        static const char* connection_reason(ng::ErrorCode error) {
            switch (error) {
            case ng::ErrorCode::DuplicateId:
                return "duplicate edge id";
            case ng::ErrorCode::NodeNotFound:
                return "node not found";
            case ng::ErrorCode::SocketNotFound:
                return "socket not found";
            case ng::ErrorCode::SelfLink:
                return "self-link";
            case ng::ErrorCode::TypeMismatch:
                return "type mismatch";
            case ng::ErrorCode::CardinalityViolation:
                return "cardinality violation";
            default:
                return "connection rejected";
            }
        }

        nb::dict node_snapshot(ng::NodeHandle handle) const {
            const std::string id = graph_.node(handle)->id;
            nb::list nodes = nb::cast<nb::list>(serialize()["nodes"]);
            for (nb::handle item : nodes) {
                nb::dict node = nb::cast<nb::dict>(item);
                if (nb::cast<std::string>(node["id"]) == id)
                    return node;
            }
            throw std::runtime_error("created node snapshot disappeared");
        }

        nb::dict group_snapshot(ng::GroupHandle handle) const {
            const std::string id = graph_.group(handle)->id;
            nb::list groups = nb::cast<nb::list>(serialize()["groups"]);
            for (nb::handle item : groups) {
                nb::dict group = nb::cast<nb::dict>(item);
                if (nb::cast<std::string>(group["id"]) == id)
                    return group;
            }
            throw std::runtime_error("created group snapshot disappeared");
        }

        std::shared_ptr<PythonValidator> validator_;
        ng::Graph graph_;
    };

} // namespace

NB_MODULE(_nodegraph_native, module) {
    module.doc() = "Native Termin nodegraph core binding";
    nb::class_<NativeGraph>(module, "NativeGraph")
        .def(nb::init<>())
        .def_prop_ro("revision", &NativeGraph::revision)
        .def("set_validator", &NativeGraph::set_validator)
        .def("serialize", &NativeGraph::serialize)
        .def("replace", &NativeGraph::replace)
        .def("to_json", &NativeGraph::to_json, "indent"_a = 2)
        .def("replace_json", &NativeGraph::replace_json)
        .def("create_node", &NativeGraph::create_node)
        .def("remove_node", &NativeGraph::remove_node)
        .def("move_node", &NativeGraph::move_node)
        .def("add_socket", &NativeGraph::add_socket)
        .def("set_node_param", &NativeGraph::set_node_param)
        .def("set_node_data", &NativeGraph::set_node_data)
        .def("update_node", &NativeGraph::update_node)
        .def("connect", &NativeGraph::connect, "source_id"_a, "source_socket"_a,
             "destination_id"_a, "destination_socket"_a, "edge_id"_a = "")
        .def("remove_edge", &NativeGraph::remove_edge)
        .def("create_group", &NativeGraph::create_group)
        .def("remove_group", &NativeGraph::remove_group)
        .def("move_group", &NativeGraph::move_group)
        .def("set_group_data", &NativeGraph::set_group_data)
        .def("set_data", &NativeGraph::set_data);
}
