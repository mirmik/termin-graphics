#include "termin/render/tgfx2_bridge.hpp"

#include <string>
#include <string_view>

#include "tgfx/handles.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/descriptors.hpp"

namespace termin {

tgfx2::PixelFormat fbo_format_string_to_tgfx2(const char* format) {
    if (!format) return tgfx2::PixelFormat::RGBA8_UNorm;
    std::string_view s(format);
    if (s == "r8")      return tgfx2::PixelFormat::R8_UNorm;
    if (s == "r16f")    return tgfx2::PixelFormat::R16F;
    if (s == "r32f")    return tgfx2::PixelFormat::R32F;
    if (s == "rgba16f") return tgfx2::PixelFormat::RGBA16F;
    if (s == "rgba32f") return tgfx2::PixelFormat::RGBA32F;
    return tgfx2::PixelFormat::RGBA8_UNorm;
}

tgfx2::TextureHandle wrap_fbo_color_as_tgfx2(
    tgfx2::OpenGLRenderDevice& device,
    FramebufferHandle* fbo
) {
    if (!fbo) return {};
    GPUTextureHandle* color = fbo->color_texture();
    if (!color || !color->is_valid()) return {};

    tgfx2::TextureDesc desc;
    desc.width = static_cast<uint32_t>(fbo->get_width());
    desc.height = static_cast<uint32_t>(fbo->get_height());
    desc.format = fbo_format_string_to_tgfx2(fbo->get_format().c_str());
    desc.usage = tgfx2::TextureUsage::ColorAttachment |
                 tgfx2::TextureUsage::Sampled;
    desc.mip_levels = 1;
    desc.sample_count = static_cast<uint32_t>(fbo->get_samples() > 0 ? fbo->get_samples() : 1);

    return device.register_external_texture(
        static_cast<GLuint>(color->get_id()),
        desc
    );
}

} // namespace termin
