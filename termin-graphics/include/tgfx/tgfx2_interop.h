// tgfx2_interop.h - C bridge to the active tgfx2 IRenderDevice.
#pragma once

#include "tgfx2/tgfx2_api.h"

#ifdef __cplusplus
#include <cstdint>
namespace tgfx { class IRenderDevice; }

extern "C" {
#else
#include <stdint.h>
#endif

// Set the active tgfx2 render device used by C/Python interop helpers.
TGFX2_API void tgfx2_interop_set_device(void* device);

// Get the tgfx2 render device (returns NULL if not set).
TGFX2_API void* tgfx2_interop_get_device(void);

// ---------------------------------------------------------------------------
// External GL texture registration - plain-C bridge to
// IRenderDevice::register_external_texture (OpenGL backend only).
//
// Intended for host code (C# / WPF through P/Invoke, any non-C++ caller)
// that already owns a GL texture (e.g. the color attachment of a WPF
// GLWpfControl framebuffer) and wants to hand it to the engine without
// talking to `IRenderDevice&` directly.
//
// `format` is a `tgfx::PixelFormat` cast to int. `usage` is a bitmask of
// `tgfx::TextureUsage` flags; the most common value is
// `Sampled | ColorAttachment | CopyDst` for a composite target.
//
// Returns a non-zero tgfx2 TextureHandle id on success. Returns 0 on error.
// The returned handle is owned by the caller; release it with
// `tgfx2_interop_destroy_texture_handle`.
TGFX2_API uint32_t tgfx2_interop_register_external_gl_texture(
    uint32_t gl_tex_id,
    uint32_t width, uint32_t height,
    int format,
    uint32_t usage);

// Release a handle previously returned by
// tgfx2_interop_register_external_gl_texture. Safe to call with id == 0.
TGFX2_API void tgfx2_interop_destroy_texture_handle(uint32_t handle_id);

// Copy/resolve one tgfx2 texture into another. Both arguments are
// TextureHandle ids owned by the current interop device.
TGFX2_API void tgfx2_interop_blit_texture(
    uint32_t src_handle_id,
    uint32_t dst_handle_id,
    int width,
    int height);

// ---------------------------------------------------------------------------
// D3D11 swapchain presentation bridge.
//
// WPF hosts that own an HWND can create a tgfx2-backed DXGI swapchain and
// present any texture produced by the current D3D11 tgfx2 device. The returned
// pointer is an opaque tgfx::D3D11Swapchain* and must be released with
// tgfx2_interop_destroy_d3d11_swapchain. Functions return 1 on success and 0
// on error; failures are logged through tc_log.
TGFX2_API void* tgfx2_interop_create_d3d11_swapchain(
    void* hwnd,
    uint32_t width,
    uint32_t height);

TGFX2_API void tgfx2_interop_destroy_d3d11_swapchain(void* swapchain);

TGFX2_API int tgfx2_interop_resize_d3d11_swapchain(
    void* swapchain,
    uint32_t width,
    uint32_t height);

TGFX2_API int tgfx2_interop_present_d3d11_swapchain(
    void* swapchain,
    uint32_t source_handle_id,
    uint32_t sync_interval);

// ---------------------------------------------------------------------------
// D3D11 -> WPF D3DImage presentation bridge.
//
// This path avoids creating a child-window DXGI swapchain. The bridge owns a
// shared BGRA texture visible to both the active tgfx2 D3D11 device and WPF's
// D3D9Ex-based D3DImage. C# obtains the IDirect3DSurface9 pointer through
// tgfx2_interop_get_d3d11_d3dimage_surface and passes it to
// D3DImage.SetBackBuffer. Each present blits the source tgfx2 texture into the
// shared texture; WPF composites it with the rest of the visual tree.
TGFX2_API void* tgfx2_interop_create_d3d11_d3dimage_bridge(
    uint32_t width,
    uint32_t height);

TGFX2_API void tgfx2_interop_destroy_d3d11_d3dimage_bridge(void* bridge);

TGFX2_API int tgfx2_interop_resize_d3d11_d3dimage_bridge(
    void* bridge,
    uint32_t width,
    uint32_t height);

TGFX2_API int tgfx2_interop_present_d3d11_d3dimage_bridge(
    void* bridge,
    uint32_t source_handle_id);

TGFX2_API void* tgfx2_interop_get_d3d11_d3dimage_surface(void* bridge);

#ifdef __cplusplus
}

// C++ typed accessors
namespace tgfx {

inline void set_tgfx2_device(tgfx::IRenderDevice* device) {
    tgfx2_interop_set_device(static_cast<void*>(device));
}

inline tgfx::IRenderDevice* get_tgfx2_device() {
    return static_cast<tgfx::IRenderDevice*>(tgfx2_interop_get_device());
}

} // namespace tgfx
#endif
