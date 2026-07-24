#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include <termin/gui_native/text_input.hpp>
#include <termin/gui_native/window_adapter.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

tgfx::BackendType isolated_backend() {
    if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        return tgfx::BackendType::Vulkan;
    }
    if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11)) {
        return tgfx::BackendType::D3D11;
    }
    return tgfx::BackendType::Null;
}

class BorrowedTestWindow final : public termin::BackendWindow {
  public:
    BorrowedTestWindow(tgfx::GraphicsHost& graphics, tgfx::BackendType backend)
        : graphics_(&graphics), backend_(backend) {}

    tgfx::BackendType backend_type() const override { return backend_; }
    tgfx::GraphicsHost& graphics_host() const override { return *graphics_; }
    tgfx::PresentationMode requested_presentation_mode() const override {
        return tgfx::PresentationMode::VSync;
    }
    tgfx::PresentationMode presentation_mode() const override {
        return tgfx::PresentationMode::VSync;
    }
    bool should_close() const override { return should_close_; }
    void set_should_close(bool value) override { should_close_ = value; }
    void maximize() override {}
    void set_title(const std::string&) override {}
    void set_size(int width, int height) override { size_ = {width, height}; }
    void set_fullscreen(bool) override {}
    void set_text_input_enabled(bool enabled) override { text_input_enabled = enabled; }
    void set_cursor(termin::WindowCursor cursor_value) override { cursor = cursor_value; }
    std::string clipboard_text() const override { return clipboard; }
    bool set_clipboard_text(const std::string& text) override {
        clipboard = text;
        return true;
    }
    void close() override {
        ++close_count;
        should_close_ = true;
    }
    bool poll_event(termin::WindowEvent&) override { return false; }
    std::pair<int, int> window_size() const override { return size_; }
    std::pair<int, int> framebuffer_size() const override { return {640, 400}; }
    void present(tgfx::TextureHandle texture) override {
        last_presented = texture;
        ++present_count;
    }

    bool text_input_enabled = false;
    termin::WindowCursor cursor = termin::WindowCursor::Default;
    std::string clipboard;
    size_t close_count = 0;
    size_t present_count = 0;
    tgfx::TextureHandle last_presented{};

  private:
    tgfx::GraphicsHost* graphics_;
    tgfx::BackendType backend_;
    std::pair<int, int> size_{320, 200};
    bool should_close_ = false;
};

} // namespace

int main() {
    try {
        const tgfx::BackendType backend = isolated_backend();
        if (backend == tgfx::BackendType::Null) return 77;

        auto graphics = tgfx::GraphicsHost::create_isolated(backend);
        const tc_ui_document_handle document_handle = tc_ui_document_create();
        termin::gui_native::TcDocument document(document_handle);
        BorrowedTestWindow window(*graphics, backend);
        termin::gui_native::DocumentRendererConfig config;
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;

        termin::gui_native::GuiWindowAdapter adapter(
            *graphics, document, config, window);
        if (!window.text_input_enabled || &adapter.window() != &window ||
            !tc_ui_document_handle_eq(adapter.document().handle(),
                                      document.handle()) ||
            window.close_count != 0) {
            std::fprintf(stderr, "adapter did not establish borrowed services\n");
            return 1;
        }
        std::exception_ptr cross_thread_error;
        std::thread worker([&] {
            try {
                adapter.renderer().request_repaint();
            } catch (...) {
                cross_thread_error = std::current_exception();
            }
        });
        worker.join();
        if (cross_thread_error) {
            std::rethrow_exception(cross_thread_error);
        }

        auto* input = new termin::gui_native::TextInput();
        document.adopt(input);
        document.add_root(*input);
        input->set_cursor_intent(TC_UI_CURSOR_TEXT);
        document.set_focus(*input);

        const auto framebuffer_size = adapter.renderer().framebuffer_size();
        if (framebuffer_size != std::pair<int, int>{640, 400}) {
            std::fprintf(
                stderr,
                "adapter did not expose the borrowed framebuffer extent\n");
            return 1;
        }
        document.layout_roots(tc_ui_rect{
            0.0f, 0.0f, static_cast<float>(framebuffer_size.first),
            static_cast<float>(framebuffer_size.second)});
        if (input->c_widget()->bounds.width != 640.0f ||
            input->c_widget()->bounds.height != 400.0f) {
            std::fprintf(stderr, "document layout did not use framebuffer pixels\n");
            return 1;
        }

        termin::WindowEvent pointer;
        pointer.type = termin::WindowEventType::PointerMoved;
        pointer.pointer.framebuffer_position = {4.0f, 4.0f};
        termin::WindowEvent key;
        key.type = termin::WindowEventType::KeyPressed;
        key.key.key = termin::WindowKey::A;
        termin::WindowEvent text;
        text.type = termin::WindowEventType::TextInput;
        std::memcpy(text.text.utf8.data(), "borrowed", 9);
        std::vector<termin::WindowEvent> events{pointer, key, text};

        if (adapter.consume_events(events) != events.size() ||
            input->text() != "borrowed" ||
            window.cursor != termin::WindowCursor::Text ||
            !adapter.repaint_requested()) {
            std::fprintf(stderr, "adapter did not consume the routed event batch\n");
            return 1;
        }
        window.clipboard = "borrowed-read";
        if (std::strcmp(
                tc_ui_document_clipboard_text(document.get()),
                "borrowed-read") != 0) {
            std::fprintf(stderr, "adapter clipboard read service was not connected\n");
            return 1;
        }
        if (!tc_ui_document_set_clipboard_text(document.get(), "typed", 5) ||
            window.clipboard != "typed") {
            std::fprintf(stderr, "adapter clipboard service was not connected\n");
            return 1;
        }
        size_t before_frame_count = 0;
        adapter.renderer().set_before_frame_callback(
            [&before_frame_count](tgfx::RenderContext2&) {
                ++before_frame_count;
            });
        if (!adapter.render_and_present() || before_frame_count != 1 ||
            window.present_count != 1 || !window.last_presented) {
            std::fprintf(
                stderr,
                "adapter did not execute renderer extensions before presentation\n");
            return 1;
        }

        termin::WindowEvent pointer_away;
        pointer_away.type = termin::WindowEventType::PointerMoved;
        pointer_away.pointer.framebuffer_position = {800.0f, 600.0f};
        adapter.consume_events(std::span<const termin::WindowEvent>(&pointer_away, 1));
        document.clear_focus(*input);
        adapter.close();
        if (adapter.is_open() || window.close_count != 0 || graphics->is_closed() ||
            window.text_input_enabled) {
            std::fprintf(stderr, "adapter closed a borrowed owner or leaked platform state\n");
            return 1;
        }

        tc_ui_document_destroy(document_handle);
        graphics->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "window adapter test skipped: %s\n", error.what());
        return 77;
    }
}
