#include <tcplot/gui_native/plot3d.hpp>
#include <tcplot/gui_native/widget_registration.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

#include <termin/gui_native/offscreen_composition.hpp>
#include <termin/gui_native/tc_widget_registry.h>
#include <tgfx2/device_factory.hpp>

namespace {

    tgfx::BackendType backend() {
        if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan))
            return tgfx::BackendType::Vulkan;
        if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11))
            return tgfx::BackendType::D3D11;
        return tgfx::BackendType::Null;
    }

    bool camera_changed(const tc_orbit_camera3d_state& before, const tc_orbit_camera3d_state& after) {
        return std::fabs(before.azimuth - after.azimuth) > 1.0e-4f ||
               std::fabs(before.elevation - after.elevation) > 1.0e-4f ||
               std::fabs(before.distance - after.distance) > 1.0e-4f;
    }

} // namespace

int main() {
    try {
        if (!tcplot::gui_native::register_plot_widget_types() ||
            !tc_widget_registry_has("termin.gui.Plot3D")) {
            std::fprintf(stderr, "Plot3D widget registration failed\n");
            return 1;
        }
        const tgfx::BackendType selected = backend();
        if (selected == tgfx::BackendType::Null) {
            std::printf("tcplot native Plot3D test skipped: no GPU backend compiled\n");
            return 77;
        }

        termin::gui_native::OffscreenGuiCompositionConfig config;
        config.width = 360;
        config.height = 260;
        config.backend = selected;
        config.continuous_rendering = false;
        config.renderer.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;
        termin::gui_native::OffscreenGuiComposition composition(std::move(config));

        auto* plot = new tcplot::gui_native::Plot3D();
        composition.document().adopt(plot);
        if (!composition.document().add_root(*plot)) {
            std::fprintf(stderr, "failed to add Plot3D root\n");
            return 1;
        }

        constexpr size_t point_count = 120;
        std::vector<double> x(point_count);
        std::vector<double> y(point_count);
        std::vector<double> z(point_count);
        for (size_t index = 0; index < point_count; ++index) {
            const double t = static_cast<double>(index) * 0.12;
            x[index] = std::cos(t);
            y[index] = std::sin(t);
            z[index] = t * 0.08;
        }
        const tc_line_item3d_style style{0.2f, 0.8f, 1.0f, 1.0f, 2.0f};
        const tc_plot_item3d_handle line = plot->add_line(x, y, z, style);
        if (line.scene_id == 0 || plot->item_count() == 0) {
            std::fprintf(stderr, "Plot3D retained line creation failed\n");
            return 1;
        }
        plot->set_axis_labels("x", "y", "z");
        plot->fit_camera();

        if (!composition.render_frame() || plot->texture_id() == 0) {
            std::fprintf(stderr, "Plot3D did not publish a texture during document rendering\n");
            return 1;
        }
        const std::vector<float> pixels = composition.read_frame_rgba_float();
        size_t changed_pixels = 0;
        for (size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
            if (std::fabs(pixels[offset] - 0.03f) > 0.04f ||
                std::fabs(pixels[offset + 1] - 0.035f) > 0.04f ||
                std::fabs(pixels[offset + 2] - 0.045f) > 0.04f) {
                ++changed_pixels;
            }
        }
        if (changed_pixels < 100) {
            std::fprintf(stderr, "Plot3D texture was not visible in native UI output\n");
            return 1;
        }

        tc_orbit_camera3d_state before{};
        tc_orbit_camera3d_state after{};
        if (!plot->camera(before)) {
            std::fprintf(stderr, "failed to read Plot3D camera\n");
            return 1;
        }
        composition.push_pointer({TC_UI_POINTER_MOVE, 100.0f, 100.0f, 0, 0, 0, 0.0f, 0.0f});
        composition.push_pointer({TC_UI_POINTER_DOWN, 100.0f, 100.0f, 0, 1, 0, 0.0f, 0.0f});
        composition.push_pointer({TC_UI_POINTER_MOVE, 145.0f, 120.0f, 0, 0, 0, 0.0f, 0.0f});
        composition.push_pointer({TC_UI_POINTER_UP, 145.0f, 120.0f, 0, 1, 0, 0.0f, 0.0f});
        if (composition.pump_input() != 4 || !plot->camera(after) || !camera_changed(before, after)) {
            std::fprintf(stderr, "Plot3D native pointer interaction did not orbit the camera\n");
            return 1;
        }

        plot->release_render_resources();
        if (plot->texture_id() != 0) {
            std::fprintf(stderr, "Plot3D GPU release retained a stale texture\n");
            return 1;
        }
        composition.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "tcplot native Plot3D test failed: %s\n", error.what());
        return 1;
    }
}
