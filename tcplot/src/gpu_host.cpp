#include "tcplot/gpu_host.hpp"

#include <tgfx2/device_factory.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

namespace tcplot {

GpuHost::GpuHost(const std::string& ttf_path)
    : GpuHost(ttf_path, tgfx::default_backend_from_env()) {}

GpuHost::GpuHost(const std::string& ttf_path, tgfx::BackendType backend) {
    // Order matters: cache wraps device, ctx wraps cache + device,
    // font is self-contained. Destruction is reverse (font, ctx,
    // cache, device) — which is what `std::unique_ptr` does in
    // reverse field-declaration order in the destructor.
    device_ = tgfx::create_device(backend);
    cache_  = std::make_unique<tgfx::PipelineCache>(*device_);
    ctx_    = std::make_unique<tgfx::RenderContext2>(*device_, *cache_);
    font_   = std::make_unique<tgfx::FontAtlas>(ttf_path);
}

GpuHost::~GpuHost() = default;

}  // namespace tcplot
