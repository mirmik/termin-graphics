#include <tgfx/tgfx2_interop.h>
#include <tgfx2/i_render_device.hpp>

#ifdef _WIN32
#include <tgfx2/d3d11/d3d11_render_device.hpp>
#include <tgfx2/d3d11/d3d11_swapchain.hpp>
#include <d3d9.h>
#include <dxgi.h>
#include <windows.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>

extern "C" {
#include <tcbase/tc_log.h>
}

static void* g_tgfx2_device = nullptr;
#ifdef _WIN32
namespace {

void tgfx2_log_hresult(const char* what, HRESULT hr) {
    tc_log(TC_LOG_ERROR, "%s failed: HRESULT=0x%08X", what, static_cast<unsigned>(hr));
}

bool tgfx2_env_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

bool tgfx2_read_pixel(
    tgfx::D3D11RenderDevice& device,
    tgfx::TextureHandle texture,
    int x,
    int y,
    float out_rgba[4]) {
    out_rgba[0] = out_rgba[1] = out_rgba[2] = out_rgba[3] = -1.0f;
    return texture && device.read_pixel_rgba8(texture, x, y, out_rgba);
}

void tgfx2_log_pixel_probe(
    tgfx::D3D11RenderDevice& device,
    uintptr_t bridge_id,
    uint64_t frame,
    const char* phase,
    tgfx::TextureHandle texture,
    const tgfx::TextureDesc& desc) {
    if (!texture || desc.width == 0 || desc.height == 0) {
        tc_log(TC_LOG_WARN,
               "[D3DImage:%p] frame=%llu %s texture invalid handle=%u size=%ux%u",
               reinterpret_cast<void*>(bridge_id),
               static_cast<unsigned long long>(frame),
               phase,
               texture.id,
               desc.width,
               desc.height);
        return;
    }

    const int x0 = static_cast<int>(desc.width / 2u);
    const int y0 = static_cast<int>(desc.height / 2u);
    const int x1 = static_cast<int>(desc.width / 4u);
    const int y1 = static_cast<int>(desc.height / 4u);
    const int x2 = static_cast<int>((desc.width * 3u) / 4u);
    const int y2 = static_cast<int>((desc.height * 3u) / 4u);
    float p0[4]{};
    float p1[4]{};
    float p2[4]{};
    const bool ok0 = tgfx2_read_pixel(device, texture, x0, y0, p0);
    const bool ok1 = tgfx2_read_pixel(device, texture, x1, y1, p1);
    const bool ok2 = tgfx2_read_pixel(device, texture, x2, y2, p2);
    tc_log(TC_LOG_INFO,
           "[D3DImage:%p] frame=%llu %s handle=%u size=%ux%u fmt=%d samples=%u "
           "p0(%d,%d)=%d %.3f %.3f %.3f %.3f "
           "p1(%d,%d)=%d %.3f %.3f %.3f %.3f "
           "p2(%d,%d)=%d %.3f %.3f %.3f %.3f",
           reinterpret_cast<void*>(bridge_id),
           static_cast<unsigned long long>(frame),
           phase,
           texture.id,
           desc.width,
           desc.height,
           static_cast<int>(desc.format),
           desc.sample_count,
           x0, y0, ok0 ? 1 : 0, p0[0], p0[1], p0[2], p0[3],
           x1, y1, ok1 ? 1 : 0, p1[0], p1[1], p1[2], p1[3],
           x2, y2, ok2 ? 1 : 0, p2[0], p2[1], p2[2], p2[3]);
}

class D3D9ImageRuntime {
public:
    D3D9ImageRuntime() {
        HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, d3d9_.GetAddressOf());
        if (FAILED(hr)) {
            tgfx2_log_hresult("Direct3DCreate9Ex", hr);
            throw std::runtime_error("D3D9ImageRuntime: Direct3DCreate9Ex failed");
        }

        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferWidth = 1;
        pp.BackBufferHeight = 1;
        pp.hDeviceWindow = GetDesktopWindow();
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        hr = d3d9_->CreateDeviceEx(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            GetDesktopWindow(),
            D3DCREATE_HARDWARE_VERTEXPROCESSING |
                D3DCREATE_MULTITHREADED |
                D3DCREATE_FPU_PRESERVE |
                D3DCREATE_NOWINDOWCHANGES,
            &pp,
            nullptr,
            d3d9_device_.GetAddressOf());
        if (FAILED(hr)) {
            tgfx2_log_hresult("IDirect3D9Ex::CreateDeviceEx", hr);
            throw std::runtime_error("D3D9ImageRuntime: CreateDeviceEx failed");
        }
    }

    IDirect3DDevice9Ex* device() const {
        return d3d9_device_.Get();
    }

private:
    Microsoft::WRL::ComPtr<IDirect3D9Ex> d3d9_;
    Microsoft::WRL::ComPtr<IDirect3DDevice9Ex> d3d9_device_;
};

std::shared_ptr<D3D9ImageRuntime> shared_d3d9_image_runtime() {
    static std::mutex mutex;
    static std::weak_ptr<D3D9ImageRuntime> weak_runtime;

    std::lock_guard<std::mutex> lock(mutex);
    if (auto runtime = weak_runtime.lock()) {
        return runtime;
    }

    auto runtime = std::make_shared<D3D9ImageRuntime>();
    weak_runtime = runtime;
    return runtime;
}

class D3D11D3DImageBridge {
public:
    D3D11D3DImageBridge(tgfx::D3D11RenderDevice& device, uint32_t width, uint32_t height)
        : device_(device), d3d9_runtime_(shared_d3d9_image_runtime()) {
        resize(width, height);
    }

    ~D3D11D3DImageBridge() {
        release_shared_resources();
    }

    D3D11D3DImageBridge(const D3D11D3DImageBridge&) = delete;
    D3D11D3DImageBridge& operator=(const D3D11D3DImageBridge&) = delete;

    bool resize(uint32_t width, uint32_t height) {
        width = width == 0 ? 1u : width;
        height = height == 0 ? 1u : height;
        if (width == width_ && height == height_ && d3d9_surface_) {
            return true;
        }

        release_shared_resources();
        width_ = width;
        height_ = height;

        D3D11_TEXTURE2D_DESC d3d11_desc{};
        d3d11_desc.Width = width_;
        d3d11_desc.Height = height_;
        d3d11_desc.MipLevels = 1;
        d3d11_desc.ArraySize = 1;
        d3d11_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d3d11_desc.SampleDesc.Count = 1;
        d3d11_desc.SampleDesc.Quality = 0;
        d3d11_desc.Usage = D3D11_USAGE_DEFAULT;
        d3d11_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d3d11_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        HRESULT hr = device_.native_device()->CreateTexture2D(
            &d3d11_desc,
            nullptr,
            d3d11_texture_.GetAddressOf());
        if (FAILED(hr)) {
            tgfx2_log_hresult("D3D11D3DImageBridge::CreateTexture2D", hr);
            release_shared_resources();
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
        hr = d3d11_texture_.As(&dxgi_resource);
        if (FAILED(hr)) {
            tgfx2_log_hresult("D3D11D3DImageBridge::QueryInterface(IDXGIResource)", hr);
            release_shared_resources();
            return false;
        }

        HANDLE shared_handle = nullptr;
        hr = dxgi_resource->GetSharedHandle(&shared_handle);
        if (FAILED(hr) || !shared_handle) {
            tgfx2_log_hresult("D3D11D3DImageBridge::GetSharedHandle", hr);
            release_shared_resources();
            return false;
        }

        HANDLE d3d9_shared_handle = shared_handle;
        hr = d3d9_runtime_->device()->CreateTexture(
            width_,
            height_,
            1,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            d3d9_texture_.GetAddressOf(),
            &d3d9_shared_handle);
        if (FAILED(hr)) {
            tgfx2_log_hresult("D3D11D3DImageBridge::IDirect3DDevice9Ex::CreateTexture", hr);
            release_shared_resources();
            return false;
        }

        hr = d3d9_texture_->GetSurfaceLevel(0, d3d9_surface_.GetAddressOf());
        if (FAILED(hr)) {
            tgfx2_log_hresult("D3D11D3DImageBridge::GetSurfaceLevel", hr);
            release_shared_resources();
            return false;
        }

        tgfx::TextureDesc desc{};
        desc.width = width_;
        desc.height = height_;
        desc.format = tgfx::PixelFormat::BGRA8_UNorm;
        desc.mip_levels = 1;
        desc.sample_count = 1;
        desc.usage = tgfx::TextureUsage::ColorAttachment |
                     tgfx::TextureUsage::Sampled |
                     tgfx::TextureUsage::CopyDst;
        shared_texture_handle_ = device_.register_external_texture(reinterpret_cast<uintptr_t>(d3d11_texture_.Get()), desc);
        if (!shared_texture_handle_) {
            tc_log(TC_LOG_ERROR, "D3D11D3DImageBridge: failed to register shared texture");
            release_shared_resources();
            return false;
        }

        if (tgfx2_env_enabled("TERMIN_D3DIMAGE_TRACE")) {
            tc_log(TC_LOG_INFO,
                   "[D3DImage:%p] resize/create size=%ux%u shared_tgfx=%u d3d11_tex=%p d3d9_surface=%p",
                   this,
                   width_,
                   height_,
                   shared_texture_handle_.id,
                   d3d11_texture_.Get(),
                   d3d9_surface_.Get());
        }

        return true;
    }

    bool present(tgfx::TextureHandle source) {
        if (!source || !shared_texture_handle_) {
            tc_log(TC_LOG_ERROR, "D3D11D3DImageBridge::present: invalid source or shared texture");
            return false;
        }

        const tgfx::TextureDesc src_desc = device_.texture_desc(source);
        if (src_desc.width != width_ || src_desc.height != height_) {
            tc_log(TC_LOG_ERROR,
                   "D3D11D3DImageBridge::present: source size %ux%u does not match bridge %ux%u",
                   src_desc.width,
                   src_desc.height,
                   width_,
                   height_);
            return false;
        }

        ++present_count_;
        const bool trace = tgfx2_env_enabled("TERMIN_D3DIMAGE_TRACE");
        const bool probe = tgfx2_env_enabled("TERMIN_D3DIMAGE_PROBE_PIXELS");
        const bool log_frame = trace && (present_count_ <= 8 || (present_count_ % 120u) == 0u);
        if (log_frame) {
            tc_log(TC_LOG_INFO,
                   "[D3DImage:%p] frame=%llu present src=%u src_size=%ux%u src_fmt=%d src_samples=%u dst=%u dst_size=%ux%u",
                   this,
                   static_cast<unsigned long long>(present_count_),
                   source.id,
                   src_desc.width,
                   src_desc.height,
                   static_cast<int>(src_desc.format),
                   src_desc.sample_count,
                   shared_texture_handle_.id,
                   width_,
                   height_);
        }
        if (probe && log_frame) {
            tgfx2_log_pixel_probe(device_, reinterpret_cast<uintptr_t>(this), present_count_, "src-before", source, src_desc);
        }

        device_.blit_to_texture(
            shared_texture_handle_,
            source,
            0, 0, static_cast<int>(src_desc.width), static_cast<int>(src_desc.height),
            0, 0, static_cast<int>(width_), static_cast<int>(height_));
        if (probe && log_frame) {
            const tgfx::TextureDesc dst_desc = device_.texture_desc(shared_texture_handle_);
            tgfx2_log_pixel_probe(device_, reinterpret_cast<uintptr_t>(this), present_count_, "dst-after", shared_texture_handle_, dst_desc);
        }
        // D3DImage presentation uses the same immediate D3D11 context as the
        // plot renderers. Keep the bridge from leaking its blit shaders,
        // resource bindings, and render-target state into the next WPF plot.
        device_.reset_state();
        device_.wait_idle();
        return true;
    }

    IDirect3DSurface9* surface() const {
        return d3d9_surface_.Get();
    }

private:
    void release_shared_resources() {
        if (shared_texture_handle_) {
            device_.destroy(shared_texture_handle_);
            shared_texture_handle_ = {};
        }
        d3d9_surface_.Reset();
        d3d9_texture_.Reset();
        d3d11_texture_.Reset();
    }

    tgfx::D3D11RenderDevice& device_;
    std::shared_ptr<D3D9ImageRuntime> d3d9_runtime_;
    Microsoft::WRL::ComPtr<IDirect3DTexture9> d3d9_texture_;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> d3d9_surface_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11_texture_;
    tgfx::TextureHandle shared_texture_handle_;
    uint64_t present_count_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

D3D11D3DImageBridge* bridge_from_void(void* bridge) {
    return static_cast<D3D11D3DImageBridge*>(bridge);
}

} // namespace
#endif

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
void* tgfx2_interop_create_d3d11_d3dimage_bridge(
    uint32_t width,
    uint32_t height)
{
#ifdef _WIN32
    if (width == 0 || height == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3DImage bridge request: size=%ux%u",
               width, height);
        return nullptr;
    }

    auto* device = static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
    if (!device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot create D3DImage bridge: no active tgfx2 device");
        return nullptr;
    }
    if (device->backend_type() != tgfx::BackendType::D3D11) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] cannot create D3DImage bridge: active backend is not D3D11");
        return nullptr;
    }

    auto* d3d_device = dynamic_cast<tgfx::D3D11RenderDevice*>(device);
    if (!d3d_device) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] active tgfx2 device reports D3D11 but is not D3D11RenderDevice");
        return nullptr;
    }

    try {
        return new D3D11D3DImageBridge(*d3d_device, width, height);
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to create D3DImage bridge: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to create D3DImage bridge: unknown error");
    }
    return nullptr;
#else
    (void)width; (void)height;
    tc_log(TC_LOG_ERROR,
           "[tgfx2_interop] D3DImage bridge creation is only supported on Windows");
    return nullptr;
#endif
}

void tgfx2_interop_destroy_d3d11_d3dimage_bridge(void* bridge) {
#ifdef _WIN32
    delete bridge_from_void(bridge);
#else
    (void)bridge;
#endif
}

int tgfx2_interop_resize_d3d11_d3dimage_bridge(
    void* bridge,
    uint32_t width,
    uint32_t height)
{
#ifdef _WIN32
    if (!bridge || width == 0 || height == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3DImage bridge resize: bridge=%p size=%ux%u",
               bridge, width, height);
        return 0;
    }

    try {
        return bridge_from_void(bridge)->resize(width, height) ? 1 : 0;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to resize D3DImage bridge: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to resize D3DImage bridge: unknown error");
    }
    return 0;
#else
    (void)bridge; (void)width; (void)height;
    return 0;
#endif
}

int tgfx2_interop_present_d3d11_d3dimage_bridge(
    void* bridge,
    uint32_t source_handle_id)
{
#ifdef _WIN32
    if (!bridge || source_handle_id == 0) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] invalid D3DImage bridge present: bridge=%p source=%u",
               bridge, source_handle_id);
        return 0;
    }

    try {
        return bridge_from_void(bridge)->present(tgfx::TextureHandle{source_handle_id}) ? 1 : 0;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to present D3DImage bridge: %s",
               e.what());
    } catch (...) {
        tc_log(TC_LOG_ERROR,
               "[tgfx2_interop] failed to present D3DImage bridge: unknown error");
    }
    return 0;
#else
    (void)bridge; (void)source_handle_id;
    return 0;
#endif
}

void* tgfx2_interop_get_d3d11_d3dimage_surface(void* bridge) {
#ifdef _WIN32
    if (!bridge) {
        return nullptr;
    }
    return bridge_from_void(bridge)->surface();
#else
    (void)bridge;
    return nullptr;
#endif
}
