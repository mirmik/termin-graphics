#include <termin/render/builtin_passes.hpp>
#include <termin/render/graph_alias_pass.hpp>
#include <termin/render/unknown_pass.hpp>

namespace termin {

void register_builtin_render_pass_types() {
    auto root = FramePassTypeDescriptorBuilder::abstract_native(
        "CxxFramePass", "termin-render");
    (void)root.commit();
    ensure_unknown_pass_registered();
    GraphAliasPass::register_type();
}

} // namespace termin
