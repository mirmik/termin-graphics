#include "tgfx2/device_factory.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

#include <stdexcept>

namespace tgfx2 {

std::unique_ptr<IRenderDevice> create_device(BackendType type) {
    switch (type) {
        case BackendType::OpenGL:
            return std::make_unique<OpenGLRenderDevice>();

        case BackendType::Vulkan:
        case BackendType::Metal:
        case BackendType::D3D12:
            throw std::runtime_error("Backend not yet implemented");

        case BackendType::Null:
            throw std::runtime_error("Null backend not yet implemented");
    }

    throw std::runtime_error("Unknown backend type");
}

} // namespace tgfx2
