#include <termin/render/graph_alias_pass.hpp>

#include <termin/render/execute_context.hpp>

namespace termin {

GraphAliasPass::GraphAliasPass(
    std::vector<std::string> reads,
    std::vector<std::string> writes,
    std::vector<std::string> aliases,
    const std::string& pass_name
) : read_resources(std::move(reads)),
    write_resources(std::move(writes)),
    alias_resources(std::move(aliases)) {
    pass_name_set(pass_name);
    link_to_type_registry("GraphAliasPass");
}

std::set<const char*> GraphAliasPass::compute_reads() const {
    std::set<const char*> result;
    for (const auto& resource : read_resources) {
        if (!resource.empty()) {
            result.insert(resource.c_str());
        }
    }
    return result;
}

std::set<const char*> GraphAliasPass::compute_writes() const {
    std::set<const char*> result;
    for (const auto& resource : write_resources) {
        if (!resource.empty()) {
            result.insert(resource.c_str());
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> GraphAliasPass::get_inplace_aliases() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (size_t i = 0; i + 1 < alias_resources.size(); i += 2) {
        const std::string& source = alias_resources[i];
        const std::string& target = alias_resources[i + 1];
        if (!source.empty() && !target.empty()) {
            result.emplace_back(source, target);
        }
    }
    return result;
}

void GraphAliasPass::execute(ExecuteContext& ctx) {
    (void)ctx;
}

void GraphAliasPass::register_type() {
    auto descriptor = FramePassTypeDescriptorBuilder::native<GraphAliasPass>(
        "GraphAliasPass", "termin-render");
    auto& inspect = descriptor.inspect();
    _register_inspect_read_resources(inspect);
    _register_inspect_write_resources(inspect);
    _register_inspect_alias_resources(inspect);
    _register_inspect_metadata_graph(inspect);
    (void)descriptor.commit();
}

} // namespace termin
