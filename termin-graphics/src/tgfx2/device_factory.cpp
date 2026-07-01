#include "tgfx2/device_factory.hpp"
#ifdef TGFX2_HAS_OPENGL
#include "tgfx2/opengl/opengl_render_device.hpp"
#endif

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#endif

#ifdef TGFX2_HAS_D3D11
#include "tgfx2/d3d11/d3d11_render_device.hpp"
#endif

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace tgfx {

const char* backend_name(BackendType type) {
    switch (type) {
        case BackendType::OpenGL: return "opengl";
        case BackendType::Vulkan: return "vulkan";
        case BackendType::Metal: return "metal";
        case BackendType::D3D11: return "d3d11";
        case BackendType::Null: return "null";
    }
    return "unknown";
}

BackendType backend_from_name(const std::string& name) {
    std::string s(name);
    for (auto& c : s) c = static_cast<char>(std::tolower(c));

    if (s == "opengl" || s == "gl") return BackendType::OpenGL;
    if (s == "vulkan" || s == "vk") return BackendType::Vulkan;
    if (s == "metal") return BackendType::Metal;
    if (s == "d3d11" || s == "dx11") return BackendType::D3D11;
    if (s == "null") return BackendType::Null;
    return BackendType::Null;
}

bool backend_is_compiled(BackendType type) {
    switch (type) {
        case BackendType::OpenGL:
#ifdef TGFX2_HAS_OPENGL
            return true;
#else
            return false;
#endif
        case BackendType::Vulkan:
#ifdef TGFX2_HAS_VULKAN
            return true;
#else
            return false;
#endif
        case BackendType::D3D11:
#ifdef TGFX2_HAS_D3D11
            return true;
#else
            return false;
#endif
        case BackendType::Metal:
        case BackendType::Null:
            return false;
    }
    return false;
}

BackendType compiled_default_backend() {
#if defined(_WIN32) && defined(TGFX2_HAS_D3D11)
    return BackendType::D3D11;
#elif defined(TGFX2_HAS_VULKAN)
    return BackendType::Vulkan;
#elif defined(TGFX2_HAS_OPENGL)
    return BackendType::OpenGL;
#elif defined(TGFX2_HAS_D3D11)
    return BackendType::D3D11;
#else
    return BackendType::Null;
#endif
}


BackendType default_backend_from_env() {
    const char* env = std::getenv("TERMIN_BACKEND");
    if (!env || !env[0]) return compiled_default_backend();

    std::string s(env);
    for (auto& c : s) c = static_cast<char>(std::tolower(c));
    if (s == "null") return BackendType::Null;

    BackendType backend = backend_from_name(s);
    if (backend != BackendType::Null) return backend;

    std::fprintf(stderr,
                 "[tgfx2] Unknown TERMIN_BACKEND='%s'; using compiled default backend\n",
                 env);
    return compiled_default_backend();
}

std::unique_ptr<IRenderDevice> create_device(BackendType type) {
    switch (type) {
        case BackendType::OpenGL:
#ifdef TGFX2_HAS_OPENGL
            return std::make_unique<OpenGLRenderDevice>();
#else
            throw std::runtime_error("OpenGL backend not compiled (set TGFX2_ENABLE_OPENGL=ON)");
#endif

        case BackendType::Vulkan:
#ifdef TGFX2_HAS_VULKAN
        {
            VulkanDeviceCreateInfo info;
            // Validation layer is opt-in via TGFX2_VULKAN_VALIDATION=1.
            // Leaving it on in release costs 3-5× on ShadowPass / ColorPass
            // because every vkCmdDraw, descriptor update and submit routes
            // through the layer's CPU-side checks. Dev builds turn it on
            // explicitly when chasing a GPU validation issue.
            const char* val = std::getenv("TGFX2_VULKAN_VALIDATION");
            info.enable_validation = (val && val[0] == '1');
            return std::make_unique<VulkanRenderDevice>(info);
        }
#else
            throw std::runtime_error("Vulkan backend not compiled (set TGFX2_ENABLE_VULKAN=ON)");
#endif

        case BackendType::D3D11:
#ifdef TGFX2_HAS_D3D11
            return std::make_unique<D3D11RenderDevice>();
#else
            throw std::runtime_error("D3D11 backend not compiled (set TGFX2_ENABLE_D3D11=ON)");
#endif

        case BackendType::Metal:
            throw std::runtime_error("Backend not yet implemented");

        case BackendType::Null:
            throw std::runtime_error("Null backend not yet implemented");
    }

    throw std::runtime_error("Unknown backend type");
}

} // namespace tgfx
