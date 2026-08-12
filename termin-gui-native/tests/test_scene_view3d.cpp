#ifdef NDEBUG
#undef NDEBUG
#endif

#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>

#include <termin_visual_scene/items/primitive_item3d.hpp>

using namespace termin::gui_native;
using namespace termin::visual;

namespace {

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
        return result;
    }

    SceneView3DCamera identity_camera(double x = 0.0) {
        return {identity_matrix(), identity_matrix(), {x, 0.0, 0.0}};
    }

    std::shared_ptr<PrimitiveGeometry3D> centered_triangle() {
        auto geometry = std::make_shared<PrimitiveGeometry3D>();
        geometry->vertices = {
            {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{0.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        };
        geometry->triangles = {0, 1, 2};
        geometry->triangle_parts = {42};
        return geometry;
    }

    void test_independent_views_resize_and_ray_projection() {
        const tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        const tc_visual_scene3d_handle left_scene_handle = tc_visual_scene3d_create();
        const tc_visual_scene3d_handle right_scene_handle = tc_visual_scene3d_create();
        TcVisualScene3D left_scene{left_scene_handle};
        TcVisualScene3D right_scene{right_scene_handle};

        auto* left = new SceneView3D(left_scene);
        auto* right = new SceneView3D(right_scene);
        const tc_widget_handle left_handle = document.adopt(left);
        const tc_widget_handle right_handle = document.adopt(right);
        assert(document.add_root(*left));
        assert(document.add_root(*right));
        left->layout(document_handle, {0.0f, 0.0f, 200.0f, 100.0f});
        right->layout(document_handle, {220.0f, 0.0f, 80.0f, 160.0f});
        left->set_camera(identity_camera(1.0));
        right->set_camera(identity_camera(2.0));
        int provider_calls = 0;
        right->set_camera_provider([&](ViewportSurfaceSize size) -> std::optional<SceneView3DCamera> {
            ++provider_calls;
            assert((size == ViewportSurfaceSize{80, 160}));
            return identity_camera(3.0);
        });

        assert((left->framebuffer_size() == ViewportSurfaceSize{200, 100}));
        assert((right->framebuffer_size() == ViewportSurfaceSize{80, 160}));
        assert(left->camera().world_position.x == 1.0);
        // Providers are sampled by the render-preparation phase; the last
        // valid camera remains usable for input until then.
        assert(right->camera().world_position.x == 2.0);
        assert(provider_calls == 0);
        const auto left_ray = left->world_ray(100.0f, 50.0f);
        const auto right_ray = right->world_ray(260.0f, 80.0f);
        assert(left_ray && right_ray);
        assert(std::abs(left_ray->origin.x) < 1.0e-12);
        assert(std::abs(left_ray->origin.y) < 1.0e-12);
        assert(std::abs(left_ray->origin.z) < 1.0e-12);
        assert(std::abs(left_ray->direction.z - 1.0) < 1.0e-12);
        assert(std::abs(right_ray->direction.z - 1.0) < 1.0e-12);

        left->layout(document_handle, {0.0f, 0.0f, 301.0f, 181.0f});
        assert((left->framebuffer_size() == ViewportSurfaceSize{301, 181}));
        const auto resized_ray = left->world_ray(150.5f, 90.5f);
        assert(resized_ray);
        assert(std::abs(resized_ray->direction.x) < 1.0e-12);
        assert(std::abs(resized_ray->direction.y) < 1.0e-12);

        assert(tc_ui_document_destroy_widget(document_handle, left_handle));
        assert(tc_ui_document_destroy_widget(document_handle, right_handle));
        tc_visual_scene3d_destroy(left_scene_handle);
        tc_visual_scene3d_destroy(right_scene_handle);
        tc_ui_document_destroy(document_handle);
    }

    void test_item_capture_is_local_and_fallback_receives_only_unhandled_pointer() {
        const tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        const tc_visual_scene3d_handle scene_handle = tc_visual_scene3d_create();
        TcVisualScene3D scene{scene_handle};
        const auto item_handle = scene.adopt(std::make_unique<PrimitiveItem3D>(centered_triangle()));
        assert(item_handle);

        auto* view = new SceneView3D(scene);
        const tc_widget_handle view_handle = document.adopt(view);
        assert(document.add_root(*view));
        view->layout(document_handle, {10.0f, 20.0f, 200.0f, 100.0f});
        view->set_camera(identity_camera());
        int actions = 0;
        int fallback_calls = 0;
        view->interaction().set_action_handler(*item_handle, [&](const ActionEvent3D& action) {
            assert(action.part == 42);
            ++actions;
        });
        view->set_fallback_pointer_handler(
            [&](SceneView3D&, const tc_ui_pointer_event&, const std::optional<termin::Ray3>& ray) {
                assert(ray);
                ++fallback_calls;
                return true;
            });
        const auto dispatch = [&](tc_ui_pointer_event event) {
            return view->pointer_event(document_handle, &event);
        };

        assert(dispatch(tc_ui_pointer_event{TC_UI_POINTER_DOWN, 110.0f, 70.0f, 0, 1, 0}) == TC_UI_EVENT_HANDLED);
        assert(fallback_calls == 0);
        assert(tc_widget_handle_eq(document.pointer_capture(), view_handle));
        assert(dispatch(tc_ui_pointer_event{TC_UI_POINTER_MOVE, 400.0f, 400.0f, 0, 0, 0}) == TC_UI_EVENT_HANDLED);
        assert(fallback_calls == 0);
        assert(dispatch(tc_ui_pointer_event{TC_UI_POINTER_UP, 110.0f, 70.0f, 0, 1, 0}) == TC_UI_EVENT_HANDLED);
        assert(actions == 1);
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        assert(dispatch(tc_ui_pointer_event{TC_UI_POINTER_DOWN, 205.0f, 115.0f, 0, 1, 0}) == TC_UI_EVENT_HANDLED);
        assert(fallback_calls == 1);
        assert(tc_widget_handle_eq(document.pointer_capture(), view_handle));
        assert(dispatch(tc_ui_pointer_event{TC_UI_POINTER_UP, 205.0f, 115.0f, 0, 1, 0}) == TC_UI_EVENT_HANDLED);
        assert(fallback_calls == 2);
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        assert(tc_ui_document_destroy_widget(document_handle, view_handle));
        tc_visual_scene3d_destroy(scene_handle);
        tc_ui_document_destroy(document_handle);
    }

    void test_destroy_cancels_capture_and_releases_callbacks_without_owning_scene() {
        const tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        const tc_visual_scene3d_handle scene_handle = tc_visual_scene3d_create();
        TcVisualScene3D scene{scene_handle};
        auto* view = new SceneView3D(scene);
        const tc_widget_handle view_handle = document.adopt(view);
        assert(document.add_root(*view));
        view->layout(document_handle, {0.0f, 0.0f, 100.0f, 100.0f});
        view->set_camera(identity_camera());

        auto lifetime = std::make_shared<int>(7);
        std::weak_ptr<int> weak_lifetime = lifetime;
        int cancel_calls = 0;
        view->set_fallback_pointer_handler([lifetime, &cancel_calls](SceneView3D&,
                                                                     const tc_ui_pointer_event& event,
                                                                     const std::optional<termin::Ray3>&) {
            if (event.type == TC_UI_POINTER_CANCEL)
                ++cancel_calls;
            return true;
        });
        lifetime.reset();
        tc_ui_pointer_event down{TC_UI_POINTER_DOWN, 90.0f, 90.0f, 0, 1, 0};
        assert(view->pointer_event(document_handle, &down) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.pointer_capture(), view_handle));

        assert(tc_ui_document_destroy_widget(document_handle, view_handle));
        assert(cancel_calls == 1);
        assert(weak_lifetime.expired());
        assert(tc_visual_scene3d_is_valid(scene_handle));
        tc_visual_scene3d_destroy(scene_handle);
        tc_ui_document_destroy(document_handle);
    }

} // namespace

int main() {
    test_independent_views_resize_and_ray_projection();
    test_item_capture_is_local_and_fallback_receives_only_unhandled_pointer();
    test_destroy_cancels_capture_and_releases_callbacks_without_owning_scene();
    return EXIT_SUCCESS;
}
