#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>

#include <dxgi1_5.h>
#include <wrl/client.h>
#include <windef.h>

#include "tgfx2/d3d11/d3d11_presentation.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class D3D11RenderDevice;

class TGFX2_TYPE_API D3D11Swapchain {
public:
    D3D11Swapchain(
        D3D11RenderDevice& device,
        HWND hwnd,
        uint32_t width,
        uint32_t height,
        PresentationMode presentation_mode = PresentationMode::VSync);
    ~D3D11Swapchain();

    D3D11Swapchain(const D3D11Swapchain&) = delete;
    D3D11Swapchain& operator=(const D3D11Swapchain&) = delete;

    bool present();
    bool present(uint32_t sync_interval);
    bool compose_and_present(TextureHandle color_texture);
    bool compose_and_present(TextureHandle color_texture, uint32_t sync_interval);
    void resize(uint32_t width, uint32_t height);

    TextureHandle backbuffer_texture() const { return backbuffer_texture_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    IDXGISwapChain* native_swapchain() const { return swapchain_.Get(); }
    IDXGISwapChain1* native_swapchain1() const { return swapchain_.Get(); }
    PresentationMode requested_presentation_mode() const {
        return presentation_.requested_mode;
    }
    PresentationMode presentation_mode() const { return presentation_.effective_mode; }
    bool tearing_supported() const { return presentation_.tearing_supported; }
    bool tearing_enabled() const { return presentation_.allow_tearing; }

private:
    void create_swapchain(HWND hwnd);
    void refresh_backbuffer_texture();
    void release_backbuffer_texture();

    D3D11RenderDevice& device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_;
    TextureHandle backbuffer_texture_;
    D3D11PresentationPlan presentation_;
    uint32_t swapchain_flags_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace tgfx
