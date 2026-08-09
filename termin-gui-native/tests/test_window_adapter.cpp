#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <termin/gui_native/command_model.hpp>
#include <termin/gui_native/menu_bar.hpp>
#include <termin/gui_native/text_input.hpp>
#include <termin/gui_native/window_adapter.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/shader_artifact_resolver.hpp>

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

    struct ScopedShaderArtifacts {
        std::filesystem::path root;

        ~ScopedShaderArtifacts() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    class BorrowedTestWindow final : public termin::BackendWindow {
    public:
        BorrowedTestWindow(tgfx::GraphicsHost& graphics, tgfx::BackendType backend)
            : graphics_(&graphics),
              backend_(backend) {}

        tgfx::BackendType backend_type() const override {
            return backend_;
        }
        tgfx::GraphicsHost& graphics_host() const override {
            return *graphics_;
        }
        tgfx::PresentationMode requested_presentation_mode() const override {
            return tgfx::PresentationMode::VSync;
        }
        tgfx::PresentationMode presentation_mode() const override {
            return tgfx::PresentationMode::VSync;
        }
        bool should_close() const override {
            return should_close_;
        }
        void set_should_close(bool value) override {
            should_close_ = value;
        }
        void maximize() override {}
        void set_title(const std::string&) override {}
        void set_size(int width, int height) override {
            size_ = {width, height};
        }
        void set_fullscreen(bool) override {}
        void set_text_input_enabled(bool enabled) override {
            text_input_enabled = enabled;
        }
        void set_cursor(termin::WindowCursor cursor_value) override {
            cursor = cursor_value;
        }
        std::string clipboard_text() const override {
            return clipboard;
        }
        bool set_clipboard_text(const std::string& text) override {
            clipboard = text;
            return true;
        }
        void close() override {
            ++close_count;
            should_close_ = true;
        }
        bool poll_event(termin::WindowEvent&) override {
            return false;
        }
        std::pair<int, int> window_size() const override {
            return size_;
        }
        std::pair<int, int> framebuffer_size() const override {
            return framebuffer_size_;
        }
        float content_scale() const override {
            return content_scale_;
        }
        void present(tgfx::TextureHandle texture) override {
            last_presented = texture;
            ++present_count;
        }
        void set_presentation(std::pair<int, int> framebuffer_size, float content_scale) {
            framebuffer_size_ = framebuffer_size;
            content_scale_ = content_scale;
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
        std::pair<int, int> framebuffer_size_{640, 400};
        float content_scale_ = 2.0f;
        bool should_close_ = false;
    };

} // namespace

int main() {
    try {
        const tgfx::BackendType backend = isolated_backend();
        if (backend == tgfx::BackendType::Null) {
            std::printf("window adapter test skipped: no Vulkan or D3D11 backend compiled\n");
            return 77;
        }

        auto graphics = tgfx::GraphicsHost::create_isolated(backend);
        const char* shader_compiler = std::getenv("TERMIN_SHADERC");
        if (!shader_compiler || shader_compiler[0] == '\0') {
            std::fprintf(stderr, "window adapter test requires TERMIN_SHADERC\n");
            return 1;
        }
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const ScopedShaderArtifacts shader_artifacts{std::filesystem::temp_directory_path() /
                                                     ("termin-gui-native-window-adapter-" + std::to_string(unique))};
        graphics->configure_shader_artifacts(termin::ShaderArtifactResolver(
            shader_artifacts.root.string(), (shader_artifacts.root / "cache").string(), shader_compiler, true));
        const tc_ui_document_handle document_handle = tc_ui_document_create();
        termin::gui_native::TcDocument document(document_handle);
        BorrowedTestWindow window(*graphics, backend);
        termin::gui_native::DocumentRendererConfig config;
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;

        termin::gui_native::GuiWindowAdapter adapter(*graphics, document, config, window);
        if (!window.text_input_enabled || &adapter.window() != &window || adapter.should_close() ||
            !tc_ui_document_handle_eq(adapter.document().handle(), document.handle()) || window.close_count != 0) {
            std::fprintf(stderr, "adapter did not establish borrowed services\n");
            return 1;
        }
        tc_ui_presentation_metrics initial_metrics{};
        if (!document.presentation_metrics(initial_metrics) || initial_metrics.density_scale != 2.0f ||
            initial_metrics.physical_extent.width != 640.0f || initial_metrics.physical_extent.height != 400.0f) {
            std::fprintf(stderr, "adapter did not publish initial per-window presentation metrics\n");
            return 1;
        }
        window.set_should_close(true);
        if (!adapter.should_close()) {
            std::fprintf(stderr, "adapter did not expose borrowed close state\n");
            return 1;
        }
        window.set_should_close(false);
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
            std::fprintf(stderr, "adapter did not expose the borrowed framebuffer extent\n");
            return 1;
        }
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 200.0f});
        if (input->c_widget()->bounds.width != 320.0f || input->c_widget()->bounds.height != 200.0f) {
            std::fprintf(stderr, "document layout did not use logical window points\n");
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

        if (adapter.consume_events(events) != events.size() || input->text() != "borrowed" ||
            window.cursor != termin::WindowCursor::Text || !adapter.repaint_requested()) {
            std::fprintf(stderr, "adapter did not consume the routed event batch\n");
            return 1;
        }
        auto commands = std::make_shared<termin::gui_native::CommandModel>();
        commands->append(termin::gui_native::CommandData{"redo", "Redo", {}, "Ctrl+Y"});
        auto* menu_bar = new termin::gui_native::MenuBar();
        document.adopt(menu_bar);
        document.add_root(*menu_bar);
        menu_bar->set_entries({{"edit", "Edit", commands}});
        size_t global_activation_count = 0;
        menu_bar->activated().connect(
            [&global_activation_count](termin::gui_native::MenuBar&,
                                       size_t,
                                       termin::gui_native::CommandId,
                                       const termin::gui_native::CommandData&) { ++global_activation_count; });
        adapter.renderer().set_unhandled_key_handler([menu_bar](const tc_ui_key_event& event) {
            return menu_bar->dispatch_shortcut(event.key, event.modifiers);
        });
        termin::WindowEvent global_shortcut;
        global_shortcut.type = termin::WindowEventType::KeyPressed;
        global_shortcut.key.key = termin::WindowKey::Y;
        global_shortcut.key.modifiers = termin::WindowModifierControl;
        adapter.consume_events(std::span<const termin::WindowEvent>(&global_shortcut, 1));
        if (global_activation_count != 1) {
            std::fprintf(stderr, "window adapter did not route an unhandled global shortcut\n");
            return 1;
        }
        window.clipboard = "borrowed-read";
        if (std::strcmp(tc_ui_document_clipboard_text(document.get()), "borrowed-read") != 0) {
            std::fprintf(stderr, "adapter clipboard read service was not connected\n");
            return 1;
        }
        if (!tc_ui_document_set_clipboard_text(document.get(), "typed", 5) || window.clipboard != "typed") {
            std::fprintf(stderr, "adapter clipboard service was not connected\n");
            return 1;
        }
        size_t before_frame_count = 0;
        adapter.renderer().set_before_frame_callback(
            [&before_frame_count](tgfx::RenderContext2&) { ++before_frame_count; });
        if (!adapter.render_and_present() || before_frame_count != 1 || window.present_count != 1 ||
            !window.last_presented) {
            std::fprintf(stderr, "adapter did not execute renderer extensions before presentation\n");
            return 1;
        }
        graphics->device().wait_idle();
        std::vector<float> pixels(static_cast<size_t>(framebuffer_size.first) *
                                  static_cast<size_t>(framebuffer_size.second) * 4u);
        if (!graphics->device().read_texture_rgba_float(window.last_presented, pixels.data())) {
            std::fprintf(stderr, "window adapter %s composition readback failed\n", tgfx::backend_name(backend));
            return 1;
        }
        size_t non_background_pixels = 0;
        size_t text_pixels = 0;
        for (size_t index = 0; index < pixels.size(); index += 4) {
            // UI fills are authored in sRGB and composed in linear RGB. Dark
            // controls can therefore differ from the linear clear by much less
            // than the old display-referred 0.03 threshold.
            const bool differs_from_clear = std::fabs(pixels[index] - config.clear_linear_color.r) > 0.005f ||
                                            std::fabs(pixels[index + 1] - config.clear_linear_color.g) > 0.005f ||
                                            std::fabs(pixels[index + 2] - config.clear_linear_color.b) > 0.005f;
            non_background_pixels += differs_from_clear;
            text_pixels += pixels[index] > 0.45f || pixels[index + 1] > 0.45f || pixels[index + 2] > 0.45f;
        }
        if (non_background_pixels < 1000 || text_pixels < 8) {
            std::fprintf(stderr,
                         "window adapter %s composition lost control/text batches: "
                         "non_background=%zu text=%zu\n",
                         tgfx::backend_name(backend),
                         non_background_pixels,
                         text_pixels);
            return 1;
        }
        std::printf("window adapter exercised %s: non_background=%zu text=%zu\n",
                    tgfx::backend_name(backend),
                    non_background_pixels,
                    text_pixels);

        const uint64_t initial_revision = document.presentation_revision();
        window.set_presentation({480, 300}, 1.5f);
        termin::WindowEvent scale_changed;
        scale_changed.type = termin::WindowEventType::DisplayScaleChanged;
        if (adapter.consume_events(std::span<const termin::WindowEvent>(&scale_changed, 1)) != 1 ||
            !adapter.repaint_requested()) {
            std::fprintf(stderr, "display-scale event did not request relayout/repaint\n");
            return 1;
        }
        tc_ui_presentation_metrics changed_metrics{};
        tc_ui_rect changed_rect{};
        if (!document.presentation_metrics(changed_metrics) || changed_metrics.density_scale != 1.5f ||
            changed_metrics.physical_extent.width != 480.0f || changed_metrics.physical_extent.height != 300.0f ||
            document.presentation_revision() <= initial_revision || !document.presentation_layout_rect(changed_rect) ||
            changed_rect.width != 320.0f || changed_rect.height != 200.0f) {
            std::fprintf(stderr, "runtime display scale did not preserve logical document extent\n");
            return 1;
        }
        if (!adapter.render_and_present()) {
            std::fprintf(stderr, "adapter did not render after display-scale change\n");
            return 1;
        }

        window.set_presentation({320, 200}, 1.0f);
        scale_changed.type = termin::WindowEventType::Resized;
        adapter.consume_events(std::span<const termin::WindowEvent>(&scale_changed, 1));
        if (!document.presentation_metrics(changed_metrics) || changed_metrics.density_scale != 1.0f ||
            !document.presentation_layout_rect(changed_rect) || changed_rect.width != 320.0f ||
            changed_rect.height != 200.0f || !adapter.render_and_present()) {
            std::fprintf(stderr, "identity desktop scale lost geometry compatibility\n");
            return 1;
        }

        termin::WindowEvent pointer_away;
        pointer_away.type = termin::WindowEventType::PointerMoved;
        pointer_away.pointer.framebuffer_position = {800.0f, 600.0f};
        adapter.consume_events(std::span<const termin::WindowEvent>(&pointer_away, 1));
        document.clear_focus(*input);
        adapter.close();
        if (adapter.is_open() || window.close_count != 0 || graphics->is_closed() || window.text_input_enabled) {
            std::fprintf(stderr, "adapter closed a borrowed owner or leaked platform state\n");
            return 1;
        }

        tc_ui_document_destroy(document_handle);
        graphics->close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "window adapter test failed: %s\n", error.what());
        return 1;
    }
}
