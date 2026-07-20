#include "tgfx2/d3d11/d3d11_swapchain.hpp"

#include "tgfx2/d3d11/d3d11_render_device.hpp"
#include "tgfx2/presentation_geometry.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace tgfx {
namespace {

void throw_if_failed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s failed: HRESULT=0x%08X", what, static_cast<unsigned>(hr));
        throw std::runtime_error(buffer);
    }
}

} // namespace

D3D11Swapchain::D3D11Swapchain(D3D11RenderDevice& device, HWND hwnd, uint32_t width, uint32_t height)
    : device_(device),
      width_(std::max(1u, width)),
      height_(std::max(1u, height)) {
    if (!hwnd) {
        throw std::runtime_error("D3D11Swapchain requires a valid HWND");
    }
    create_swapchain(hwnd);
    refresh_backbuffer_texture();
}

D3D11Swapchain::~D3D11Swapchain() {
    release_backbuffer_texture();
}

void D3D11Swapchain::create_swapchain(HWND hwnd) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    throw_if_failed(
        device_.native_device()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi_device.GetAddressOf())),
        "ID3D11Device::QueryInterface(IDXGIDevice)");

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    throw_if_failed(dxgi_device->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");

    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    throw_if_failed(
        adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(factory.GetAddressOf())),
        "IDXGIAdapter::GetParent(IDXGIFactory)");

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width_;
    desc.BufferDesc.Height = height_;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 0;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    throw_if_failed(
        factory->CreateSwapChain(device_.native_device(), &desc, &swapchain_),
        "IDXGIFactory::CreateSwapChain");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
}

void D3D11Swapchain::release_backbuffer_texture() {
    if (backbuffer_texture_) {
        device_.destroy(backbuffer_texture_);
        backbuffer_texture_ = {};
    }
}

void D3D11Swapchain::refresh_backbuffer_texture() {
    release_backbuffer_texture();

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    throw_if_failed(
        swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backbuffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer");

    D3D11_TEXTURE2D_DESC native_desc{};
    backbuffer->GetDesc(&native_desc);
    width_ = native_desc.Width;
    height_ = native_desc.Height;

    TextureDesc desc;
    desc.width = native_desc.Width;
    desc.height = native_desc.Height;
    desc.format = PixelFormat::BGRA8_UNorm;
    desc.mip_levels = 1;
    desc.sample_count = native_desc.SampleDesc.Count;
    desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst;
    backbuffer_texture_ = device_.register_external_texture(
        reinterpret_cast<uintptr_t>(backbuffer.Get()),
        desc);
    if (!backbuffer_texture_) {
        throw std::runtime_error("D3D11Swapchain failed to register backbuffer texture");
    }
}

bool D3D11Swapchain::present(uint32_t sync_interval) {
    HRESULT hr = swapchain_->Present(sync_interval, 0);
    if (FAILED(hr)) {
        tc::Log::error("D3D11Swapchain::present failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

bool D3D11Swapchain::compose_and_present(TextureHandle color_texture, uint32_t sync_interval) {
    if (!color_texture || !backbuffer_texture_) {
        tc::Log::error("D3D11Swapchain::compose_and_present: invalid source or backbuffer texture");
        return false;
    }

    TextureDesc src_desc = device_.texture_desc(color_texture);
    if (src_desc.width == 0 || src_desc.height == 0) {
        tc::Log::error(
            "D3D11Swapchain::compose_and_present: invalid source texture id=%u",
            color_texture.id);
        return false;
    }
    const termin::Bounds2i destination = aspect_fit_rect(
        src_desc.width, src_desc.height, width_, height_);
    D3D11Texture* source = device_.get_texture(color_texture);
    D3D11Texture* target = device_.get_texture(backbuffer_texture_);
    if (!source || !source->texture || !target || !target->texture || !target->rtv) {
        tc::Log::error(
            "D3D11Swapchain::compose_and_present: stale native texture source=%u backbuffer=%u",
            color_texture.id,
            backbuffer_texture_.id);
        return false;
    }
    const bool exact_extent = destination.x0 == 0 && destination.y0 == 0 &&
                              destination.width() == static_cast<int>(width_) &&
                              destination.height() == static_cast<int>(height_);
    const bool raw_copy = exact_extent && src_desc.format == target->desc.format &&
                          src_desc.sample_count == target->desc.sample_count;
    if (!raw_copy && src_desc.sample_count == 1 && !source->srv) {
        tc::Log::error(
            "D3D11Swapchain::compose_and_present: source texture id=%u requires Sampled usage for composition",
            color_texture.id);
        return false;
    }
    if (!exact_extent) {
        device_.clear_texture(
            backbuffer_texture_,
            termin::Color4(0.0f, 0.0f, 0.0f, 1.0f),
            termin::Bounds2i::from_size(
                static_cast<int>(width_), static_cast<int>(height_)));
    }

    device_.blit_to_texture(
        backbuffer_texture_,
        color_texture,
        termin::Bounds2i::from_size(
            static_cast<int>(src_desc.width),
            static_cast<int>(src_desc.height)),
        destination);
    return present(sync_interval);
}

void D3D11Swapchain::resize(uint32_t width, uint32_t height) {
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    device_.wait_idle();
    release_backbuffer_texture();
    throw_if_failed(
        swapchain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0),
        "IDXGISwapChain::ResizeBuffers");
    refresh_backbuffer_texture();
}

} // namespace tgfx
