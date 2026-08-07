#include "tcplot/gpu_host.hpp"

#include <tgfx2/device_factory.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/graphics_host.hpp>

namespace tcplot {

GpuHost::GpuHost(const std::string& ttf_path)
    : GpuHost(ttf_path, tgfx::default_backend_from_env()) {}

GpuHost::GpuHost(const std::string& ttf_path, tgfx::BackendType backend) {
    owned_graphics_ = tgfx::GraphicsHost::create_application(backend);
    graphics_ = owned_graphics_.get();
    owned_font_ = std::make_unique<tgfx::FontAtlas>(ttf_path);
    font_ = owned_font_.get();
}

GpuHost::GpuHost(const std::string& ttf_path, tgfx::GraphicsHost& graphics)
    : graphics_(&graphics) {
    owned_font_ = std::make_unique<tgfx::FontAtlas>(ttf_path);
    font_ = owned_font_.get();
}

GpuHost::GpuHost(tgfx::GraphicsHost& graphics, tgfx::FontAtlas& font)
    : graphics_(&graphics), font_(&font) {}

GpuHost::~GpuHost() = default;

tgfx::IRenderDevice& GpuHost::device() {
    return graphics_->device();
}

tgfx::PipelineCache& GpuHost::cache() {
    return graphics_->cache();
}

tgfx::RenderContext2& GpuHost::ctx() {
    return graphics_->context();
}

}  // namespace tcplot
