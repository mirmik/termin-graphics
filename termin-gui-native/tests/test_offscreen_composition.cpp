#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

#include <tc_profiler.h>

#include <termin/gui_native/command_model.hpp>
#include <termin/gui_native/dynamic_texture_lease.hpp>
#include <termin/gui_native/menu_bar.hpp>
#include <termin/gui_native/offscreen_composition.hpp>
#include <termin/gui_native/text_input.hpp>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>

namespace {

    int find_section(const tc_frame_profile& frame, const char* name, int parent_index) {
        for (int index = 0; index < frame.section_count; ++index) {
            const tc_section_timing& section = frame.sections[index];
            if (section.parent_index == parent_index && std::strcmp(section.name, name) == 0) {
                return index;
            }
        }
        return -1;
    }

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
        if (backend == tgfx::BackendType::Null) {
            std::printf("offscreen composition test skipped: no Vulkan or D3D11 backend compiled\n");
            return 77;
        }

        termin::gui_native::OffscreenGuiCompositionConfig config;
        if (config.backend != termin::gui_native::default_offscreen_backend()) {
            std::fprintf(stderr, "offscreen config did not use canonical default backend\n");
            return 1;
        }
        config.width = 64;
        config.height = 48;
        config.backend = backend;
        config.continuous_rendering = false;
        config.renderer.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;

        termin::gui_native::OffscreenGuiComposition composition(std::move(config));
        if (composition.renderer().frame_sink_encoding() != tgfx::TextureEncoding::Linear) {
            std::fprintf(stderr, "offscreen composition did not declare its linear frame sink\n");
            return 1;
        }
        if (composition.graphics().owns_application_domain()) {
            std::fprintf(stderr, "offscreen composition claimed application graphics\n");
            return 1;
        }
        tc_profiler_clear_history();
        tc_profiler_set_enabled(true);
        tc_profiler_begin_frame();
        tc_profiler_begin_section("UI Compose");
        const bool rendered = composition.render_frame();
        tc_profiler_end_section();
        tc_profiler_begin_section("After Render");
        tc_profiler_end_section();
        tc_profiler_end_frame();
        tc_profiler_set_enabled(false);
        if (!rendered || composition.frame_generation() != 1) {
            std::fprintf(stderr, "offscreen composition did not publish a frame\n");
            return 1;
        }
        if (composition.graphics().device().texture_desc(composition.latest_frame_texture()).format !=
            tgfx::PixelFormat::RGBA16F) {
            std::fprintf(stderr, "offscreen composition quantized DisplayLinear output\n");
            return 1;
        }
        const tc_frame_profile* profile = tc_profiler_history_at(0);
        const int compose = profile ? find_section(*profile, "UI Compose", -1) : -1;
        const char* stages[] = {
            "UI Presentation Sync",
            "UI Begin Frame",
            "UI Before Frame",
            "UI Texture Sync",
            "UI Document Paint",
            "UI Submit",
            "UI Present",
        };
        if (!profile || compose < 0 || find_section(*profile, "After Render", -1) < 0) {
            std::fprintf(stderr, "document renderer profiler stack was unbalanced\n");
            return 1;
        }
        for (const char* stage : stages) {
            if (find_section(*profile, stage, compose) < 0) {
                std::fprintf(stderr, "document renderer profiler stage is missing: %s\n", stage);
                return 1;
            }
        }
        tc_profiler_capture* cadence_capture = tc_profiler_capture_create(1);
        if (!cadence_capture) {
            std::fprintf(stderr, "failed to create cadence-only profiler capture\n");
            return 1;
        }
        tc_profiler_capture_set_active(cadence_capture, true);
        tc_profiler_begin_frame();
        const bool cadence_rendered = composition.render_frame();
        tc_profiler_end_frame();
        const tc_frame_profile* cadence_frame = tc_profiler_capture_at(cadence_capture, 0);
        if (!cadence_rendered || !cadence_frame || cadence_frame->sections_profiled ||
            cadence_frame->section_count != 0) {
            std::fprintf(stderr, "document renderer emitted sections during cadence-only capture\n");
            tc_profiler_capture_destroy(cadence_capture);
            return 1;
        }
        tc_profiler_capture_destroy(cadence_capture);
        const std::vector<float> first = composition.read_frame_rgba_float();
        if (first.size() != 64u * 48u * 4u || first[3] < 0.9f || std::fabs(first[0] - 0.03f) > 0.03f) {
            std::fprintf(stderr, "offscreen readback returned invalid pixels\n");
            return 1;
        }

        auto* input = new termin::gui_native::TextInput();
        composition.document().adopt(input);
        composition.document().add_root(*input);
        composition.document().set_focus(*input);
        composition.push_key(tc_ui_key_event{TC_UI_KEY_DOWN, TC_UI_KEY_A, 0, 0, false});
        composition.push_text("headless");
        if (composition.pump_input() != 2 || input->text() != "headless") {
            std::fprintf(stderr, "normalized input did not reach the tc_ui_document\n");
            return 1;
        }

        auto commands = std::make_shared<termin::gui_native::CommandModel>();
        const termin::gui_native::CommandId redo =
            commands->append(termin::gui_native::CommandData{"redo", "Redo", {}, "Ctrl+Y"});
        commands->append(termin::gui_native::CommandData{"copy-global", "Copy", {}, "Ctrl+C"});
        auto* menu_bar = new termin::gui_native::MenuBar();
        composition.document().adopt(menu_bar);
        composition.document().add_root(*menu_bar);
        menu_bar->set_entries({{"edit", "Edit", commands}});
        std::vector<std::string> activated;
        menu_bar->activated().connect(
            [&activated](termin::gui_native::MenuBar&,
                         size_t,
                         termin::gui_native::CommandId,
                         const termin::gui_native::CommandData& command) { activated.push_back(command.stable_id); });
        composition.renderer().set_unhandled_key_handler([menu_bar](const tc_ui_key_event& event) {
            return menu_bar->dispatch_shortcut(event.key, event.modifiers);
        });
        composition.push_key(tc_ui_key_event{TC_UI_KEY_DOWN, TC_UI_KEY_Y, 0, TC_UI_MOD_CTRL, false});
        if (composition.pump_input() != 1 || activated != std::vector<std::string>{"redo"}) {
            std::fprintf(stderr, "offscreen composition did not route an unhandled global shortcut\n");
            return 1;
        }
        commands->set_enabled(redo, false);
        composition.push_key(tc_ui_key_event{TC_UI_KEY_DOWN, TC_UI_KEY_Y, 0, TC_UI_MOD_CTRL, false});
        composition.push_key(tc_ui_key_event{TC_UI_KEY_DOWN, TC_UI_KEY_C, 0, TC_UI_MOD_CTRL, false});
        if (composition.pump_input() != 2 || activated != std::vector<std::string>{"redo"}) {
            std::fprintf(stderr, "offscreen composition bypassed disabled or focused-widget shortcuts\n");
            return 1;
        }

        termin::gui_native::DynamicTextureLease lease(composition.renderer());
        std::vector<uint8_t> pixels(3u * 2u * 4u, 127);
        lease.set_rgba8(3, 2, pixels, tgfx::TextureEncoding::SRGB);
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
        if (!composition.render_frame() || composition.framebuffer_size() != std::pair<int, int>{32, 24} ||
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
        return 1;
    }
}
