#include "tgfx/opengl/opengl_backend.hpp"
#include "tgfx/opengl/opengl_framebuffer.hpp"

namespace termin {

// Move inline implementations to .cpp to ensure single RTTI across modules

FramebufferHandlePtr OpenGLGraphicsBackend::create_framebuffer(
    int width, int height, int samples,
    const std::string& format, TextureFilter filter
) {
    FBOFormat fmt = format.empty() ? FBOFormat::RGBA8 : parse_fbo_format(format);
    return std::make_unique<OpenGLFramebufferHandle>(width, height, samples, fmt, filter);
}

FramebufferHandlePtr OpenGLGraphicsBackend::create_shadow_framebuffer(int width, int height) {
    return std::make_unique<OpenGLShadowFramebufferHandle>(width, height);
}

} // namespace termin
