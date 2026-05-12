// gpu_host.hpp — process-wide GPU runtime bundle for tcplot.
//
// Owns the one `IRenderDevice`, `PipelineCache`, `RenderContext2` and
// `FontAtlas` that every `PlotView*` borrows. Matches the "one device
// per process" invariant from tgfx2 Python bindings
// (`Tgfx2Context.from_window`) — recreating a device provokes GL
// resource collisions on shared contexts (two GLWpfControls in one
// window), so we create everything once at application startup and
// tear it down at process exit.
#pragma once

#include <memory>
#include <string>

#include <tgfx2/enums.hpp>

#include "tcplot/tcplot_api.h"

namespace tgfx {
class IRenderDevice;
class PipelineCache;
class RenderContext2;
class RenderRuntime;
class FontAtlas;
}

namespace tcplot {

class TCPLOT_API GpuHost {
public:
    // Create the full tgfx2 stack. Backend is picked by env
    // TERMIN_BACKEND (same rules as `tgfx::default_backend_from_env`).
    explicit GpuHost(const std::string& ttf_path);

    // Explicit backend override — mostly for the Vulkan path.
    GpuHost(const std::string& ttf_path, tgfx::BackendType backend);

    ~GpuHost();

    GpuHost(const GpuHost&) = delete;
    GpuHost& operator=(const GpuHost&) = delete;

    tgfx::IRenderDevice&  device();
    tgfx::PipelineCache&  cache();
    tgfx::RenderContext2& ctx();
    tgfx::FontAtlas&      font()   { return *font_;   }

private:
    std::unique_ptr<tgfx::RenderRuntime> runtime_;
    std::unique_ptr<tgfx::FontAtlas>      font_;
};

}  // namespace tcplot
