#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>

#include <termin/gui_native/application_host.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/text_input.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

tgfx::BackendType offscreen_backend() {
    if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) return tgfx::BackendType::Vulkan;
    if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11)) return tgfx::BackendType::D3D11;
    return tgfx::BackendType::Null;
}

class PaintProbe final : public termin::gui_native::NativeWidget {
  public:
    PaintProbe() : NativeWidget("OffscreenOverlayPaintProbe") {}

    void paint(tc_ui_document_handle, tc_ui_paint_context*) override {
        ++paint_count;
    }

    size_t paint_count = 0;
};

} // namespace

int main() {
    const tgfx::BackendType backend = offscreen_backend();
    if (backend == tgfx::BackendType::Null) return 77;

    try {
        termin::gui_native::OffscreenGuiApplicationConfig config;
        config.width = 64;
        config.height = 48;
        config.backend = backend;
        config.gui.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.gui.continuous_rendering = false;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;

        termin::gui_native::OffscreenGuiApplication application(std::move(config));
        if (application.graphics().owns_application_domain()) {
            std::fprintf(stderr, "offscreen composition claimed the application graphics domain\n");
            return 1;
        }
        if (application.frame_generation() != 0 || application.latest_frame_texture()) {
            std::fprintf(stderr, "offscreen composition published a frame before rendering\n");
            return 1;
        }
        if (!application.render_frame() || application.frame_generation() != 1 ||
            !application.latest_frame_texture()) {
            std::fprintf(stderr, "offscreen composition did not publish its first frame\n");
            return 1;
        }
        const std::vector<float> first = application.read_frame_rgba_float();
        if (first.size() != 64u * 48u * 4u || first[3] < 0.9f ||
            std::fabs(first[0] - 0.03f) > 0.03f) {
            std::fprintf(stderr, "offscreen readback did not return the configured clear frame\n");
            return 1;
        }

        auto* text_input = new termin::gui_native::TextInput();
        application.document().adopt(text_input);
        application.document().add_root(*text_input);
        application.document().set_focus(*text_input);
        auto* overlay = new PaintProbe();
        application.document().adopt(overlay);
        if (!application.document().show_overlay(*overlay)) {
            std::fprintf(stderr, "offscreen composition did not accept an overlay\n");
            return 1;
        }
        application.render_frame();
        if (overlay->paint_count != 1) {
            std::fprintf(stderr, "offscreen composition did not paint the overlay stack\n");
            return 1;
        }

        termin::WindowEvent key;
        key.type = termin::WindowEventType::KeyPressed;
        key.key.key = termin::WindowKey::A;
        application.push_event(key);
        termin::WindowEvent text;
        text.type = termin::WindowEventType::TextInput;
        std::memcpy(text.text.utf8.data(), "headless", 9);
        application.push_event(text);
        if (application.pump_events() != 2 || text_input->text() != "headless") {
            std::fprintf(stderr, "synthetic input did not traverse the shared GUI host\n");
            return 1;
        }

        application.resize(32, 24);
        if (application.latest_frame_size() != std::pair<int, int>{64, 48} ||
            application.read_frame_rgba_float().size() != 64u * 48u * 4u) {
            std::fprintf(stderr, "resize reinterpreted the previously published frame\n");
            return 1;
        }
        if (!application.render_frame() || application.frame_generation() != 3 ||
            application.framebuffer_size() != std::pair<int, int>{32, 24} ||
            application.read_frame_rgba_float().size() != 32u * 24u * 4u) {
            std::fprintf(stderr, "resized offscreen frame was not published and readable\n");
            return 1;
        }

        auto& document = application.document();
        document.clear_focus(*text_input);
        application.request_close();
        if (!application.should_close()) {
            std::fprintf(stderr, "offscreen close request was not observable\n");
            return 1;
        }
        application.close();
        application.close();
        if (application.is_open() || document.valid()) {
            std::fprintf(stderr, "offscreen composition violated ordered shutdown\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "offscreen application test failed: %s\n", error.what());
        return 1;
    }
}
