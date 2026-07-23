// sdl_backend_window.cpp - SDL presentation windows on a host-owned device.
#include "termin/platform/sdl_backend_window.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#ifdef TGFX2_HAS_OPENGL
#include "tgfx2/opengl/opengl_render_device.hpp"
#endif
#include "tgfx2/render_context.hpp"
#include "tgfx2/graphics_host.hpp"
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

using SDLWindowEventQueue = std::deque<SDL_Event>;

std::unordered_map<uint32_t, SDLWindowEventQueue*>& window_event_queues() {
    static std::unordered_map<uint32_t, SDLWindowEventQueue*> queues;
    return queues;
}

size_t& text_input_user_count() {
    static size_t count = 0;
    return count;
}

struct SystemCursorStore {
    std::array<SDL_Cursor*, 9> cursors{};

    ~SystemCursorStore() {
        SDL_SetCursor(SDL_GetDefaultCursor());
        for (SDL_Cursor* cursor : cursors) {
            if (cursor) SDL_FreeCursor(cursor);
        }
    }
};

SystemCursorStore& system_cursor_store() {
    static SystemCursorStore store;
    return store;
}

void register_window_event_queue(SDL_Window* window, SDLWindowEventQueue& queue) {
    if (!window) return;
    window_event_queues()[SDL_GetWindowID(window)] = &queue;
}

void unregister_window_event_queue(SDL_Window* window) {
    if (!window) return;
    window_event_queues().erase(SDL_GetWindowID(window));
}

bool next_window_event(
    SDL_Window* window,
    SDLWindowEventQueue& own_queue,
    SDL_Event& out_event) {
    if (!own_queue.empty()) {
        out_event = own_queue.front();
        own_queue.pop_front();
        return true;
    }

    const uint32_t own_window_id = window ? SDL_GetWindowID(window) : 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            for (const auto& [window_id, queue] : window_event_queues()) {
                if (window_id != own_window_id && queue) {
                    queue->push_back(event);
                }
            }
            out_event = event;
            return true;
        }

        const uint32_t target_window_id = event_window_id(event);
        if (target_window_id == 0 || target_window_id == own_window_id) {
            out_event = event;
            return true;
        }

        const auto target = window_event_queues().find(target_window_id);
        if (target != window_event_queues().end() && target->second) {
            target->second->push_back(event);
        }
        // Keep pumping: an event for another registered window must not make
        // the current window appear idle before its own queued events arrive.
    }
    return false;
}

uint32_t translate_modifiers(SDL_Keymod modifiers) {
    uint32_t result = WindowModifierNone;
    if ((modifiers & KMOD_SHIFT) != 0) result |= WindowModifierShift;
    if ((modifiers & KMOD_CTRL) != 0) result |= WindowModifierControl;
    if ((modifiers & KMOD_ALT) != 0) result |= WindowModifierAlt;
    if ((modifiers & KMOD_GUI) != 0) result |= WindowModifierSuper;
    return result;
}

tcbase::MouseButton translate_pointer_button(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return tcbase::MouseButton::LEFT;
        case SDL_BUTTON_RIGHT: return tcbase::MouseButton::RIGHT;
        case SDL_BUTTON_MIDDLE: return tcbase::MouseButton::MIDDLE;
        default: return tcbase::MouseButton::OTHER;
    }
}

WindowKey translate_key(SDL_Scancode key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
        return static_cast<WindowKey>(
            static_cast<uint16_t>(WindowKey::A) +
            static_cast<uint16_t>(key - SDL_SCANCODE_A));
    }
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) {
        return static_cast<WindowKey>(
            static_cast<uint16_t>(WindowKey::Digit1) +
            static_cast<uint16_t>(key - SDL_SCANCODE_1));
    }
    switch (key) {
        case SDL_SCANCODE_0: return WindowKey::Digit0;
        case SDL_SCANCODE_TAB: return WindowKey::Tab;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return WindowKey::Enter;
        case SDL_SCANCODE_SPACE: return WindowKey::Space;
        case SDL_SCANCODE_ESCAPE: return WindowKey::Escape;
        case SDL_SCANCODE_BACKSPACE: return WindowKey::Backspace;
        case SDL_SCANCODE_DELETE: return WindowKey::Delete;
        case SDL_SCANCODE_RIGHT: return WindowKey::Right;
        case SDL_SCANCODE_LEFT: return WindowKey::Left;
        case SDL_SCANCODE_DOWN: return WindowKey::Down;
        case SDL_SCANCODE_UP: return WindowKey::Up;
        case SDL_SCANCODE_HOME: return WindowKey::Home;
        case SDL_SCANCODE_END: return WindowKey::End;
        case SDL_SCANCODE_INSERT: return WindowKey::Insert;
        case SDL_SCANCODE_PAGEUP: return WindowKey::PageUp;
        case SDL_SCANCODE_PAGEDOWN: return WindowKey::PageDown;
        case SDL_SCANCODE_F1: return WindowKey::F1;
        case SDL_SCANCODE_F2: return WindowKey::F2;
        case SDL_SCANCODE_F3: return WindowKey::F3;
        case SDL_SCANCODE_F4: return WindowKey::F4;
        case SDL_SCANCODE_F5: return WindowKey::F5;
        case SDL_SCANCODE_F6: return WindowKey::F6;
        case SDL_SCANCODE_F7: return WindowKey::F7;
        case SDL_SCANCODE_F8: return WindowKey::F8;
        case SDL_SCANCODE_F9: return WindowKey::F9;
        case SDL_SCANCODE_F10: return WindowKey::F10;
        case SDL_SCANCODE_F11: return WindowKey::F11;
        case SDL_SCANCODE_F12: return WindowKey::F12;
        default: return WindowKey::Unknown;
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

struct SDLWindowSystem::Impl {
    tgfx::BackendType backend = tgfx::BackendType::OpenGL;
    std::unique_ptr<tgfx::IRenderDevice> prepared_device;
    tgfx::GraphicsHost* graphics_host = nullptr;
    SDL_Window* gl_bootstrap_window = nullptr;
    SDL_GLContext gl_context = nullptr;
    size_t live_windows = 0;
    bool closed = false;

    ~Impl() {
        release_bootstrap_resources();
    }

    void release_bootstrap_resources() noexcept {
        // The device must die while its platform context is still valid.
        prepared_device.reset();
        if (gl_context) {
            SDL_GL_DeleteContext(gl_context);
            gl_context = nullptr;
        }
        if (gl_bootstrap_window) {
            SDL_DestroyWindow(gl_bootstrap_window);
            gl_bootstrap_window = nullptr;
        }
    }

    void abandon_bootstrap_resources() noexcept {
        // A lifetime violation may leave presentation windows referring to
        // these resources. Do not tear their dependencies down underneath
        // them; the error is already terminal and has been logged.
        (void)prepared_device.release();
        gl_context = nullptr;
        gl_bootstrap_window = nullptr;
    }
};

struct SDLBackendWindow::Impl {
    SDLWindowSystem* window_system = nullptr;
    tgfx::GraphicsHost* graphics_host = nullptr;
    tgfx::BackendType backend = tgfx::BackendType::OpenGL;
    tgfx::PresentationMode requested_presentation_mode = tgfx::PresentationMode::VSync;
    tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync;
    tgfx::IRenderDevice* device_ref = nullptr;
    SDLWindowEventQueue pending_events;

#ifdef TGFX2_HAS_VULKAN
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<tgfx::VulkanSwapchain> swapchain;
#endif

#ifdef TGFX2_HAS_D3D11
    // D3D11 keeps swapchains outside the device so primary and
    // secondary windows share the same device model.
    std::unique_ptr<tgfx::D3D11Swapchain> d3d11_swapchain;
#endif
};

// ---------------------------------------------------------------------------
// Host graphics runtime
// ---------------------------------------------------------------------------

SDLWindowSystem::SDLWindowSystem()
    : impl_(std::make_unique<Impl>())
{
    if (tgfx2_interop_get_device() != nullptr) {
        throw std::runtime_error(
            "SDLWindowSystem: an application graphics device is already installed");
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

        const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
        impl_->gl_bootstrap_window = SDL_CreateWindow(
            "Termin graphics runtime",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            1,
            1,
            flags);
        if (!impl_->gl_bootstrap_window) {
            throw std::runtime_error(
                std::string("SDLWindowSystem bootstrap window failed: ") + SDL_GetError());
        }

        impl_->gl_context = SDL_GL_CreateContext(impl_->gl_bootstrap_window);
        if (!impl_->gl_context) {
            SDL_DestroyWindow(impl_->gl_bootstrap_window);
            impl_->gl_bootstrap_window = nullptr;
            throw std::runtime_error(
                std::string("SDLWindowSystem GL context failed: ") + SDL_GetError());
        }
        SDL_GL_MakeCurrent(impl_->gl_bootstrap_window, impl_->gl_context);

        // OpenGLRenderDevice ctor loads GLAD and validates the live
        // context — it expects MakeCurrent to already have happened.
        impl_->prepared_device = tgfx::create_device(tgfx::BackendType::OpenGL);

#else
        throw std::runtime_error("SDLWindowSystem: OpenGL backend not compiled");
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> bootstrap(
            SDL_CreateWindow(
            "Termin Vulkan bootstrap",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            1,
            1,
            SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        if (!bootstrap) {
            throw std::runtime_error(
                std::string("SDLWindowSystem Vulkan bootstrap failed: ") + SDL_GetError());
        }

        uint32_t ext_count = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(bootstrap.get(), &ext_count, nullptr)) {
            throw std::runtime_error(
                std::string("SDL_Vulkan_GetInstanceExtensions(count) failed: ") + SDL_GetError());
        }
        std::vector<const char*> extensions(ext_count);
        if (!SDL_Vulkan_GetInstanceExtensions(
                bootstrap.get(), &ext_count, extensions.data())) {
            throw std::runtime_error(
                std::string("SDL_Vulkan_GetInstanceExtensions(list) failed: ") + SDL_GetError());
        }

        tgfx::VulkanDeviceCreateInfo info;
        const char* validation_env = std::getenv("TGFX2_VULKAN_VALIDATION");
        info.enable_validation = (validation_env && validation_env[0] == '1');
        info.instance_extensions = extensions;
        info.presentation_probe_surface_factory = [window = bootstrap.get()](
            VkInstance instance) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
                throw std::runtime_error(
                    std::string("SDL_Vulkan_CreateSurface(probe) failed: ") + SDL_GetError());
            }
            return surface;
        };
        impl_->prepared_device = std::make_unique<tgfx::VulkanRenderDevice>(info);
    }
#endif
#ifdef TGFX2_HAS_D3D11
    else if (impl_->backend == tgfx::BackendType::D3D11) {
        impl_->prepared_device = std::make_unique<tgfx::D3D11RenderDevice>();
    }
#endif
    else {
        throw std::runtime_error("SDLWindowSystem: unsupported backend");
    }
}

SDLWindowSystem::~SDLWindowSystem() {
    if (!impl_) return;
    if (impl_->live_windows != 0) {
        tc_log_error(
            "[SDLWindowSystem] destroyed with %zu live presentation window(s)",
            impl_->live_windows);
        // Do not pretend recovery is possible: live windows still contain
        // non-owning device/system references. Their owner violated the
        // WindowedGraphicsSession lifetime contract.
        impl_->abandon_bootstrap_resources();
        return;
    }
    // A host created through create_graphics_host() is externally owned and
    // must already be closed by the session. A merely prepared device is
    // still ours and must die before the GL bootstrap context. Impl repeats
    // this idempotently so partially constructed systems receive the same
    // teardown order during stack unwinding.
    impl_->release_bootstrap_resources();
}

std::unique_ptr<tgfx::GraphicsHost> SDLWindowSystem::create_graphics_host() {
    if (impl_->closed || !impl_->prepared_device || impl_->graphics_host) {
        throw std::logic_error(
            "SDLWindowSystem::create_graphics_host requires one fresh prepared device");
    }
    auto graphics = tgfx::GraphicsHost::adopt_application_device(
        std::move(impl_->prepared_device));
    impl_->graphics_host = graphics.get();
    return graphics;
}

BackendWindowPtr SDLWindowSystem::create_window(
    tgfx::GraphicsHost& graphics,
    const WindowConfig& config) {
    if (impl_->closed || impl_->graphics_host != &graphics || graphics.is_closed()) {
        throw std::logic_error("SDLWindowSystem::create_window called after close");
    }
    return std::make_unique<SDLBackendWindow>(
        *this,
        graphics,
        config.title,
        config.width,
        config.height,
        config.presentation_mode);
}

size_t SDLWindowSystem::live_window_count() const {
    return impl_->live_windows;
}

void SDLWindowSystem::close(tgfx::GraphicsHost& graphics) {
    if (impl_->closed) return;
    if (impl_->live_windows != 0) {
        throw std::logic_error(
            "SDLWindowSystem::close requires all presentation windows to be closed");
    }
    if (impl_->graphics_host != &graphics) {
        throw std::invalid_argument(
            "SDLWindowSystem::close received a different GraphicsHost");
    }
    graphics.close();
    impl_->graphics_host = nullptr;
    impl_->release_bootstrap_resources();
    impl_->closed = true;
}

void SDLWindowSystem::register_window() {
    ++impl_->live_windows;
}

void SDLWindowSystem::unregister_window() {
    if (impl_->live_windows == 0) {
        tc_log_error("[SDLWindowSystem] presentation window count underflow");
        return;
    }
    --impl_->live_windows;
}

// ---------------------------------------------------------------------------
// Presentation window
// ---------------------------------------------------------------------------

SDLBackendWindow::SDLBackendWindow(
    SDLWindowSystem& window_system,
    tgfx::GraphicsHost& graphics,
    const std::string& title,
    int width,
    int height,
    tgfx::PresentationMode presentation_mode)
    : impl_(std::make_unique<Impl>())
{
    impl_->window_system = &window_system;
    impl_->graphics_host = &graphics;
    impl_->backend = graphics.device().backend_type();
    impl_->requested_presentation_mode = presentation_mode;
    impl_->presentation_mode = presentation_mode;
    impl_->device_ref = &graphics.device();
    configure_sdl_window_hints();

    try {
    if (impl_->backend == tgfx::BackendType::OpenGL) {
#ifdef TGFX2_HAS_OPENGL
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(OpenGL) failed: ") +
                                     SDL_GetError());
        }
        if (SDL_GL_MakeCurrent(window_, window_system.impl_->gl_context) != 0) {
            throw std::runtime_error(
                std::string("SDL_GL_MakeCurrent(window) failed: ") + SDL_GetError());
        }
        const int swap_interval = presentation_mode == tgfx::PresentationMode::VSync ? 1 : 0;
        if (SDL_GL_SetSwapInterval(swap_interval) != 0) {
            tc_log_warn(
                "SDLBackendWindow: SDL_GL_SetSwapInterval(%d) is unsupported: %s",
                swap_interval,
                SDL_GetError());
        }
        const int applied_swap_interval = SDL_GL_GetSwapInterval();
        impl_->presentation_mode = applied_swap_interval == 0
            ? tgfx::PresentationMode::Immediate
            : tgfx::PresentationMode::VSync;
#else
        throw std::runtime_error("SDLBackendWindow: OpenGL backend not compiled");
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
        window_ = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    width, height, flags);
        if (!window_) {
            throw std::runtime_error(std::string("SDL_CreateWindow(Vulkan) failed: ") +
                                     SDL_GetError());
        }

        auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window_, vk_dev->instance(), &surf)) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface(window) failed: ") +
                                     SDL_GetError());
        }
        impl_->surface = surf;

        int fb_w = 0, fb_h = 0;
        SDL_Vulkan_GetDrawableSize(window_, &fb_w, &fb_h);
        impl_->swapchain = std::make_unique<tgfx::VulkanSwapchain>(
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
            throw std::runtime_error(std::string("SDL_CreateWindow(D3D11) failed: ") +
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
            static_cast<uint32_t>(fb_h),
            impl_->requested_presentation_mode);
        impl_->presentation_mode = impl_->d3d11_swapchain->presentation_mode();
    }
#endif
    else {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        throw std::runtime_error("SDLBackendWindow: unsupported backend");
    }
    register_window_event_queue(window_, impl_->pending_events);
    window_system.register_window();
    } catch (...) {
#ifdef TGFX2_HAS_D3D11
        impl_->d3d11_swapchain.reset();
#endif
#ifdef TGFX2_HAS_VULKAN
        impl_->swapchain.reset();
        if (impl_->surface != VK_NULL_HANDLE) {
            auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
            vkDestroySurfaceKHR(vk_dev->instance(), impl_->surface, nullptr);
            impl_->surface = VK_NULL_HANDLE;
        }
#endif
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        impl_->device_ref = nullptr;
        impl_->graphics_host = nullptr;
        impl_->window_system = nullptr;
        throw;
    }
}

tgfx::GraphicsHost& SDLBackendWindow::graphics_host() const {
    if (!impl_ || !impl_->graphics_host || impl_->graphics_host->is_closed()) {
        throw std::logic_error("SDLBackendWindow: graphics host is unavailable");
    }
    return *impl_->graphics_host;
}

void SDLBackendWindow::maximize() {
    if (window_) {
        SDL_MaximizeWindow(window_);
    }
}

void SDLBackendWindow::set_title(const std::string& title) {
    if (window_) {
        SDL_SetWindowTitle(window_, title.c_str());
    }
}

void SDLBackendWindow::set_size(int width, int height) {
    if (window_ && width > 0 && height > 0) {
        SDL_SetWindowSize(window_, width, height);
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

void SDLBackendWindow::set_text_input_enabled(bool enabled) {
    if (enabled == text_input_enabled_) return;
    text_input_enabled_ = enabled;
    size_t& users = text_input_user_count();
    if (enabled) {
        if (users++ == 0) SDL_StartTextInput();
        return;
    }
    if (users > 0 && --users == 0) SDL_StopTextInput();
}

void SDLBackendWindow::set_cursor(WindowCursor cursor) {
    static constexpr std::array<SDL_SystemCursor, 9> sdl_cursors{
        SDL_SYSTEM_CURSOR_ARROW,
        SDL_SYSTEM_CURSOR_IBEAM,
        SDL_SYSTEM_CURSOR_HAND,
        SDL_SYSTEM_CURSOR_CROSSHAIR,
        SDL_SYSTEM_CURSOR_SIZEALL,
        SDL_SYSTEM_CURSOR_SIZEWE,
        SDL_SYSTEM_CURSOR_SIZENS,
        SDL_SYSTEM_CURSOR_SIZENWSE,
        SDL_SYSTEM_CURSOR_SIZENESW,
    };
    const size_t index = static_cast<size_t>(cursor);
    if (index >= sdl_cursors.size()) {
        tc_log_error("[SDLBackendWindow] invalid cursor value: %zu", index);
        return;
    }
    SDL_Cursor*& handle = system_cursor_store().cursors[index];
    if (!handle) {
        handle = SDL_CreateSystemCursor(sdl_cursors[index]);
        if (!handle) {
            tc_log_error(
                "[SDLBackendWindow] SDL_CreateSystemCursor failed: %s",
                SDL_GetError());
            return;
        }
    }
    SDL_SetCursor(handle);
}

std::string SDLBackendWindow::clipboard_text() const {
    if (SDL_HasClipboardText() != SDL_TRUE) return {};
    char* text = SDL_GetClipboardText();
    if (!text) {
        tc_log_error(
            "[SDLBackendWindow] SDL_GetClipboardText failed: %s",
            SDL_GetError());
        return {};
    }
    std::string result(text);
    SDL_free(text);
    return result;
}

bool SDLBackendWindow::set_clipboard_text(const std::string& text) {
    if (SDL_SetClipboardText(text.c_str()) == 0) return true;
    tc_log_error(
        "[SDLBackendWindow] SDL_SetClipboardText failed: %s",
        SDL_GetError());
    return false;
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
    if (!window_ && !impl_->device_ref) {
        return;
    }
    set_text_input_enabled(false);
    unregister_window_event_queue(window_);
    impl_->pending_events.clear();

    // Teardown only this window's presentation resources. The shared device
    // and context belong to SDLWindowSystem and remain available to every
    // other window regardless of creation or destruction order.
#ifdef TGFX2_HAS_D3D11
    if (impl_->d3d11_swapchain) {
        if (impl_->device_ref) {
            impl_->device_ref->wait_idle();
        }
        impl_->d3d11_swapchain.reset();
    }
#endif
#ifdef TGFX2_HAS_VULKAN
    if (impl_->swapchain) {
        if (impl_->device_ref) {
            impl_->device_ref->wait_idle();
        }
        impl_->swapchain.reset();
    }
    if (impl_->surface != VK_NULL_HANDLE && impl_->device_ref) {
        auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(impl_->device_ref);
        vkDestroySurfaceKHR(vk_dev->instance(), impl_->surface, nullptr);
        impl_->surface = VK_NULL_HANDLE;
    }
#endif
    impl_->device_ref = nullptr;
    impl_->graphics_host = nullptr;
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (impl_->window_system) {
        impl_->window_system->unregister_window();
        impl_->window_system = nullptr;
    }
    should_close_ = true;
}

SDLBackendWindow::~SDLBackendWindow() {
    close();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

tgfx::BackendType SDLBackendWindow::backend_type() const {
    return impl_->backend;
}

tgfx::PresentationMode SDLBackendWindow::requested_presentation_mode() const {
    return impl_->requested_presentation_mode;
}

tgfx::PresentationMode SDLBackendWindow::presentation_mode() const {
    return impl_->presentation_mode;
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

std::pair<int, int> SDLBackendWindow::window_size() const {
    if (!window_) return {0, 0};
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    return {width, height};
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool SDLBackendWindow::poll_event(WindowEvent& out_event) {
    SDL_Event ev;
    while (next_window_event(window_, impl_->pending_events, ev)) {
        if (ev.type == SDL_QUIT) {
            should_close_ = true;
            out_event = {};
            out_event.type = WindowEventType::CloseRequested;
            return true;
        }

        const uint32_t own_window_id = window_ ? SDL_GetWindowID(window_) : 0;
        const uint32_t ev_window_id = event_window_id(ev);
        if (ev_window_id != 0 && own_window_id != 0 && ev_window_id != own_window_id) {
            continue;
        }

        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
            should_close_ = true;
            out_event = {};
            out_event.type = WindowEventType::CloseRequested;
            return true;
        }

        out_event = {};
        switch (ev.type) {
            case SDL_WINDOWEVENT:
                if (ev.window.event != SDL_WINDOWEVENT_RESIZED &&
                    ev.window.event != SDL_WINDOWEVENT_SIZE_CHANGED) {
                    continue;
                }
                out_event.type = WindowEventType::Resized;
                out_event.resize.width = ev.window.data1;
                out_event.resize.height = ev.window.data2;
                {
                    const auto [width, height] = framebuffer_size();
                    out_event.resize.framebuffer_width = width;
                    out_event.resize.framebuffer_height = height;
                }
                return true;

            case SDL_MOUSEMOTION:
                out_event.type = WindowEventType::PointerMoved;
                out_event.pointer.logical_position = {
                    static_cast<float>(ev.motion.x),
                    static_cast<float>(ev.motion.y)};
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                out_event.type = ev.type == SDL_MOUSEBUTTONDOWN
                    ? WindowEventType::PointerButtonPressed
                    : WindowEventType::PointerButtonReleased;
                out_event.pointer.logical_position = {
                    static_cast<float>(ev.button.x),
                    static_cast<float>(ev.button.y)};
                out_event.pointer.button = translate_pointer_button(ev.button.button);
                out_event.pointer.clicks = ev.button.clicks;
                break;

            case SDL_MOUSEWHEEL: {
                out_event.type = WindowEventType::PointerWheel;
                int x = 0;
                int y = 0;
                SDL_GetMouseState(&x, &y);
                out_event.pointer.logical_position = {
                    static_cast<float>(x), static_cast<float>(y)};
                const float direction = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                    ? -1.0f : 1.0f;
                out_event.pointer.wheel_x = direction * static_cast<float>(ev.wheel.x);
                out_event.pointer.wheel_y = direction * static_cast<float>(ev.wheel.y);
                break;
            }

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                out_event.type = ev.type == SDL_KEYDOWN
                    ? WindowEventType::KeyPressed
                    : WindowEventType::KeyReleased;
                out_event.key.key = translate_key(ev.key.keysym.scancode);
                out_event.key.native_key = static_cast<int32_t>(ev.key.keysym.sym);
                out_event.key.native_scancode = static_cast<int32_t>(ev.key.keysym.scancode);
                out_event.key.modifiers = translate_modifiers(
                    static_cast<SDL_Keymod>(ev.key.keysym.mod));
                out_event.key.repeat = ev.key.repeat != 0;
                return true;

            case SDL_TEXTINPUT:
                out_event.type = WindowEventType::TextInput;
                {
                    const size_t length = std::min(
                        std::strlen(ev.text.text),
                        out_event.text.utf8.size() - 1);
                    std::copy_n(
                        ev.text.text, length, out_event.text.utf8.data());
                    out_event.text.utf8[length] = '\0';
                }
                return true;

            case SDL_DROPFILE: {
                out_event.type = WindowEventType::FileDropped;
                if (ev.drop.file) {
                    out_event.file_drop.path = ev.drop.file;
                    SDL_free(ev.drop.file);
                }
                int x = 0;
                int y = 0;
                SDL_GetMouseState(&x, &y);
                out_event.file_drop.logical_position = {
                    static_cast<float>(x), static_cast<float>(y)};
                out_event.file_drop.modifiers =
                    translate_modifiers(SDL_GetModState());
                return true;
            }

            default:
                continue;
        }

        out_event.pointer.modifiers = translate_modifiers(SDL_GetModState());
        const auto [window_width, window_height] = window_size();
        const auto [framebuffer_width, framebuffer_height] = framebuffer_size();
        out_event.pointer.framebuffer_position = out_event.pointer.logical_position;
        if (window_width > 0 && window_height > 0) {
            out_event.pointer.framebuffer_position.x *=
                static_cast<float>(framebuffer_width) / static_cast<float>(window_width);
            out_event.pointer.framebuffer_position.y *=
                static_cast<float>(framebuffer_height) / static_cast<float>(window_height);
        }
        return true;
    }
    return false;
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
        SDL_GLContext gl_ctx = impl_->window_system
            ? impl_->window_system->impl_->gl_context
            : nullptr;
        if (!gl_ctx) {
            tc_log(TC_LOG_ERROR, "[BackendWindow] present: no GL context");
            return;
        }
        if (SDL_GL_MakeCurrent(window_, gl_ctx) != 0) {
            tc_log_error(
                "[BackendWindow] present: SDL_GL_MakeCurrent failed: %s",
                SDL_GetError());
            return;
        }

        auto* gl_dev = static_cast<tgfx::OpenGLRenderDevice*>(impl_->device_ref);
        gl_dev->present_to_default_framebuffer(color_tex, w, h);
        SDL_GL_SwapWindow(window_);
#endif
    }
#ifdef TGFX2_HAS_VULKAN
    else if (impl_->backend == tgfx::BackendType::Vulkan) {
        tgfx::VulkanSwapchain* sc = impl_->swapchain.get();
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
