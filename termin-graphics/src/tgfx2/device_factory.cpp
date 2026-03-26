#include "tgfx2/device_factory.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#endif

#include <stdexcept>

namespace tgfx2 {

std::unique_ptr<IRenderDevice> create_device(BackendType type) {
    switch (type) {
        case BackendType::OpenGL:
            return std::make_unique<OpenGLRenderDevice>();

        case BackendType::Vulkan:
#ifdef TGFX2_HAS_VULKAN
        {
            VulkanDeviceCreateInfo info;
            info.enable_validation = true;
            return std::make_unique<VulkanRenderDevice>(info);
        }
#else
            throw std::runtime_error("Vulkan backend not compiled (set TGFX2_ENABLE_VULKAN=ON)");
#endif

        case BackendType::Metal:
        case BackendType::D3D12:
            throw std::runtime_error("Backend not yet implemented");

        case BackendType::Null:
            throw std::runtime_error("Null backend not yet implemented");
    }

    throw std::runtime_error("Unknown backend type");
}

} // namespace tgfx2
