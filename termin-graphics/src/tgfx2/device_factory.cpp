#include "tgfx2/device_factory.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#endif

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace tgfx {

BackendType default_backend_from_env() {
    const char* env = std::getenv("TERMIN_BACKEND");
    if (!env || !env[0]) return BackendType::OpenGL;

    std::string s(env);
    for (auto& c : s) c = static_cast<char>(std::tolower(c));

    if (s == "opengl" || s == "gl") return BackendType::OpenGL;
    if (s == "vulkan" || s == "vk") return BackendType::Vulkan;
    if (s == "metal") return BackendType::Metal;
    if (s == "d3d12" || s == "dx12") return BackendType::D3D12;
    if (s == "null") return BackendType::Null;

    // Unknown value — fall back to OpenGL. Silent fallback (no log
    // dependency) so the factory stays header-light.
    return BackendType::OpenGL;
}

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

} // namespace tgfx
