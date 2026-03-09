#include "tgfx/opengl/opengl_backend.hpp"

namespace termin {

// Define static members
OpenGLGraphicsBackend* OpenGLGraphicsBackend::instance_ = nullptr;
bool OpenGLGraphicsBackend::glad_initialized_ = false;

// Singleton accessor implementation
OpenGLGraphicsBackend& OpenGLGraphicsBackend::get_instance() {
    if (instance_ == nullptr) {
        instance_ = new OpenGLGraphicsBackend();
    }
    return *instance_;
}

} // namespace termin
