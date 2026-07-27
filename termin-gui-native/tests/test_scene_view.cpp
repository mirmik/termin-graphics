#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace termin::gui_native;

namespace {

std::vector<const tc_ui_draw_command*> commands_of_type(
    const tc_ui_draw_list* draw_list,
    tc_ui_draw_command_type type) {
    std::vector<const tc_ui_draw_command*> result;
    for (std::size_t index = 0;
         index < tc_ui_draw_list_command_count(draw_list);
         ++index) {
        const auto* command =
            tc_ui_draw_list_command_at(draw_list, index);
        if (command && command->type == type) result.push_back(command);
    }
    return result;
}

tgfx::FillPaint fill(float r, float g, float b, float a = 1.0f) {
    return {{r, g, b, a}};
}

void test_scene_uses_generation_handles_and_canonical_hit_testing() {
    auto scene = std::make_shared<GraphicsScene>();
    auto back = scene->create_rect(
        "back", {0.0f, 0.0f, 100.0f, 80.0f},
        fill(0.2f, 0.3f, 0.4f));
    auto child = scene->create_rect(
        "child", {0.0f, 0.0f, 20.0f, 10.0f},
        fill(0.7f, 0.2f, 0.2f), std::nullopt, back);
    auto front = scene->create_rect(
        "front", {0.0f, 0.0f, 100.0f, 80.0f},
        fill(0.3f, 0.6f, 0.4f));
    assert(back && child && front);
    assert(back.set_position({10.0f, 20.0f}));
    assert(child.set_position({5.0f, 7.0f}));
    assert(child.set_z_order(10));
    assert(front.set_position({20.0f, 30.0f}));
    assert(front.set_z_order(20));

    const auto front_hit = scene->hit_test(25.0f, 35.0f);
    assert(front_hit && *front_hit == front);
    assert(front.set_enabled(false));
    const auto child_hit = scene->hit_test(16.0f, 28.0f);
    assert(child_hit && *child_hit == child);
    assert(scene->destroy(back));
    assert(!back.valid());
    assert(!child.valid());
    assert(front.valid());

    auto replacement = scene->create_rect(
        "replacement", {0.0f, 0.0f, 20.0f, 20.0f},
        fill(0.8f, 0.8f, 0.2f));
    assert(replacement);
    assert(replacement.handle().generation != back.handle().generation ||
           replacement.handle().index != back.handle().index);
}

void test_view_transform_drag_pan_zoom_and_draw_list_bridge() {
    const auto document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    auto scene = std::make_shared<GraphicsScene>();
    auto item = scene->create_rect(
        "node", {0.0f, 0.0f, 80.0f, 40.0f},
        fill(0.8f, 0.2f, 0.1f));
    assert(item.set_position({10.0f, 20.0f}));
    assert(item.set_draggable(true));
    auto* view = new SceneView(scene);
    const auto view_handle = document.adopt(view);
    assert(document.add_root(*view));
    document.layout_roots({100.0f, 50.0f, 400.0f, 300.0f});
    assert(view->world_to_screen({10.0f, 20.0f}).x == 110.0f);

    auto* draw_list = tc_ui_draw_list_create();
    auto* context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(context);
    assert(commands_of_type(
               draw_list,
               TC_UI_DRAW_CANVAS2D_LIST).size() == 1);
    assert(!commands_of_type(draw_list, TC_UI_DRAW_LINE).empty());

    std::size_t moved = 0;
    view->item_moved().connect(
        [&](SceneView&, GraphicItemRef moved_item) {
            assert(moved_item == item);
            ++moved;
        });
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_DOWN, 120.0f, 80.0f, 0, 1, 0, 0.0f, 0.0f}) ==
           TC_UI_EVENT_HANDLED);
    assert(view->selected_items() == std::vector<GraphicItemRef>{item});
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_MOVE, 150.0f, 110.0f, 0, 0, 0, 0.0f, 0.0f}) ==
           TC_UI_EVENT_HANDLED);
    assert(item.position().x == 40.0f && item.position().y == 50.0f);
    assert(moved == 1);
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_UP, 150.0f, 110.0f, 0, 1, 0, 0.0f, 0.0f}) ==
           TC_UI_EVENT_HANDLED);

    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_DOWN, 300.0f, 200.0f, 2, 1, 0, 0.0f, 0.0f}) ==
           TC_UI_EVENT_HANDLED);
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_MOVE, 320.0f, 230.0f, 2, 0, 0, 0.0f, 0.0f}) ==
           TC_UI_EVENT_HANDLED);
    assert(view->offset().x == 20.0f && view->offset().y == 30.0f);
    document.dispatch_pointer_event(
        {TC_UI_POINTER_UP, 320.0f, 230.0f, 2, 1, 0, 0.0f, 0.0f});

    const tc_ui_point anchor{250.0f, 160.0f};
    const auto before = view->screen_to_world(anchor);
    document.dispatch_pointer_event(
        {TC_UI_POINTER_WHEEL, anchor.x, anchor.y, 0, 0, 0, 0.0f, 1.0f});
    const auto after = view->screen_to_world(anchor);
    assert(view->zoom() > 1.0f);
    assert(std::fabs(before.x - after.x) < 0.001f);
    assert(std::fabs(before.y - after.y) < 0.001f);

    bool forwarded = false;
    view->set_pointer_handler(
        [&](SceneView&, tc_ui_point world, const tc_ui_pointer_event& event) {
            forwarded =
                event.type == TC_UI_POINTER_DOWN &&
                std::isfinite(world.x);
            return forwarded;
        });
    document.dispatch_pointer_event(
        {TC_UI_POINTER_DOWN, 200.0f, 120.0f, 1, 1, 0, 0.0f, 0.0f});
    assert(forwarded);

    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
    assert(tc_ui_document_destroy_widget(document.get(), view_handle));
    tc_ui_document_destroy(document_handle);
}

void test_widget_portal_has_separate_document_lifetime() {
    const auto document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    auto scene = std::make_shared<GraphicsScene>();
    auto item = scene->create_rect(
        "editor", {0.0f, 0.0f, 100.0f, 30.0f},
        fill(0.2f, 0.2f, 0.25f));
    assert(item.set_position({15.0f, 12.0f}));
    auto* button = new Button("Embedded");
    const auto button_handle = document.adopt(button);
    auto* view = new SceneView(scene);
    const auto view_handle = document.adopt(view);
    assert(view->set_widget_portal(item, button_handle));
    assert(document.add_root(*view));
    document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});

    assert(button->parent_widget() == view->c_widget());
    assert(button->bounds().x == 25.0f);
    assert(button->bounds().y == 32.0f);
    assert(tc_widget_handle_eq(
        document.hit_test(30.0f, 40.0f),
        button_handle));

    assert(scene->destroy(item));
    document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
    assert(button->parent_widget() == nullptr);
    assert(tc_ui_document_is_alive(document.get(), button_handle));

    assert(tc_ui_document_destroy_widget(document.get(), view_handle));
    assert(tc_ui_document_is_alive(document.get(), button_handle));
    assert(tc_ui_document_destroy_widget(document.get(), button_handle));
    tc_ui_document_destroy(document_handle);
}

} // namespace

int main() {
    test_scene_uses_generation_handles_and_canonical_hit_testing();
    test_view_transform_drag_pan_zoom_and_draw_list_bridge();
    test_widget_portal_has_separate_document_lifetime();
    return EXIT_SUCCESS;
}
