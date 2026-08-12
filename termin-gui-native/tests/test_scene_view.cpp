#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include <termin_visual_scene/builtin_items2d.hpp>
#include <termin_visual_scene/interaction2d.hpp>

using namespace termin::gui_native;
using namespace termin::visual;

namespace {

    std::vector<const tc_ui_draw_command*> commands_of_type(const tc_ui_draw_list* draw_list,
                                                            tc_ui_draw_command_type type) {
        std::vector<const tc_ui_draw_command*> result;
        for (std::size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const auto* command = tc_ui_draw_list_command_at(draw_list, index);
            if (command && command->type == type)
                result.push_back(command);
        }
        return result;
    }

    tgfx::FillPaint fill(float r, float g, float b, float a = 1.0f) {
        return {{r, g, b, a}};
    }

    bool handle_eq(GraphicItemHandle left, GraphicItemHandle right) {
        return left.scene_id == right.scene_id && left.index == right.index && left.generation == right.generation;
    }

    template <typename Item, typename... Args>
    Item* adopt(TcVisualScene& scene, GraphicItem2D* parent, Args&&... args) {
        auto item = std::make_unique<Item>(std::forward<Args>(args)...);
        auto* result = item.get();
        assert(scene.adopt(std::move(item), parent));
        return result;
    }

    void test_scene_uses_generation_handles_and_canonical_hit_testing() {
        const auto scene_handle = tc_visual_scene_create();
        TcVisualScene scene{scene_handle};
        auto* back =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 100.0f, 80.0f}, fill(0.2f, 0.3f, 0.4f));
        auto* child = adopt<RectItem2D>(scene, back, termin::Rect2f{0.0f, 0.0f, 20.0f, 10.0f}, fill(0.7f, 0.2f, 0.2f));
        auto* front =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 100.0f, 80.0f}, fill(0.3f, 0.6f, 0.4f));
        back->set_local_transform(termin::Affine2f::translation({10.0f, 20.0f}));
        child->set_local_transform(termin::Affine2f::translation({5.0f, 7.0f}));
        child->set_z_order(10);
        front->set_local_transform(termin::Affine2f::translation({20.0f, 30.0f}));
        front->set_z_order(20);
        const auto back_handle = back->handle();

        const auto front_hit = hit_test(scene, {25.0f, 35.0f});
        assert(front_hit && handle_eq(*front_hit, front->handle()));
        front->set_enabled(false);
        const auto child_hit = hit_test(scene, {16.0f, 28.0f});
        assert(child_hit && handle_eq(*child_hit, child->handle()));
        assert(scene.destroy(back_handle));
        assert(!scene.contains(back_handle));
        assert(scene.contains(front->handle()));

        auto* replacement =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 20.0f, 20.0f}, fill(0.8f, 0.8f, 0.2f));
        assert(replacement->handle().generation != back_handle.generation ||
               replacement->handle().index != back_handle.index);
        tc_visual_scene_destroy(scene_handle);
    }

    void test_view_transform_forwarding_pan_zoom_and_draw_list_bridge() {
        const auto document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        const auto scene_handle = tc_visual_scene_create();
        TcVisualScene scene{scene_handle};
        auto* item =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 80.0f, 40.0f}, fill(0.8f, 0.2f, 0.1f));
        item->set_local_transform(termin::Affine2f::translation({10.0f, 20.0f}));
        auto* view = new SceneView(scene);
        const auto view_handle = document.adopt(view);
        assert(document.add_root(*view));
        document.layout_roots({100.0f, 50.0f, 400.0f, 300.0f});
        assert(view->world_to_screen({10.0f, 20.0f}).x == 110.0f);

        auto* draw_list = tc_ui_draw_list_create();
        auto* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        assert(commands_of_type(draw_list, TC_UI_DRAW_CANVAS2D_LIST).size() == 1);
        assert(!commands_of_type(draw_list, TC_UI_DRAW_LINE).empty());

        bool forwarded = false;
        view->set_pointer_handler([&](SceneView&, tc_ui_point world, const tc_ui_pointer_event& event) {
            forwarded = event.type == TC_UI_POINTER_DOWN && std::isfinite(world.x);
            return forwarded;
        });
        assert(document.dispatch_pointer_event({TC_UI_POINTER_DOWN, 120.0f, 80.0f, 0, 1, 0, 0.0f, 0.0f}) ==
               TC_UI_EVENT_HANDLED);
        assert(forwarded);
        view->set_pointer_handler({});
        document.dispatch_pointer_event({TC_UI_POINTER_UP, 120.0f, 80.0f, 0, 1, 0, 0.0f, 0.0f});

        assert(document.dispatch_pointer_event({TC_UI_POINTER_DOWN, 300.0f, 200.0f, 2, 1, 0, 0.0f, 0.0f}) ==
               TC_UI_EVENT_HANDLED);
        assert(document.dispatch_pointer_event({TC_UI_POINTER_MOVE, 320.0f, 230.0f, 2, 0, 0, 0.0f, 0.0f}) ==
               TC_UI_EVENT_HANDLED);
        assert(view->offset().x == 20.0f && view->offset().y == 30.0f);
        document.dispatch_pointer_event({TC_UI_POINTER_UP, 320.0f, 230.0f, 2, 1, 0, 0.0f, 0.0f});

        const tc_ui_point anchor{250.0f, 160.0f};
        const auto before = view->screen_to_world(anchor);
        document.dispatch_pointer_event({TC_UI_POINTER_WHEEL, anchor.x, anchor.y, 0, 0, 0, 0.0f, 1.0f});
        const auto after = view->screen_to_world(anchor);
        assert(view->zoom() > 1.0f);
        assert(std::fabs(before.x - after.x) < 0.001f);
        assert(std::fabs(before.y - after.y) < 0.001f);

        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);
        assert(tc_ui_document_destroy_widget(document.get(), view_handle));
        tc_ui_document_destroy(document_handle);
        tc_visual_scene_destroy(scene_handle);
    }

    void test_widget_portal_has_separate_document_lifetime() {
        const auto document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        const auto scene_handle = tc_visual_scene_create();
        TcVisualScene scene{scene_handle};
        auto* item =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 100.0f, 30.0f}, fill(0.2f, 0.2f, 0.25f));
        item->set_local_transform(termin::Affine2f::translation({15.0f, 12.0f}));
        auto* button = new Button("Embedded");
        const auto button_handle = document.adopt(button);
        auto* view = new SceneView(scene);
        const auto view_handle = document.adopt(view);
        assert(view->set_widget_portal(item->handle(), button_handle));
        assert(document.add_root(*view));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});

        assert(button->parent_widget() == view->c_widget());
        assert(button->bounds().x == 0.0f);
        assert(button->bounds().y == 0.0f);
        assert(tc_widget_subtree_transform(button->c_widget()).translation.x == 25.0f);
        assert(tc_widget_subtree_transform(button->c_widget()).translation.y == 32.0f);
        assert(tc_widget_handle_eq(document.hit_test(30.0f, 40.0f), button_handle));

        view->set_zoom(2.0f, {10.0f, 20.0f});
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(button->bounds().x == 0.0f);
        assert(button->bounds().width == 100.0f);
        assert(tc_widget_subtree_transform(button->c_widget()).scale == 2.0f);
        assert(tc_widget_handle_eq(document.hit_test(50.0f, 60.0f), button_handle));

        auto* draw_list = tc_ui_draw_list_create();
        auto* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        assert(commands_of_type(draw_list, TC_UI_DRAW_PUSH_UNIFORM_TRANSFORM).size() == 1);
        assert(commands_of_type(draw_list, TC_UI_DRAW_POP_TRANSFORM).size() == 1);
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        view->set_zoom(3.0f, {10.0f, 20.0f});
        assert(tc_widget_subtree_transform(button->c_widget()).scale == 3.0f);
        assert(tc_widget_handle_eq(document.hit_test(70.0f, 80.0f), button_handle));

        assert(tc_ui_document_set_pointer_capture(document.get(), button_handle));
        assert(view->clear_widget_portal(item->handle()));
        assert(button->parent_widget() == nullptr);
        assert(tc_widget_subtree_transform(button->c_widget()).scale == 1.0f);
        assert(tc_widget_handle_is_invalid(tc_ui_document_pointer_capture(document.get())));

        auto* replacement = new Button("Replacement");
        const auto replacement_handle = document.adopt(replacement);
        assert(view->set_widget_portal(item->handle(), button_handle));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(view->set_widget_portal(item->handle(), replacement_handle));
        assert(button->parent_widget() == nullptr);
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(replacement->parent_widget() == view->c_widget());
        view->clear_widget_portals();
        assert(replacement->parent_widget() == nullptr);
        assert(view->set_widget_portal(item->handle(), button_handle));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});

        auto* conflicting_item =
            adopt<RectItem2D>(scene, nullptr, termin::Rect2f{0.0f, 0.0f, 10.0f, 10.0f}, fill(0.3f, 0.2f, 0.2f));
        assert(!view->set_widget_portal(conflicting_item->handle(), button_handle));

        assert(tc_ui_document_set_pointer_capture(document.get(), button_handle));
        item->set_local_transform(termin::Affine2f::rotation(0.25f));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(button->parent_widget() == nullptr);
        assert(tc_widget_handle_is_invalid(tc_ui_document_pointer_capture(document.get())));
        item->set_local_transform(termin::Affine2f::translation({15.0f, 12.0f}));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(button->parent_widget() == view->c_widget());

        assert(scene.destroy(item->handle()));
        document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
        assert(button->parent_widget() == nullptr);
        assert(tc_widget_subtree_transform(button->c_widget()).scale == 1.0f);
        assert(tc_ui_document_is_alive(document.get(), button_handle));

        assert(tc_ui_document_destroy_widget(document.get(), view_handle));
        assert(tc_ui_document_is_alive(document.get(), button_handle));
        assert(tc_ui_document_destroy_widget(document.get(), button_handle));
        assert(tc_ui_document_destroy_widget(document.get(), replacement_handle));
        assert(scene.destroy(conflicting_item->handle()));
        tc_ui_document_destroy(document_handle);
        tc_visual_scene_destroy(scene_handle);
    }

} // namespace

int main() {
    test_scene_uses_generation_handles_and_canonical_hit_testing();
    test_view_transform_forwarding_pan_zoom_and_draw_list_bridge();
    test_widget_portal_has_separate_document_lifetime();
    return EXIT_SUCCESS;
}
