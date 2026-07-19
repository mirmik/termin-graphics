// sdl_backend_window.cpp - SDL window + IRenderDevice wrapper.
#include "termin/platform/sdl_backend_window.hpp"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#ifdef TGFX2_HAS_OPENGL
#include "tgfx2/opengl/opengl_render_device.hpp"
#endif
#include "tgfx2/render_context.hpp"
#include "tgfx2/render_runtime.hpp"
#include "tgfx/tgfx2_interop.h"
#ifdef TGFX2_HAS_D3D11
#include <SDL2/SDL_syswm.h>
#include "tgfx2/d3d11/d3d11_render_device.hpp"
#include "tgfx2/d3d11/d3d11_swapchain.hpp"
#endif

extern "C" {
#include <tcbase/tc_log.h>
}

#ifdef TGFX2_HAS_VULKAN
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#endif

namespace termin {

namespace {

void configure_sdl_window_hints() {
#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif
}

uint32_t event_window_id(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_WINDOWEVENT:
            return ev.window.windowID;
        case SDL_MOUSEMOTION:
            return ev.motion.windowID;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return ev.button.windowID;
        case SDL_MOUSEWHEEL:
            return ev.wheel.windowID;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            return ev.key.windowID;
        case SDL_TEXTINPUT:
            return ev.text.windowID;
        default:
            return 0;
    }
}

#ifdef TGFX2_HAS_D3D11
HWND get_sdl_hwnd(SDL_Window* window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());
    }
    if (!info.info.win.window) {
        throw std::runtime_error("SDL_GetWindowWMInfo returned an empty Win32 window handle");
    }
    return info.info.win.window;
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// Impl — private backend state. Kept out of the header so apps don't
// need to know about SDL_GLContext / VkInstance / etc.
// ---------------------------------------------------------------------------

struct SDLBackendWindow::Impl {
    tgfx::BackendType backend = tgfx::BackendType::OpenGL;
    tgfx::PresentationMode requested_presentation_mode = tgfx::PresentationMode::VSync;
    tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync;
    // Owned GPU runtime for primary windows; left empty on secondary
    // windows (they point `device_ref` at the primary's device instead).
    std::unique_ptr<tgfx::RenderRuntime> runtime;
    // Non-owning pointer used by the rest of the class. Always set —
    // equals runtime->device() for primary, or the shared device for
    // secondary.
    tgfx::IRenderDevice* device_ref = nullptr;
    // Primary the secondary window borrows its device/context from.
    // nullptr on primaries. Used so context() / device() return the
    // same objects everywhere.
    SDLBackendWindow* shared_ctx_owner = nullptr;

    // OpenGL state (only used when backend == OpenGL).
    SDL_GLContext gl_context = nullptr;

    // Vulkan state for secondary windows — each one owns its own
    // swapchain bound to its own VkSurfaceKHR. Primary windows have
    // the surface/swapchain owned by VulkanRenderDevice itself and
    // leave these empty.
#ifdef TGFX2_HAS_VULKAN
    VkSurfaceKHR secondary_surface = VK_NULL_HANDLE;
    std::unique_ptr<tgfx::VulkanSwapchain> secondary_swapchain;
#endif

#ifdef TGFX2_HAS_D3D11
    // D3D11 keeps swapchains outside the device so primary and
    // secondary windows share the same device model.
    std::unique_ptr<tgfx::D3D11Swapchain> d3d11_swapchain;
#endif
};

// ---------------------------------------------------------------------------
// Ctor / dtor
// ---------------------------------------------------------------------------

SDLBackendWindow::SDLBackendWindow(
    const std::string& title,
    int width,
    int height,
    tgfx::PresentationMode presentation_mode)
    : impl_(std::make_unique<Impl>())
{
    impl_->requested_presentation_mode = presentation_mode;
    impl_->presentation_mode = presentation_mode;
    if (tgfx2_interop_get_device() != nullptr) {
        throw std::runtime_error(
            "BackendWindow(primary): an application graphics device is already installed; "
            "create a secondary window sharing the primary instead");
    }
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_InitSubSystem failed: ") + SDL_GetError());
    }
    configure_sdl_window_hints();

    impl_->backend = tgfx::default_backend_from_env();

    if (impl_->backend == tgfx::BackendType::OpenGL) {
#ifdef TGFX2_HAS_OPENGL
        // GL 4.3 core — gives us `layout(binding=N)` on UBOs (GL 4.2+,
        // needed for the push-constants ring UBO trick at binding 14)
        // plus `layout(location=N)` on varyings (GL 4.1+). The single
        // shader source that compiles on Vulkan SPIR-V via shaderc
        // (`#version 450`) then compiles on this GL context too —
        // `#version 450 core` is a subset of what 4.3 accepts. macOS
        // is a loss at core-profile 4.3, but the cross-backend story
        // is unusable on macOS anyway (no Vulkan natively).
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        impl_->gl_context = SDL_GL_CreateContext(window_);
        if (!impl_->gl_context) {
            SDL_DestroyWindow(window_);
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }
        SDL_GL_MakeCurrent(window_, impl_->gl_context);
        const int swap_interval = presentation_mode == tgfx::PresentationMode::VSync ? 1 : 0;
        if (SDL_GL_SetSwapInterval(swap_interval) != 0) {
            tc_log_warn(
                "BackendWindow: SDL_GL_SetSwapInterval(%d) is unsupported: %s; "
                "continuing with the driver's active interval",
                swap_interval,
                SDL_GetError());
        }
        const int applied_swap_interval = SDL_GL_GetSwapInterval();
        impl_->presentation_mode = applied_swap_interval == 0
            ? tgfx::PresentationMode::Immediate
            : tgfx::PresentationMode::VSync;
        if (impl_->presentation_mode != presentation_mode) {
            tc_log_warn(
                "BackendWindow: requested OpenGL presentation mode '%s', but the "
                "driver applied '%s' (swap interval %d)",
                presentation_mode == tgfx::PresentationMode::VSync ? "vsync" : "immediate",
                impl_->presentation_mode == tgfx::PresentationMode::VSync ? "vsync" : "immediate",
                applied_swap_interval);
        }

        // OpenGLRenderDevice ctor loads GLAD and validates the live
        // context — it expects MakeCurrent to already have happened.
        impl_->runtime = tgfx::RenderRuntime::create(tgfx::BackendType::OpenGL);
        impl_->device_ref = &impl_->runtime->device();

#else
        throw std::runtime_error("BackendWindow: OpenGL backend not compiled");
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(Vulkan) failed: ") + SDL_GetError());
        }

        uint32_t ext_count = 0;
        SDL_Vulkan_GetInstanceExtensions(window_, &ext_count, nullptr);
        std::vector<const char*> extensions(ext_count);
        SDL_Vulkan_GetInstanceExtensions(window_, &ext_count, extensions.data());

        int fb_w = 0, fb_h = 0;
        SDL_Vulkan_GetDrawableSize(window_, &fb_w, &fb_h);

        tgfx::VulkanDeviceCreateInfo info;
        const char* validation_env = std::getenv("TGFX2_VULKAN_VALIDATION");
        info.enable_validation = (validation_env && validation_env[0] == '1');
        info.instance_extensions = extensions;
        info.swapchain_width = static_cast<uint32_t>(fb_w);
        info.swapchain_height = static_cast<uint32_t>(fb_h);
        info.presentation_mode = presentation_mode;
        SDL_Window* win = window_;
        info.surface_factory = [win](VkInstance inst) -> VkSurfaceKHR {
            VkSurfaceKHR surf = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(win, inst, &surf)) {
                return VK_NULL_HANDLE;
            }
            return surf;
        };

        impl_->runtime = std::make_unique<tgfx::RenderRuntime>(
            std::make_unique<tgfx::VulkanRenderDevice>(info));
        impl_->device_ref = &impl_->runtime->device();
    }
#endif
#ifdef TGFX2_HAS_D3D11
    else if (impl_->backend == tgfx::BackendType::D3D11) {
        if (presentation_mode != tgfx::PresentationMode::VSync) {
            tc_log_warn(
                "BackendWindow: D3D11 Immediate presentation is unsupported until "
                "the flip-model tearing path is implemented; continuing with VSync");
            impl_->presentation_mode = tgfx::PresentationMode::VSync;
        }
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(D3D11) failed: ") +
                                     SDL_GetError());
        }

        int fb_w = 0, fb_h = 0;
        SDL_GetWindowSize(window_, &fb_w, &fb_h);
        if (fb_w <= 0 || fb_h <= 0) {
            fb_w = width;
            fb_h = height;
        }

        auto d3d_device = std::make_unique<tgfx::D3D11RenderDevice>();
        auto* d3d_device_ref = d3d_device.get();
        impl_->runtime = std::make_unique<tgfx::RenderRuntime>(std::move(d3d_device));
        impl_->device_ref = &impl_->runtime->device();
        impl_->d3d11_swapchain = std::make_unique<tgfx::D3D11Swapchain>(
            *d3d_device_ref,
            get_sdl_hwnd(window_),
            static_cast<uint32_t>(fb_w),
            static_cast<uint32_t>(fb_h));
    }
#endif
    else {
        SDL_DestroyWindow(window_);
        throw std::runtime_error("BackendWindow: unsupported backend");
    }
    impl_->runtime->claim_interop();
}

// ---------------------------------------------------------------------------
// Secondary window — reuses the primary's IRenderDevice, adds its own
// OS window + surface/GL-context + (Vulkan) swapchain. The whole point
// is "one IRenderDevice per process" — secondary windows must not spin
// up their own devices.
// ---------------------------------------------------------------------------

SDLBackendWindow::SDLBackendWindow(const std::string& title, int width, int height,
                              SDLBackendWindow& share_with)
    : impl_(std::make_unique<Impl>())
{
    if (!share_with.impl_->device_ref) {
        throw std::runtime_error(
            "BackendWindow(secondary): primary window has no device");
    }
    impl_->backend = share_with.impl_->backend;
    impl_->requested_presentation_mode = share_with.impl_->requested_presentation_mode;
    impl_->presentation_mode = share_with.impl_->presentation_mode;
    impl_->device_ref = share_with.impl_->device_ref;
    impl_->shared_ctx_owner = &share_with;
    configure_sdl_window_hints();

    if (impl_->backend == tgfx::BackendType::OpenGL) {
#ifdef TGFX2_HAS_OPENGL
        // Secondary GL windows don't get their own GL context — they
        // borrow the primary's. One context can be made current against
        // any of its owner's compatible windows, which is exactly what
        // we need here: no share-group complexity, no second set of
        // cached FBOs, no risk of desynchronised GLAD function tables.
        // present() will MakeCurrent the primary's context against
        // this window before blitting + SwapWindow.
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(secondary) failed: ") +
                                     SDL_GetError());
        }
        // impl_->gl_context stays null — the dtor skips SDL_GL_DeleteContext.

        // Re-make the primary context current. Creating a new
        // SDL_WINDOW_OPENGL window can unbind the current GL context
        // on some drivers (Mesa/GLX in particular), after which any
        // GL call dispatched through GLAD hits a null function pointer
        // and segfaults.
        SDL_GL_MakeCurrent(share_with.window_, share_with.impl_->gl_context);
#else
        throw std::runtime_error("BackendWindow(secondary): OpenGL backend not compiled");
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(Vulkan secondary) failed: ") +
                                     SDL_GetError());
        }

        auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window_, vk_dev->instance(), &surf)) {
            SDL_DestroyWindow(window_);
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface(secondary) failed: ") +
                                     SDL_GetError());
        }
        impl_->secondary_surface = surf;

        int fb_w = 0, fb_h = 0;
        SDL_Vulkan_GetDrawableSize(window_, &fb_w, &fb_h);
        impl_->secondary_swapchain = std::make_unique<tgfx::VulkanSwapchain>(
            *vk_dev, surf,
            static_cast<uint32_t>(fb_w),
            static_cast<uint32_t>(fb_h),
            impl_->presentation_mode);
    }
#endif
#ifdef TGFX2_HAS_D3D11
    else if (impl_->backend == tgfx::BackendType::D3D11) {
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(D3D11 secondary) failed: ") +
                                     SDL_GetError());
        }

        int fb_w = 0, fb_h = 0;
        SDL_GetWindowSize(window_, &fb_w, &fb_h);
        if (fb_w <= 0 || fb_h <= 0) {
            fb_w = width;
            fb_h = height;
        }
        auto* d3d_dev = static_cast<tgfx::D3D11RenderDevice*>(impl_->device_ref);
        impl_->d3d11_swapchain = std::make_unique<tgfx::D3D11Swapchain>(
            *d3d_dev,
            get_sdl_hwnd(window_),
            static_cast<uint32_t>(fb_w),
            static_cast<uint32_t>(fb_h));
    }
#endif
    else {
        SDL_DestroyWindow(window_);
        throw std::runtime_error("BackendWindow(secondary): unsupported backend");
    }
}

void SDLBackendWindow::maximize() {
    if (window_) {
        SDL_MaximizeWindow(window_);
    }
}

void SDLBackendWindow::set_icon_bmp(const std::string& path) {
    if (!window_) {
        tc_log_error("[SDLBackendWindow] cannot set icon on a closed window");
        throw std::runtime_error("SDLBackendWindow::set_icon_bmp: window is closed");
    }

    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> icon(
        SDL_LoadBMP(path.c_str()), SDL_FreeSurface);
    if (!icon) {
        const std::string message =
            "SDLBackendWindow::set_icon_bmp failed for '" + path + "': " + SDL_GetError();
        tc_log_error("[SDLBackendWindow] %s", message.c_str());
        throw std::runtime_error(message);
    }

    SDL_SetWindowIcon(window_, icon.get());
}

void SDLBackendWindow::set_fullscreen(bool enabled) {
    if (!window_) {
        return;
    }
    const Uint32 flags = enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    if (SDL_SetWindowFullscreen(window_, flags) != 0) {
        tc_log(TC_LOG_ERROR, "[BackendWindow] SDL_SetWindowFullscreen failed: %s", SDL_GetError());
    }
}

void SDLBackendWindow::set_always_on_top(bool enabled) {
    if (!window_) {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 16)
    SDL_SetWindowAlwaysOnTop(window_, enabled ? SDL_TRUE : SDL_FALSE);
#else
    (void)enabled;
#endif
}

void SDLBackendWindow::close() {
    // Idempotent teardown — callers (both the dtor and Python
    // `window.close()`) can invoke this without checking state first.
    if (!window_ && !impl_->device_ref && !impl_->gl_context) {
        return;
    }

    // Teardown in reverse dependency order. Secondary windows skip the
    // context/device reset because those are owned by the primary —
    // tearing them down here would yank the rug out from under the
    // primary and every other secondary.
#ifdef TGFX2_HAS_D3D11
    if (impl_->d3d11_swapchain) {
        if (impl_->device_ref) {
            impl_->device_ref->wait_idle();
        }
        impl_->d3d11_swapchain.reset();
    }
#endif
#ifdef TGFX2_HAS_VULKAN
    // Vulkan secondary: wait idle before destroying the swapchain /
    // surface to avoid killing resources still in flight.
    if (impl_->secondary_swapchain) {
        if (impl_->device_ref) {
            impl_->device_ref->wait_idle();
        }
        impl_->secondary_swapchain.reset();
    }
    if (impl_->secondary_surface != VK_NULL_HANDLE && impl_->device_ref) {
        auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
        vkDestroySurfaceKHR(vk_dev->instance(), impl_->secondary_surface, nullptr);
        impl_->secondary_surface = VK_NULL_HANDLE;
    }
#endif
    if (impl_->shared_ctx_owner == nullptr) {
        // Primary window — RenderRuntime owns the device/ctx/cache chain and
        // releases its explicit process-wide interop claim before teardown.
        impl_->runtime.reset();
    }
    // Shared devices still accessed through device_ref; stop using it.
    impl_->device_ref = nullptr;
    impl_->shared_ctx_owner = nullptr;

    if (impl_->gl_context) {
        SDL_GL_DeleteContext(impl_->gl_context);
        impl_->gl_context = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    should_close_ = true;
}

SDLBackendWindow::~SDLBackendWindow() {
    close();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

tgfx::IRenderDevice* SDLBackendWindow::device() {
    return impl_->device_ref;
}

tgfx::BackendType SDLBackendWindow::backend_type() const {
    return impl_->backend;
}

tgfx::PresentationMode SDLBackendWindow::requested_presentation_mode() const {
    return impl_->requested_presentation_mode;
}

tgfx::PresentationMode SDLBackendWindow::presentation_mode() const {
    return impl_->presentation_mode;
}

tgfx::RenderContext2* SDLBackendWindow::context() {
    // Secondary windows never build their own RenderContext2 — that
    // would defeat "one PipelineCache per device" and force redundant
    // pipeline compilations. They forward to the primary instead.
    if (impl_->shared_ctx_owner) {
        return impl_->shared_ctx_owner->context();
    }
    if (!impl_->runtime) {
        return nullptr;
    }
    return &impl_->runtime->context();
}

std::pair<int, int> SDLBackendWindow::framebuffer_size() const {
    if (!window_) return {0, 0};
    int w = 0, h = 0;
    if (impl_->backend == tgfx::BackendType::OpenGL) {
        SDL_GL_GetDrawableSize(window_, &w, &h);
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        SDL_Vulkan_GetDrawableSize(window_, &w, &h);
    }
#endif
    else {
        SDL_GetWindowSize(window_, &w, &h);
    }
    return {w, h};
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool SDLBackendWindow::poll_event(SDL_Event& out_event) {
    return SDL_PollEvent(&out_event) != 0;
}

void SDLBackendWindow::poll_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            should_close_ = true;
            continue;
        }

        const uint32_t own_window_id = window_ ? SDL_GetWindowID(window_) : 0;
        const uint32_t ev_window_id = event_window_id(ev);
        if (ev_window_id != 0 && own_window_id != 0 && ev_window_id != own_window_id) {
            continue;
        }

        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
            should_close_ = true;
            continue;
        }

        if (event_handler_) {
            event_handler_(ev);
        }
    }
}

// ---------------------------------------------------------------------------
// Present
// ---------------------------------------------------------------------------

void SDLBackendWindow::present(tgfx::TextureHandle color_tex) {
    if (!impl_->device_ref || !window_) return;

    auto [w, h] = framebuffer_size();
    if (w <= 0 || h <= 0) return;

    if (impl_->backend == tgfx::BackendType::OpenGL) {
#ifdef TGFX2_HAS_OPENGL
        // Secondary windows borrow the primary's GL context. Find it:
        // either we own it (primary) or the shared owner does.
        SDL_GLContext gl_ctx = impl_->gl_context
            ? impl_->gl_context
            : (impl_->shared_ctx_owner
                ? impl_->shared_ctx_owner->impl_->gl_context
                : nullptr);
        if (!gl_ctx) {
            tc_log(TC_LOG_ERROR, "[BackendWindow] present: no GL context");
            return;
        }
        SDL_GL_MakeCurrent(window_, gl_ctx);

        auto* gl_dev = static_cast<tgfx::OpenGLRenderDevice*>(impl_->device_ref);
        gl_dev->present_to_default_framebuffer(color_tex, w, h);
        SDL_GL_SwapWindow(window_);
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
        // Secondary windows carry their own swapchain; primary windows
        // use the one owned by VulkanRenderDevice. Either way, pick
        // the one that belongs to this window.
        tgfx::VulkanSwapchain* sc = impl_->secondary_swapchain
                                        ? impl_->secondary_swapchain.get()
                                        : vk_dev->swapchain();
        if (!sc) {
            tc_log(TC_LOG_ERROR, "[BackendWindow] present: no Vulkan swapchain");
            return;
        }

        // If the window drawable size changed (resize event), recreate
        // the swapchain before acquiring.
        if (sc->width() != static_cast<uint32_t>(w) ||
            sc->height() != static_cast<uint32_t>(h)) {
            sc->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }

        if (sc->compose_and_present(color_tex)) {
            // OUT_OF_DATE / SUBOPTIMAL after present — schedule
            // recreate for next frame by resampling current size.
            int nw = 0, nh = 0;
            SDL_Vulkan_GetDrawableSize(window_, &nw, &nh);
            sc->recreate(static_cast<uint32_t>(nw), static_cast<uint32_t>(nh));
        }
    }
#endif
#ifdef TGFX2_HAS_D3D11
    else if (impl_->backend == tgfx::BackendType::D3D11) {
        tgfx::D3D11Swapchain* sc = impl_->d3d11_swapchain.get();
        if (!sc) {
            tc_log(TC_LOG_ERROR, "[BackendWindow] present: no D3D11 swapchain");
            return;
        }

        if (sc->width() != static_cast<uint32_t>(w) ||
            sc->height() != static_cast<uint32_t>(h)) {
            sc->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }

        if (!sc->compose_and_present(color_tex)) {
            tc_log(TC_LOG_ERROR, "[BackendWindow] present: D3D11 present failed");
        }
    }
#endif
}

} // namespace termin
