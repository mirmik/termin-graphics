#include <termin/gui_native/offscreen_composition.hpp>
#include <termin/gui_native/scene_view3d.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <termin_visual_scene/items/static_mesh_item3d.hpp>
#include <tgfx2/device_factory.hpp>

namespace {

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
        return result;
    }

    std::shared_ptr<termin::Mesh3> textured_quad() {
        auto mesh = std::make_shared<termin::Mesh3>(
            std::vector<termin::Vec3f>{{-0.9f, -0.9f, 0.5f},
                                       {0.9f, -0.9f, 0.5f},
                                       {0.9f, 0.9f, 0.5f},
                                       {-0.9f, 0.9f, 0.5f}},
            std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3},
            "scene-view3d-textured-quad");
        mesh->uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        return mesh;
    }

    std::shared_ptr<termin::visual::BaseColorTextureData3D> two_color_texture(bool swapped = false) {
        auto texture = std::make_shared<termin::visual::BaseColorTextureData3D>();
        texture->width = 2;
        texture->height = 1;
        texture->rgba8 = swapped ? std::vector<std::uint8_t>{0, 255, 0, 255, 255, 0, 0, 255}
                                 : std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255};
        return texture;
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
    auto static_mesh = std::make_unique<termin::visual::StaticMeshItem3D>(textured_quad());
    auto* static_mesh_ptr = static_mesh.get();
    static_mesh_ptr->set_base_color_texture(two_color_texture());
    const auto item = scene.adopt(std::move(static_mesh));
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
            if (action.part == 1)
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
        const auto pixel = [&](int x, int y, int channel) {
            return pixels[(static_cast<std::size_t>(y) * 64 + x) * 4 + channel];
        };
        if (pixel(16, 32, 0) <= pixel(16, 32, 1) + 0.2f ||
            pixel(48, 32, 1) <= pixel(48, 32, 0) + 0.2f) {
            std::fprintf(stderr, "SceneView3D base-color texture was not spatially distinguishable\n");
            return 1;
        }

        static_mesh_ptr->set_base_color_texture(two_color_texture(true));
        view->invalidate_scene();
        if (!composition.render_frame()) {
            std::fprintf(stderr, "SceneView3D did not render a replacement base-color texture\n");
            return 1;
        }
        const std::vector<float> replaced = composition.read_frame_rgba_float();
        const auto replaced_pixel = [&](int x, int y, int channel) {
            return replaced[(static_cast<std::size_t>(y) * 64 + x) * 4 + channel];
        };
        if (replaced_pixel(16, 32, 1) <= replaced_pixel(16, 32, 0) + 0.2f ||
            replaced_pixel(48, 32, 0) <= replaced_pixel(48, 32, 1) + 0.2f) {
            std::fprintf(stderr, "SceneView3D retained a stale base-color texture after replacement\n");
            return 1;
        }

        composition.push_pointer(tc_ui_pointer_event{TC_UI_POINTER_DOWN, 32.0f, 32.0f, 0, 1, 0});
        composition.push_pointer(tc_ui_pointer_event{TC_UI_POINTER_UP, 32.0f, 32.0f, 0, 1, 0});
        if (composition.pump_input() != 2 || actions != 1) {
            std::fprintf(stderr, "SceneView3D did not route the headless click to its item\n");
            return 1;
        }
        if (!scene.destroy(*item)) {
            std::fprintf(stderr, "failed to destroy SceneView3D textured item\n");
            return 1;
        }
        view->invalidate_scene();
        if (!composition.render_frame()) {
            std::fprintf(stderr, "SceneView3D did not release a destroyed textured item\n");
            return 1;
        }
        composition.close();
    }
    tc_visual_scene3d_destroy(scene_handle);
    return EXIT_SUCCESS;
}
