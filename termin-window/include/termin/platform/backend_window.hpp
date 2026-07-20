// backend_window.hpp - abstract tgfx2 presentation window interface.
#pragma once

#include <functional>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "termin/window/api.h"
#include "termin/window/event.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"

namespace tgfx {
class IRenderDevice;
class RenderContext2;
class GraphicsHost;
}

namespace termin {

class BackendWindow;
using BackendWindowPtr = std::unique_ptr<BackendWindow>;

enum class WindowCursor {
    Default,
    Text,
    Hand,
    Crosshair,
    Move,
    ResizeHorizontal,
    ResizeVertical,
    ResizeNWSE,
    ResizeNESW,
};

// BackendWindow is intentionally backend-neutral. It does not expose SDL,
// Qt, GLFW, or native OS handles, which lets termin-display/graphics build
// without SDL and lets tests provide lightweight mock windows.
class TERMIN_WINDOW_API BackendWindow {
private:
    std::function<void(const WindowEvent&)> event_handler_;

public:
    virtual ~BackendWindow() = default;

    BackendWindow(const BackendWindow&) = delete;
    BackendWindow& operator=(const BackendWindow&) = delete;

    virtual tgfx::BackendType backend_type() const = 0;
    virtual tgfx::PresentationMode requested_presentation_mode() const = 0;
    virtual tgfx::PresentationMode presentation_mode() const = 0;

    virtual bool should_close() const = 0;
    virtual void set_should_close(bool v) = 0;

    virtual void maximize() = 0;
    virtual void set_title(const std::string& title) = 0;
    virtual void set_size(int width, int height) = 0;
    virtual void set_fullscreen(bool enabled) = 0;
    virtual void set_text_input_enabled(bool enabled) = 0;
    virtual void set_cursor(WindowCursor cursor) = 0;
    virtual std::string clipboard_text() const = 0;
    virtual bool set_clipboard_text(const std::string& text) = 0;
    virtual void close() = 0;
    virtual bool poll_event(WindowEvent& out_event) = 0;
    void poll_events();
    void set_event_handler(std::function<void(const WindowEvent&)> handler);
    virtual std::pair<int, int> window_size() const = 0;
    virtual std::pair<int, int> framebuffer_size() const = 0;
    virtual void present(tgfx::TextureHandle color_tex) = 0;

protected:
    BackendWindow() = default;
};

struct WindowConfig {
    std::string title;
    int width = 1280;
    int height = 720;
    tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync;
};

// Platform window/presentation service for one application graphics domain.
// The canonical domain object is tgfx::GraphicsHost; this interface adds
// native-window construction without duplicating its device/context contract.
// Windows created by one system are equal presentation targets.
class TERMIN_WINDOW_API BackendWindowSystem {
public:
    virtual ~BackendWindowSystem() = default;

    BackendWindowSystem(const BackendWindowSystem&) = delete;
    BackendWindowSystem& operator=(const BackendWindowSystem&) = delete;

    // Platform-aware creation is required for SDL/OpenGL and Vulkan, but the
    // returned GraphicsHost is the sole owner of the graphics domain.
    virtual std::unique_ptr<tgfx::GraphicsHost> create_graphics_host() = 0;
    virtual BackendWindowPtr create_window(
        tgfx::GraphicsHost& graphics,
        const WindowConfig& config) = 0;
    virtual size_t live_window_count() const = 0;
    // Closes the canonical host first, then platform state. This ordering is
    // mandatory for OpenGL, whose device teardown needs the SDL GL context.
    virtual void close(tgfx::GraphicsHost& graphics) = 0;

protected:
    BackendWindowSystem() = default;
};

using BackendWindowSystemPtr = std::unique_ptr<BackendWindowSystem>;

// Standard composition root for windowed applications. This is a lifetime
// aggregate, not a second graphics abstraction: `graphics()` returns the one
// canonical tgfx::GraphicsHost. Presentation windows remain separately owned
// and must be closed before close().
class TERMIN_WINDOW_API WindowedGraphicsSession {
private:
    BackendWindowSystemPtr windows_;
    std::unique_ptr<tgfx::GraphicsHost> graphics_;
    bool closed_ = false;

public:
    WindowedGraphicsSession(
        BackendWindowSystemPtr windows,
        std::unique_ptr<tgfx::GraphicsHost> graphics);
    ~WindowedGraphicsSession();

    WindowedGraphicsSession(const WindowedGraphicsSession&) = delete;
    WindowedGraphicsSession& operator=(const WindowedGraphicsSession&) = delete;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    BackendWindowPtr create_window(const WindowConfig& config);
    void close();
};

// Creates the native window system and its platform-compatible GraphicsHost.
// The system must outlive every window and every GPU resource in that runtime.
TERMIN_WINDOW_API BackendWindowSystemPtr create_native_window_system();
TERMIN_WINDOW_API std::unique_ptr<WindowedGraphicsSession>
create_native_windowed_graphics();

} // namespace termin
