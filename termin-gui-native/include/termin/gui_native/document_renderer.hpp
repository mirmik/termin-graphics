#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <termin/gui_native/application_host_export.h>
#include <termin/gui_native/tc_document.hpp>
#include <tgfx2/handles.hpp>

namespace tgfx {
class GraphicsHost;
class IRenderDevice;
class RenderContext2;
}

namespace termin::gui_native {

class ColorPicker;
class DynamicTextureLease;
struct GuiApplicationHostLeaseState;

struct DocumentRendererConfig {
    std::string font_path;
    int font_size = 14;
    std::array<float, 4> clear_color{0.03f, 0.035f, 0.045f, 1.0f};
    bool enable_text_input = true;
};

// Presentation destination selected by the composition root. Implementations
// may present to a borrowed window or publish an offscreen texture.
class TERMIN_GUI_NATIVE_HOST_API DocumentFrameSink {
  public:
    virtual ~DocumentFrameSink() = default;
    virtual std::pair<int, int> framebuffer_size() const = 0;
    virtual void publish_frame(tgfx::TextureHandle color_texture) = 0;
};

// Environment services needed by retained document interaction. This
// contract contains no OS-window types.
class TERMIN_GUI_NATIVE_HOST_API DocumentPlatformServices {
  public:
    virtual ~DocumentPlatformServices() = default;
    virtual bool set_text_input_enabled(bool enabled) = 0;
    virtual std::string clipboard_text() const = 0;
    virtual bool set_clipboard_text(const std::string& text) = 0;
    virtual bool set_cursor(tc_ui_cursor_intent cursor) = 0;
};

// Renderer/interaction binding for one borrowed TcDocument and GraphicsHost.
// It has no application loop, window ownership, or close policy.
class TERMIN_GUI_NATIVE_HOST_API DocumentRenderer {
  public:
    DocumentRenderer(tgfx::GraphicsHost& graphics, TcDocument document,
                     DocumentRendererConfig config, DocumentFrameSink& frame_sink,
                     DocumentPlatformServices& platform_services);
    ~DocumentRenderer();

    DocumentRenderer(const DocumentRenderer&) = delete;
    DocumentRenderer& operator=(const DocumentRenderer&) = delete;
    DocumentRenderer(DocumentRenderer&&) = delete;
    DocumentRenderer& operator=(DocumentRenderer&&) = delete;

    tgfx::GraphicsHost& graphics();
    const tgfx::GraphicsHost& graphics() const;
    tgfx::IRenderDevice& device();
    const tgfx::IRenderDevice& device() const;
    TcDocument document() const;

    tc_ui_event_result dispatch_pointer(const tc_ui_pointer_event& event);
    tc_ui_event_result dispatch_key(const tc_ui_key_event& event);
    tc_ui_event_result dispatch_text(const std::string& utf8);
    std::pair<int, int> framebuffer_size() const;
    bool render_frame();

    void set_before_frame_callback(
        std::function<void(tgfx::RenderContext2&)> callback);
    void register_color_picker(ColorPicker& picker);
    void unregister_color_picker(ColorPicker& picker);
    void request_repaint();
    bool repaint_requested() const;
    size_t rendered_frame_count() const;
    tgfx::TextureHandle color_target() const;
    void wait_idle();
    void close();
    bool is_open() const;

  private:
    friend class DynamicTextureLease;
    std::shared_ptr<GuiApplicationHostLeaseState> texture_lease_state() const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
