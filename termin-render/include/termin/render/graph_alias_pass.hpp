#pragma once

#include <set>
#include <string>
#include <vector>

#include <termin/render/frame_pass.hpp>

namespace termin {

class RENDER_API GraphAliasPass : public CxxFramePass {
public:
    std::vector<std::string> read_resources;
    std::vector<std::string> write_resources;
    std::vector<std::string> alias_resources;

    INSPECT_FIELD(GraphAliasPass, read_resources, "Read Resources", "list[string]")
    INSPECT_FIELD(GraphAliasPass, write_resources, "Write Resources", "list[string]")
    INSPECT_FIELD(GraphAliasPass, alias_resources, "Alias Resources", "list[string]")
    INSPECT_TYPE_METADATA(GraphAliasPass, graph, make_pass_graph_metadata({}, {}, {}))

    GraphAliasPass(
        std::vector<std::string> reads = {},
        std::vector<std::string> writes = {},
        std::vector<std::string> aliases = {},
        const std::string& pass_name = "GraphAliasPass"
    );

    std::set<const char*> compute_reads() const override;
    std::set<const char*> compute_writes() const override;
    std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override;
    void execute(ExecuteContext& ctx) override;
};

} // namespace termin
