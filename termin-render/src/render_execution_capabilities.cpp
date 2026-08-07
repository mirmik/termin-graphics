#include <termin/render/render_execution_capabilities.hpp>

namespace termin {

    void RenderExecutionCapabilities::add(const RenderExecutionCapability& capability) {
        values_.push_back(&capability);
    }

} // namespace termin
