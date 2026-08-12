#pragma once

#include <glad/glad.h>

namespace tgfx::gl_web_compat {

    inline void set_draw_buffer(GLenum buffer) {
#if defined(__EMSCRIPTEN__)
        glDrawBuffers(1, &buffer);
#else
        glDrawBuffer(buffer);
#endif
    }

    inline bool framebuffer_srgb_enabled() {
#if defined(__EMSCRIPTEN__)
        return false;
#else
        return glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE;
#endif
    }

    inline void set_framebuffer_srgb(bool enabled) {
#if !defined(__EMSCRIPTEN__)
        if (enabled)
            glEnable(GL_FRAMEBUFFER_SRGB);
        else
            glDisable(GL_FRAMEBUFFER_SRGB);
#else
        (void)enabled;
#endif
    }

} // namespace tgfx::gl_web_compat
