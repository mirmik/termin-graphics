// backend_window.hpp - abstract tgfx2 presentation window interface.
#pragma once

#include <functional>
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
}

namespace termin {

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

    virtual tgfx::IRenderDevice* device() = 0;
    virtual tgfx::RenderContext2* context() = 0;
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

using BackendWindowPtr = std::unique_ptr<BackendWindow>;

struct WindowConfig {
    std::string title;
    int width = 1280;
    int height = 720;
    tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync;
};

// Creates the native window implementation selected by the SDK build. The
// returned interface does not expose SDL or another platform toolkit.
TERMIN_WINDOW_API BackendWindowPtr create_native_window(const WindowConfig& config);

} // namespace termin
