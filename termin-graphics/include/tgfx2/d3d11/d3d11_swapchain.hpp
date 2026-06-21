#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>

#include <dxgi.h>
#include <wrl/client.h>
#include <windef.h>

#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class D3D11RenderDevice;

class TGFX2_TYPE_API D3D11Swapchain {
public:
    D3D11Swapchain(D3D11RenderDevice& device, HWND hwnd, uint32_t width, uint32_t height);
    ~D3D11Swapchain();

    D3D11Swapchain(const D3D11Swapchain&) = delete;
    D3D11Swapchain& operator=(const D3D11Swapchain&) = delete;

    bool present(uint32_t sync_interval = 1);
    bool compose_and_present(TextureHandle color_texture, uint32_t sync_interval = 1);
    void resize(uint32_t width, uint32_t height);

    TextureHandle backbuffer_texture() const { return backbuffer_texture_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    IDXGISwapChain* native_swapchain() const { return swapchain_.Get(); }

private:
    void create_swapchain(HWND hwnd);
    void refresh_backbuffer_texture();
    void release_backbuffer_texture();

    D3D11RenderDevice& device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain_;
    TextureHandle backbuffer_texture_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace tgfx
