// backend_window.hpp - abstract tgfx2 presentation window interface.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "termin/window/api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"

namespace tgfx {
class IRenderDevice;
class RenderContext2;
}

namespace termin {

// BackendWindow is intentionally backend-neutral. It does not expose SDL,
// Qt, GLFW, or native OS handles, which lets termin-display/graphics build
// without SDL and lets tests provide lightweight mock windows.
class TERMIN_WINDOW_API BackendWindow {
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
    virtual void set_fullscreen(bool enabled) = 0;
    virtual void close() = 0;
    virtual void poll_events() = 0;
    virtual std::pair<int, int> framebuffer_size() const = 0;
    virtual void present(tgfx::TextureHandle color_tex) = 0;

protected:
    BackendWindow() = default;
};

using BackendWindowPtr = std::unique_ptr<BackendWindow>;

} // namespace termin
