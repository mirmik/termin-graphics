#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <termin/geom/color.hpp>
#include <termin/gui_native/document_renderer_export.h>
#include <termin/gui_native/tc_document.hpp>
#include <tgfx2/handles.hpp>

namespace tgfx {
    class GraphicsHost;
    class IRenderDevice;
    class RenderContext2;
} // namespace tgfx

namespace termin::gui_native {

    class ColorPicker;
    class DynamicTextureLease;
    struct DocumentRendererLeaseState;

    struct DocumentRendererConfig {
        std::string font_path;
        int font_size = 14;
        termin::LinearColor clear_linear_color{0.03f, 0.035f, 0.045f, 1.0f};
        bool enable_text_input = true;
    };

    // Presentation destination selected by the composition root. Implementations
    // may present to a borrowed window or publish an offscreen texture.
    class TERMIN_GUI_NATIVE_RENDERER_API DocumentFrameSink {
    public:
        virtual ~DocumentFrameSink() = default;
        virtual std::pair<int, int> framebuffer_size() const = 0;
        virtual float content_scale() const {
            return 1.0f;
        }
        virtual void publish_frame(tgfx::TextureHandle color_texture) = 0;
    };

    // Environment services needed by retained document interaction. This
    // contract contains no OS-window types.
    class TERMIN_GUI_NATIVE_RENDERER_API DocumentPlatformServices {
    public:
        virtual ~DocumentPlatformServices() = default;
        virtual bool set_text_input_enabled(bool enabled) = 0;
        virtual std::string clipboard_text() const = 0;
        virtual bool set_clipboard_text(const std::string& text) = 0;
        virtual bool set_cursor(tc_ui_cursor_intent cursor) = 0;
    };

    // Renderer/interaction binding for one borrowed TcDocument and GraphicsHost.
    // It has no application loop, window ownership, or close policy.
    class TERMIN_GUI_NATIVE_RENDERER_API DocumentRenderer {
    public:
        // Called synchronously for an unhandled, non-repeating key-down after the
        // document has applied focus and modal/overlay routing. Return true when
        // the application consumed the key. The renderer does not own application
        // command policy or the callback's captured objects.
        using UnhandledKeyHandler = std::function<bool(const tc_ui_key_event&)>;

        DocumentRenderer(tgfx::GraphicsHost& graphics,
                         TcDocument document,
                         DocumentRendererConfig config,
                         DocumentFrameSink& frame_sink,
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
        bool sync_presentation_metrics();
        bool render_frame();

        void set_unhandled_key_handler(UnhandledKeyHandler handler);
        void set_before_frame_callback(std::function<void(tgfx::RenderContext2&)> callback);
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
        std::shared_ptr<DocumentRendererLeaseState> texture_lease_state() const;
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace termin::gui_native
