#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <trent/trent.h>

namespace tc {

struct SocketData {
    std::string name;
    std::string socket_type;
    bool is_input = false;
};

struct NodeData {
    std::string id;
    std::string node_type;
    std::string pass_class;
    std::string name;
    nos::trent params;
    std::vector<SocketData> inputs;
    std::vector<SocketData> outputs;
    float x = 0;
    float y = 0;
};

struct ConnectionData {
    std::string from_node_id;
    std::string from_socket;
    std::string to_node_id;
    std::string to_socket;
};

struct ViewportFrameData {
    std::string viewport_name;
    float x = 0;
    float y = 0;
    float width = 400;
    float height = 300;
};

struct GraphData {
    std::vector<NodeData> nodes;
    std::vector<ConnectionData> connections;
    std::vector<ViewportFrameData> viewport_frames;

    NodeData* get_node(const std::string& id);
    const NodeData* get_node(const std::string& id) const;

    static GraphData from_trent(const nos::trent& t);
};

struct PassSocketInfo {
    std::vector<std::pair<std::string, std::string>> inputs;
    std::vector<std::pair<std::string, std::string>> outputs;
};

PassSocketInfo get_pass_sockets(const std::string& class_name);

} // namespace tc
