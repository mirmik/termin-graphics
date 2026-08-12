#include <termin/gui_native/offscreen_composition.hpp>
#include <termin/gui_native/scene_view3d.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <termin_visual_scene/items/primitive_item3d.hpp>
#include <tgfx2/device_factory.hpp>

namespace {

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
        return result;
    }

    std::shared_ptr<termin::visual::PrimitiveGeometry3D> triangle() {
        auto geometry = std::make_shared<termin::visual::PrimitiveGeometry3D>();
        geometry->vertices = {
            {{-0.7f, -0.7f, 0.5f}, {1.0f, 0.1f, 0.1f, 1.0f}},
            {{0.7f, -0.7f, 0.5f}, {0.1f, 1.0f, 0.1f, 1.0f}},
            {{0.0f, 0.7f, 0.5f}, {0.1f, 0.1f, 1.0f, 1.0f}},
        };
        geometry->triangles = {0, 1, 2};
        geometry->triangle_parts = {7};
        return geometry;
    }

    tgfx::BackendType backend() {
        if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan))
            return tgfx::BackendType::Vulkan;
        if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11))
            return tgfx::BackendType::D3D11;
        return tgfx::BackendType::Null;
    }

} // namespace

int main() {
    const tgfx::BackendType selected = backend();
    if (selected == tgfx::BackendType::Null) {
        std::printf("SceneView3D offscreen test skipped: no supported backend\n");
        return 77;
    }

    const tc_visual_scene3d_handle scene_handle = tc_visual_scene3d_create();
    termin::visual::TcVisualScene3D scene{scene_handle};
    const auto item = scene.adopt(std::make_unique<termin::visual::PrimitiveItem3D>(triangle()));
    if (!item) {
        std::fprintf(stderr, "failed to create SceneView3D smoke item\n");
        return 1;
    }

    int actions = 0;
    {
        termin::gui_native::OffscreenGuiCompositionConfig config;
        config.width = 64;
        config.height = 64;
        config.backend = selected;
        config.continuous_rendering = false;
        config.renderer.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        config.shader_compiler_path = TERMIN_GUI_NATIVE_TEST_SHADERC;
        termin::gui_native::OffscreenGuiComposition composition(std::move(config));

        auto* view = new termin::gui_native::SceneView3D(scene);
        composition.document().adopt(view);
        composition.document().add_root(*view);
        int provider_calls = 0;
        view->set_camera_provider(
            [&](termin::gui_native::ViewportSurfaceSize size) -> std::optional<termin::gui_native::SceneView3DCamera> {
                ++provider_calls;
                if (size != termin::gui_native::ViewportSurfaceSize{64, 64})
                    return std::nullopt;
                const tc_mat44 identity = identity_matrix();
                return termin::gui_native::SceneView3DCamera{identity, identity, {0.0, 0.0, 0.0}};
            });
        view->interaction().set_action_handler(*item, [&](const termin::visual::ActionEvent3D& action) {
            if (action.part == 7)
                ++actions;
        });

        if (!composition.render_frame() || provider_calls == 0 || view->texture_id() == 0 ||
            view->framebuffer_size() != termin::gui_native::ViewportSurfaceSize{64, 64}) {
            std::fprintf(stderr, "SceneView3D did not publish its offscreen framebuffer\n");
            return 1;
        }
        const std::vector<float> pixels = composition.read_frame_rgba_float();
        float brightest = 0.0f;
        for (size_t index = 0; index + 3 < pixels.size(); index += 4) {
            brightest = std::max(brightest, std::max({pixels[index], pixels[index + 1], pixels[index + 2]}));
        }
        if (brightest < 0.3f) {
            std::fprintf(stderr, "SceneView3D triangle was not visible in headless readback\n");
            return 1;
        }

        composition.push_pointer(tc_ui_pointer_event{TC_UI_POINTER_DOWN, 32.0f, 32.0f, 0, 1, 0});
        composition.push_pointer(tc_ui_pointer_event{TC_UI_POINTER_UP, 32.0f, 32.0f, 0, 1, 0});
        if (composition.pump_input() != 2 || actions != 1) {
            std::fprintf(stderr, "SceneView3D did not route the headless click to its item\n");
            return 1;
        }
        composition.close();
    }
    tc_visual_scene3d_destroy(scene_handle);
    return EXIT_SUCCESS;
}
