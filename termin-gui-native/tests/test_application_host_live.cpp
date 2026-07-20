#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>
#include <utility>

#include <termin/gui_native/application_host.hpp>

#include <tgfx2/i_render_device.hpp>

namespace {

bool unavailable_window_system(const char* message) {
    return message &&
        (std::strstr(message, "No available video device") ||
         std::strstr(message, "Vulkan support is either not configured in SDL") ||
         std::strstr(message, "Could not initialize OpenGL") ||
         std::strstr(message, "DISPLAY"));
}

} // namespace

int main() {
    try {
        termin::gui_native::Document document;
        termin::gui_native::ApplicationHostConfig config;
        config.window = termin::WindowConfig{"application host lifecycle", 320, 200};
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;
        config.continuous_rendering = false;

        termin::gui_native::ApplicationHost host(document, std::move(config));
        if (!host.render_frame()) {
            std::fprintf(stderr, "application host produced no initial frame\n");
            return 1;
        }
        const size_t first_frame_count = host.rendered_frame_count();
        if (!host.tick() || host.rendered_frame_count() != first_frame_count) {
            std::fprintf(stderr, "event-driven host rendered without a repaint request\n");
            return 1;
        }
        int deferred_step = 0;
        host.defer([&host, &deferred_step] {
            deferred_step = 1;
            host.defer([&deferred_step] { deferred_step = 2; });
        });
        if (!host.repaint_requested() || !host.tick() || deferred_step != 1 ||
            host.rendered_frame_count() != first_frame_count + 1) {
            std::fprintf(stderr, "deferred repaint was not executed deterministically\n");
            return 1;
        }
        if (!host.tick() || deferred_step != 2 ||
            host.rendered_frame_count() != first_frame_count + 2) {
            std::fprintf(stderr, "nested deferred work did not wait for the next tick\n");
            return 1;
        }
        const auto initial_desc =
            host.device().texture_desc(host.color_target());
        if (initial_desc.width != static_cast<uint32_t>(host.framebuffer_width()) ||
            initial_desc.height != static_cast<uint32_t>(host.framebuffer_height())) {
            std::fprintf(stderr, "unexpected initial color target dimensions\n");
            return 1;
        }

        host.window().set_size(400, 240);
        for (int attempt = 0; attempt < 50; ++attempt) {
            host.pump_events();
            const auto [width, height] = host.window().window_size();
            if (width == 400 && height == 240) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!host.render_frame()) {
            std::fprintf(stderr, "application host produced no resized frame\n");
            return 1;
        }
        const auto resized_desc =
            host.device().texture_desc(host.color_target());
        if (resized_desc.width != static_cast<uint32_t>(host.framebuffer_width()) ||
            resized_desc.height != static_cast<uint32_t>(host.framebuffer_height()) ||
            (resized_desc.width == initial_desc.width &&
             resized_desc.height == initial_desc.height)) {
            std::fprintf(stderr, "color target was not recreated after resize\n");
            return 1;
        }
        host.wait_idle();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "application host lifecycle failed: %s\n", error.what());
        return unavailable_window_system(error.what()) ? 77 : 1;
    }
}
