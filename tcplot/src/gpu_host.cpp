#include "tcplot/gpu_host.hpp"

#include <tgfx2/device_factory.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/render_runtime.hpp>

namespace tcplot {

GpuHost::GpuHost(const std::string& ttf_path)
    : GpuHost(ttf_path, tgfx::default_backend_from_env()) {}

GpuHost::GpuHost(const std::string& ttf_path, tgfx::BackendType backend) {
    runtime_ = tgfx::RenderRuntime::create(backend);
    font_ = std::make_unique<tgfx::FontAtlas>(ttf_path);
}

GpuHost::~GpuHost() = default;

tgfx::IRenderDevice& GpuHost::device() {
    return runtime_->device();
}

tgfx::PipelineCache& GpuHost::cache() {
    return runtime_->cache();
}

tgfx::RenderContext2& GpuHost::ctx() {
    return runtime_->context();
}

}  // namespace tcplot
