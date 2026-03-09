#include "tgfx/opengl/opengl_framebuffer.hpp"

namespace termin {

// Out-of-line virtual destructor to ensure vtable and typeinfo are in one place
// This is the "key function" that determines where RTTI is emitted

OpenGLFramebufferHandle::~OpenGLFramebufferHandle() {
    release();
}

OpenGLShadowFramebufferHandle::~OpenGLShadowFramebufferHandle() {
    release();
}

} // namespace termin
