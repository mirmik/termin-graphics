#include <cmath>
#include <cstdio>
#include <exception>
#include <vector>

#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <termin/gui_native/offscreen_composition.hpp>
#include <termin/gui_native/text_input.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

tgfx::BackendType offscreen_backend() {
    if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        return tgfx::BackendType::Vulkan;
    }
    if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11)) {
        return tgfx::BackendType::D3D11;
    }
    return tgfx::BackendType::Null;
}

} // namespace

int main() {
    try {
        const tgfx::BackendType backend = offscreen_backend();
        if (backend == tgfx::BackendType::Null) return 77;

        termin::gui_native::OffscreenGuiCompositionConfig config;
        config.width = 64;
        config.height = 48;
        config.backend = backend;
        config.continuous_rendering = false;
        config.renderer.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;

        termin::gui_native::OffscreenGuiComposition composition(
            std::move(config));
        if (composition.graphics().owns_application_domain()) {
            std::fprintf(stderr, "offscreen composition claimed application graphics\n");
            return 1;
        }
        if (!composition.render_frame() || composition.frame_generation() != 1) {
            std::fprintf(stderr, "offscreen composition did not publish a frame\n");
            return 1;
        }
        const std::vector<float> first = composition.read_frame_rgba_float();
        if (first.size() != 64u * 48u * 4u || first[3] < 0.9f ||
            std::fabs(first[0] - 0.03f) > 0.03f) {
            std::fprintf(stderr, "offscreen readback returned invalid pixels\n");
            return 1;
        }

        auto* input = new termin::gui_native::TextInput();
        composition.document().adopt(input);
        composition.document().add_root(*input);
        composition.document().set_focus(*input);
        composition.push_key(tc_ui_key_event{
            TC_UI_KEY_DOWN, TC_UI_KEY_A, 0, 0, false});
        composition.push_text("headless");
        if (composition.pump_input() != 2 || input->text() != "headless") {
            std::fprintf(stderr, "normalized input did not reach the Document\n");
            return 1;
        }

        termin::gui_native::DynamicTextureLease lease(composition.renderer());
        std::vector<uint8_t> pixels(3u * 2u * 4u, 127);
        lease.set_rgba8(3, 2, pixels);
        if (lease.empty()) {
            std::fprintf(stderr, "renderer-bound texture lease was not created\n");
            return 1;
        }

        composition.resize(32, 24);
        if (composition.latest_frame_size() != std::pair<int, int>{64, 48} ||
            composition.read_frame_rgba_float().size() != 64u * 48u * 4u) {
            std::fprintf(stderr, "resize invalidated the last published frame too early\n");
            return 1;
        }
        if (!composition.render_frame() ||
            composition.framebuffer_size() != std::pair<int, int>{32, 24} ||
            composition.read_frame_rgba_float().size() != 32u * 24u * 4u) {
            std::fprintf(stderr, "resized offscreen frame was not published\n");
            return 1;
        }

        composition.request_close();
        if (!composition.should_close()) {
            std::fprintf(stderr, "composition close request was not observable\n");
            return 1;
        }
        composition.document().clear_focus(*input);
        composition.close();
        if (composition.is_open() || !lease.released()) {
            std::fprintf(stderr, "offscreen shutdown order was violated\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "offscreen composition test failed: %s\n", error.what());
        return 77;
    }
}
