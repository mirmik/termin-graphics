#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>
#include <utility>

#include <termin/gui_native/application_host.hpp>

#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/shader_artifact_resolver.hpp>

namespace {

class CountingExtension final : public termin::gui_native::GuiWindowFrameExtension {
  public:
    CountingExtension(int& before, int& after, int& detach)
        : before_(before), after_(after), detach_(detach) {}

    void before_ui_frame(termin::gui_native::GuiWindowFrame& frame) override {
        ++before_;
        if (&frame.graphics() != &frame.host().graphics()) {
            throw std::logic_error("frame extension received another graphics domain");
        }
    }
    void after_ui_frame(termin::gui_native::GuiWindowFrame&) override { ++after_; }
    void detach(termin::gui_native::GuiApplicationHost&) noexcept override { ++detach_; }

  private:
    int& before_;
    int& after_;
    int& detach_;
};

bool unavailable_window_system(const char* message) {
    return message &&
           (std::strstr(message, "No available video device") ||
            std::strstr(message, "Vulkan support is either not configured in SDL") ||
            std::strstr(message, "Could not initialize OpenGL") || std::strstr(message, "DISPLAY"));
}

} // namespace

int main() {
    try {
        termin::gui_native::StandaloneGuiApplicationConfig config;
        config.gui.window = termin::WindowConfig{"application host lifecycle", 320, 200};
        config.gui.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;
        config.gui.continuous_rendering = false;

        termin::gui_native::StandaloneGuiApplication application(std::move(config));
        auto& host = application.window_host();
        int extension_before = 0;
        int extension_after = 0;
        int extension_detach = 0;
        int callback_before = 0;
        int callback_after = 0;
        host.set_frame_callbacks(
            [&callback_before](termin::gui_native::GuiFrame&) { ++callback_before; },
            [&callback_after](termin::gui_native::GuiFrame&) { ++callback_after; });
        host.install_frame_extension(std::make_unique<CountingExtension>(
            extension_before, extension_after, extension_detach));
        bool live_document_close_rejected = false;
        try {
            application.document().close();
        } catch (const std::logic_error&) {
            live_document_close_rejected = true;
        }
        if (!live_document_close_rejected || !application.document().valid()) {
            std::fprintf(stderr, "Document did not diagnose a live GuiWindowHost\n");
            return 1;
        }
        if (!host.render_frame()) {
            std::fprintf(stderr, "application host produced no initial frame\n");
            return 1;
        }
        const size_t first_frame_count = host.rendered_frame_count();
        if (!host.tick() || host.rendered_frame_count() != first_frame_count) {
            std::fprintf(stderr, "event-driven host rendered without a repaint request\n");
            return 1;
        }
        std::thread repaint_worker([&host] { host.request_repaint(); });
        repaint_worker.join();
        if (!host.repaint_requested() || !host.tick() ||
            host.rendered_frame_count() != first_frame_count + 1) {
            std::fprintf(stderr, "cross-thread repaint request was rejected\n");
            return 1;
        }
        const auto initial_desc = host.device().texture_desc(host.color_target());
        if (initial_desc.width != static_cast<uint32_t>(host.framebuffer_width()) ||
            initial_desc.height != static_cast<uint32_t>(host.framebuffer_height())) {
            std::fprintf(stderr, "unexpected initial color target dimensions\n");
            return 1;
        }

        host.window().set_size(400, 240);
        for (int attempt = 0; attempt < 50; ++attempt) {
            host.pump_events();
            const auto [width, height] = host.window().window_size();
            if (width == 400 && height == 240)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!host.render_frame()) {
            std::fprintf(stderr, "application host produced no resized frame\n");
            return 1;
        }
        const auto resized_desc = host.device().texture_desc(host.color_target());
        if (resized_desc.width != static_cast<uint32_t>(host.framebuffer_width()) ||
            resized_desc.height != static_cast<uint32_t>(host.framebuffer_height()) ||
            (resized_desc.width == initial_desc.width &&
             resized_desc.height == initial_desc.height)) {
            std::fprintf(stderr, "color target was not recreated after resize\n");
            return 1;
        }
        host.wait_idle();

        // Two hosts borrow the same domain without another device/interop
        // claim. The second uses the lower-level injected-window path.
        auto& session = application.graphics_session();
        termin::gui_native::Document second_document;
        termin::gui_native::GuiWindowConfig second_config;
        second_config.window = {"borrowed host replacement", 240, 160};
        second_config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        auto injected_window = session.create_window(second_config.window);
        termin::gui_native::GuiWindowHost second_host(session.graphics(), second_document,
                                                      second_config, std::move(injected_window));
        if (&second_host.graphics() != &session.graphics() || !second_host.render_frame()) {
            std::fprintf(stderr, "injected GuiWindowHost did not reuse the graphics domain\n");
            return 1;
        }
        bool live_window_close_rejected = false;
        try {
            session.close();
        } catch (const std::logic_error&) {
            live_window_close_rejected = true;
        }
        if (!live_window_close_rejected || session.graphics().is_closed()) {
            std::fprintf(stderr, "session did not diagnose a live GuiWindowHost\n");
            return 1;
        }
        host.close();
        if (session.graphics().is_closed() || !second_host.render_frame()) {
            std::fprintf(stderr, "closing one GuiWindowHost damaged the shared domain\n");
            return 1;
        }
        if (extension_before == 0 || extension_before != extension_after ||
            callback_before != extension_before || callback_after != extension_after ||
            extension_detach != 1) {
            std::fprintf(stderr, "frame extension lifecycle was not deterministic\n");
            return 1;
        }
        second_host.close();
        application.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "application host lifecycle failed: %s\n", error.what());
        return unavailable_window_system(error.what()) ? 77 : 1;
    }
}
