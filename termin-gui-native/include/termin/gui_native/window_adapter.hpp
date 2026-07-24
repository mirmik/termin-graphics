#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include <termin/gui_native/document_renderer.hpp>
#include <termin/platform/backend_window.hpp>

namespace termin::gui_native {

// Borrowed native-widget binding for one application-owned BackendWindow.
// The adapter owns only GUI rendering/input state. It never creates or closes
// the window, its WindowManager, or the shared GraphicsHost.
class TERMIN_GUI_NATIVE_HOST_API GuiWindowAdapter {
  public:
    GuiWindowAdapter(tgfx::GraphicsHost& graphics, TcDocument document,
                     DocumentRendererConfig config, BackendWindow& window);
    ~GuiWindowAdapter();

    GuiWindowAdapter(const GuiWindowAdapter&) = delete;
    GuiWindowAdapter& operator=(const GuiWindowAdapter&) = delete;
    GuiWindowAdapter(GuiWindowAdapter&&) = delete;
    GuiWindowAdapter& operator=(GuiWindowAdapter&&) = delete;

    BackendWindow& window();
    const BackendWindow& window() const;
    TcDocument document() const;
    DocumentRenderer& renderer();
    const DocumentRenderer& renderer() const;

    // Consumes only the already-routed batch supplied by the application.
    // Event pumping and cross-window routing remain WindowManager duties.
    size_t consume_events(std::span<const WindowEvent> events);
    bool render_and_present();
    void request_repaint();
    bool repaint_requested() const;
    void wait_idle();
    void close();
    bool is_open() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
