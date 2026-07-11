#include <termin/render/builtin_passes.hpp>
#include <termin/render/frame_debug_capture_pass.hpp>
#include <termin/render/graph_alias_pass.hpp>

namespace termin {

void register_builtin_render_pass_types() {
    FrameDebugCapturePass::register_type();
    GraphAliasPass::register_type();
}

} // namespace termin
