#include <termin/gui_native/window_adapter.hpp>

#include <optional>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.h>

#include <tgfx2/graphics_host.hpp>

#include <termin/gui_native/window_input.hpp>

namespace termin::gui_native {

namespace {

[[noreturn]] void adapter_error(const std::string& message) {
    tc_log_error("[gui-native-window-adapter] %s", message.c_str());
    throw std::logic_error(message);
}

class BorrowedWindowEndpoint final : public DocumentFrameSink {
  public:
    explicit BorrowedWindowEndpoint(BackendWindow& window) : window_(&window) {}

    std::pair<int, int> framebuffer_size() const override {
        return window_->framebuffer_size();
    }

    void publish_frame(tgfx::TextureHandle color_texture) override {
        window_->present(color_texture);
    }

  private:
    BackendWindow* window_;
};

WindowCursor window_cursor(tc_ui_cursor_intent cursor) {
    switch (cursor) {
    case TC_UI_CURSOR_TEXT: return WindowCursor::Text;
    case TC_UI_CURSOR_HAND: return WindowCursor::Hand;
    case TC_UI_CURSOR_CROSSHAIR: return WindowCursor::Crosshair;
    case TC_UI_CURSOR_MOVE: return WindowCursor::Move;
    case TC_UI_CURSOR_RESIZE_HORIZONTAL: return WindowCursor::ResizeHorizontal;
    case TC_UI_CURSOR_RESIZE_VERTICAL: return WindowCursor::ResizeVertical;
    case TC_UI_CURSOR_RESIZE_NWSE: return WindowCursor::ResizeNWSE;
    case TC_UI_CURSOR_RESIZE_NESW: return WindowCursor::ResizeNESW;
    case TC_UI_CURSOR_INHERIT:
    case TC_UI_CURSOR_DEFAULT:
    case TC_UI_CURSOR_INTENT_COUNT:
        return WindowCursor::Default;
    }
    return WindowCursor::Default;
}

class BorrowedWindowPlatformServices final : public DocumentPlatformServices {
  public:
    explicit BorrowedWindowPlatformServices(BackendWindow& window) : window_(&window) {}

    bool set_text_input_enabled(bool enabled) override {
        window_->set_text_input_enabled(enabled);
        return true;
    }

    std::string clipboard_text() const override {
        return window_->clipboard_text();
    }

    bool set_clipboard_text(const std::string& text) override {
        return window_->set_clipboard_text(text);
    }

    bool set_cursor(tc_ui_cursor_intent cursor) override {
        window_->set_cursor(window_cursor(cursor));
        return true;
    }

  private:
    BackendWindow* window_;
};

} // namespace

struct GuiWindowAdapter::Impl {
    tgfx::GraphicsHost* graphics;
    Document* document;
    BackendWindow* window;
    BorrowedWindowEndpoint endpoint;
    BorrowedWindowPlatformServices platform;
    std::unique_ptr<DocumentRenderer> renderer;
    bool closed = false;

    Impl(tgfx::GraphicsHost& graphics_ref, Document& document_ref,
         DocumentRendererConfig config, BackendWindow& window_ref)
        : graphics(&graphics_ref), document(&document_ref), window(&window_ref),
          endpoint(window_ref), platform(window_ref) {
        if (&window->graphics_host() != graphics) {
            adapter_error(
                "GuiWindowAdapter requires the window and renderer to share one GraphicsHost");
        }
        renderer = std::make_unique<DocumentRenderer>(
            *graphics, *document, std::move(config), endpoint, platform);
    }

    void require_open(const char* operation) const {
        if (closed || !renderer || !renderer->is_open()) {
            adapter_error(
                std::string("GuiWindowAdapter::") + operation + " called after close");
        }
    }

    void close() {
        if (closed) return;
        if (renderer) {
            renderer->close();
            renderer.reset();
        }
        closed = true;
    }
};

GuiWindowAdapter::GuiWindowAdapter(tgfx::GraphicsHost& graphics, Document& document,
                                   DocumentRendererConfig config, BackendWindow& window)
    : impl_(std::make_unique<Impl>(
          graphics, document, std::move(config), window)) {}

GuiWindowAdapter::~GuiWindowAdapter() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-window-adapter] shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-window-adapter] shutdown failed with an unknown exception");
    }
}

BackendWindow& GuiWindowAdapter::window() {
    impl_->require_open("window");
    return *impl_->window;
}

const BackendWindow& GuiWindowAdapter::window() const {
    impl_->require_open("window");
    return *impl_->window;
}

Document& GuiWindowAdapter::document() {
    impl_->require_open("document");
    return *impl_->document;
}

const Document& GuiWindowAdapter::document() const {
    impl_->require_open("document");
    return *impl_->document;
}

DocumentRenderer& GuiWindowAdapter::renderer() {
    impl_->require_open("renderer");
    return *impl_->renderer;
}

const DocumentRenderer& GuiWindowAdapter::renderer() const {
    impl_->require_open("renderer");
    return *impl_->renderer;
}

size_t GuiWindowAdapter::consume_events(std::span<const WindowEvent> events) {
    impl_->require_open("consume_events");
    for (const WindowEvent& event : events) {
        if (const auto pointer = make_pointer_event(event)) {
            impl_->renderer->dispatch_pointer(*pointer);
        } else if (const auto key = make_key_event(event)) {
            impl_->renderer->dispatch_key(*key);
        } else if (const auto text = make_text_event(event)) {
            impl_->renderer->dispatch_text(text->text);
        }
    }
    return events.size();
}

bool GuiWindowAdapter::render_and_present() {
    impl_->require_open("render_and_present");
    return impl_->renderer->render_frame();
}

void GuiWindowAdapter::request_repaint() {
    renderer().request_repaint();
}

bool GuiWindowAdapter::repaint_requested() const {
    return renderer().repaint_requested();
}

void GuiWindowAdapter::wait_idle() {
    renderer().wait_idle();
}

void GuiWindowAdapter::close() {
    if (impl_) impl_->close();
}

bool GuiWindowAdapter::is_open() const {
    return impl_ && !impl_->closed;
}

} // namespace termin::gui_native
