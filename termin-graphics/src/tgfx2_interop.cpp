#include <tgfx/tgfx2_interop.h>
#include <tgfx2/i_render_device.hpp>

#ifdef _WIN32
#include <tgfx2/d3d11/d3d11_render_device.hpp>
#include <tgfx2/d3d11/d3d11_swapchain.hpp>
#include <windows.h>
#endif

#include <cstdint>
#include <exception>

extern "C" {
#include <tcbase/tc_log.h>
}

static void* g_tgfx2_device = nullptr;

void tgfx2_interop_set_device(void* device) {
    g_tgfx2_device = device;
}

void* tgfx2_interop_get_device(void) {
    return g_tgfx2_device;
}

uint32_t tgfx2_interop_register_external_gl_texture(
    uint32_t gl_tex_id,
    uint32_t width,
    uint32_t height,
    int format,
    uint32_t usage)
{
    if (gl_tex_id == 0 || width == 0 || height == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid external GL texture: id=%u size=%ux%u",
               gl_tex_id, width, height);
        return 0;
    }

    auto* device = static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
    if (!device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot register external GL texture: no active tgfx2 device");
        return 0;
    }

    tgfx::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = static_cast<tgfx::PixelFormat>(format);
    desc.usage = static_cast<tgfx::TextureUsage>(usage);

    try {
        tgfx::TextureHandle handle =
            device->register_external_texture(static_cast<uintptr_t>(gl_tex_id), desc);
        return handle.id;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to register external GL texture: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to register external GL texture: unknown error");
    }
    return 0;
}

void tgfx2_interop_destroy_texture_handle(uint32_t handle_id) {
    if (handle_id == 0) {
        return;
    }
    auto* device = static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
    if (!device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot destroy texture handle %u: no active tgfx2 device",
               handle_id);
        return;
    }

    try {
        device->destroy(tgfx::TextureHandle{handle_id});
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to destroy texture handle %u: %s",
               handle_id, e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to destroy texture handle %u: unknown error",
               handle_id);
    }
}

void tgfx2_interop_blit_texture(
    uint32_t src_handle_id,
    uint32_t dst_handle_id,
    int width,
    int height)
{
    if (src_handle_id == 0 || dst_handle_id == 0 || width <= 0 || height <= 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid blit: src=%u dst=%u size=%dx%d",
               src_handle_id, dst_handle_id, width, height);
        return;
    }

    auto* device = static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
    if (!device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot blit texture: no active tgfx2 device");
        return;
    }

    try {
        device->blit_to_texture(
            tgfx::TextureHandle{dst_handle_id},
            tgfx::TextureHandle{src_handle_id},
            0, 0, width, height,
            0, 0, width, height);
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to blit texture: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to blit texture: unknown error");
    }
}

void* tgfx2_interop_create_d3d11_swapchain(
    void* hwnd,
    uint32_t width,
    uint32_t height)
{
#ifdef _WIN32
    if (!hwnd || width == 0 || height == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3D11 swapchain request: hwnd=%p size=%ux%u",
               hwnd, width, height);
        return nullptr;
    }

    auto* device = static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
    if (!device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot create D3D11 swapchain: no active tgfx2 device");
        return nullptr;
    }
    if (device->backend_type() != tgfx::BackendType::D3D11) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot create D3D11 swapchain: active backend is not D3D11");
        return nullptr;
    }

    auto* d3d_device = dynamic_cast<tgfx::D3D11RenderDevice*>(device);
    if (!d3d_device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] active tgfx2 device reports D3D11 but is not D3D11RenderDevice");
        return nullptr;
    }

    try {
        return new tgfx::D3D11Swapchain(
            *d3d_device,
            static_cast<HWND>(hwnd),
            width,
            height);
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to create D3D11 swapchain: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to create D3D11 swapchain: unknown error");
    }
    return nullptr;
#else
    (void)hwnd; (void)width; (void)height;
    tc_log(TC_LOG_ERROR,
           "[tgfx2_interop] D3D11 swapchain creation is only supported on Windows");
    return nullptr;
#endif
}

void tgfx2_interop_destroy_d3d11_swapchain(void* swapchain) {
#ifdef _WIN32
    if (!swapchain) {
        return;
    }
    delete static_cast<tgfx::D3D11Swapchain*>(swapchain);
#else
    (void)swapchain;
#endif
}

int tgfx2_interop_resize_d3d11_swapchain(
    void* swapchain,
    uint32_t width,
    uint32_t height)
{
#ifdef _WIN32
    if (!swapchain || width == 0 || height == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3D11 swapchain resize: swapchain=%p size=%ux%u",
               swapchain, width, height);
        return 0;
    }

    try {
        static_cast<tgfx::D3D11Swapchain*>(swapchain)->resize(width, height);
        return 1;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to resize D3D11 swapchain: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to resize D3D11 swapchain: unknown error");
    }
    return 0;
#else
    (void)swapchain; (void)width; (void)height;
    tc_log(TC_LOG_ERROR,
           "[tgfx2_interop] D3D11 swapchain resize is only supported on Windows");
    return 0;
#endif
}

int tgfx2_interop_present_d3d11_swapchain(
    void* swapchain,
    uint32_t source_handle_id,
    uint32_t sync_interval)
{
#ifdef _WIN32
    if (!swapchain || source_handle_id == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3D11 swapchain present: swapchain=%p source=%u",
               swapchain, source_handle_id);
        return 0;
    }

    try {
        return static_cast<tgfx::D3D11Swapchain*>(swapchain)
            ->compose_and_present(tgfx::TextureHandle{source_handle_id}, sync_interval)
            ? 1 : 0;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to present D3D11 swapchain: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to present D3D11 swapchain: unknown error");
    }
    return 0;
#else
    (void)swapchain; (void)source_handle_id; (void)sync_interval;
    tc_log(TC_LOG_ERROR,
           "[tgfx2_interop] D3D11 swapchain present is only supported on Windows");
    return 0;
#endif
}