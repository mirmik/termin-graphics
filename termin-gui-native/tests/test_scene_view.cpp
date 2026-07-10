#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace termin::gui_native;

namespace {

std::vector<const tc_ui_draw_command*> commands_of_type(const tc_ui_draw_list* draw_list,
                                                        tc_ui_draw_command_type type) {
    std::vector<const tc_ui_draw_command*> result;
    for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (command && command->type == type)
            result.push_back(command);
    }
    return result;
}

void test_scene_ownership_hit_testing_and_selection() {
    auto scene = std::make_shared<GraphicsScene>();
    auto back = std::make_shared<GraphicsItem>("back");
    auto front = std::make_shared<GraphicsItem>("front");
    auto child = std::make_shared<GraphicsItem>("child");
    back->set_position({10.0f, 20.0f});
    back->set_size({100.0f, 80.0f});
    front->set_position({20.0f, 30.0f});
    front->set_size({100.0f, 80.0f});
    front->set_z_index(5.0f);
    child->set_position({5.0f, 7.0f});
    child->set_size({20.0f, 10.0f});
    child->set_z_index(10.0f);
    assert(back->add_child(child));
    assert(!child->add_child(back));
    assert(scene->add_item(back));
    assert(scene->add_item(front));
    assert(!scene->add_item(child));

    assert(scene->hit_test(25.0f, 35.0f) == front);
    front->set_enabled(false);
    assert(scene->hit_test(16.0f, 28.0f) == child);
    child->set_hit_test_callback(
        [](const GraphicsItem&, float x, float y) { return x * x + y * y <= 25.0f; });
    assert(scene->hit_test(15.0f, 27.0f) == child);
    assert(scene->hit_test(29.0f, 35.0f) == back);

    size_t selection_notifications = 0;
    scene->selection_changed().connect(
        [&selection_notifications](GraphicsScene&, const auto&) { ++selection_notifications; });
    assert(scene->set_selected(back));
    assert(back->selected());
    assert(scene->toggle_selected(front));
    assert(scene->toggle_selected(child));
    assert(scene->selected_items().size() == 3);
    assert(scene->remove_item(back));
    assert(scene->selected_items().size() == 1);
    assert(!back->selected());
    assert(selection_notifications >= 3);

    std::weak_ptr<GraphicsItem> weak_child = child;
    child.reset();
    back.reset();
    assert(weak_child.expired());
}

void test_view_transform_drag_pan_zoom_paint_and_input_forwarding() {
    Document document;
    auto scene = std::make_shared<GraphicsScene>();
    auto item = std::make_shared<GraphicsItem>("node");
    item->set_position({10.0f, 20.0f});
    item->set_size({80.0f, 40.0f});
    item->set_draggable(true);
    item->set_paint_callback(
        [](GraphicsItem& self, tc_ui_paint_context* context, const SceneTransform& transform) {
            const tc_ui_rect world = self.world_bounds();
            const tc_ui_point screen = transform.world_to_screen({world.x, world.y});
            tc_ui_painter_fill_rect(
                context,
                {screen.x, screen.y, world.width * transform.zoom, world.height * transform.zoom},
                tc_ui_color{0.8f, 0.2f, 0.1f, 1.0f});
        });
    assert(scene->add_item(item));
    auto* view = new SceneView(scene);
    const tc_widget_handle view_handle = document.adopt(view);
    assert(document.add_root(*view));
    document.layout_roots({100.0f, 50.0f, 400.0f, 300.0f});
    assert(view->world_to_screen({10.0f, 20.0f}).x == 110.0f);

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(context);
    assert(commands_of_type(draw_list, TC_UI_DRAW_FILL_RECT).size() == 2);
    assert(!commands_of_type(draw_list, TC_UI_DRAW_LINE).empty());

    size_t moved = 0;
    view->item_moved().connect([&moved](SceneView&, std::shared_ptr<GraphicsItem>) { ++moved; });
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_DOWN, 120.0f, 80.0f, 0, 1, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert(scene->selected_items() == std::vector<std::shared_ptr<GraphicsItem>>{item});
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_MOVE, 150.0f, 110.0f, 0, 0, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert(item->position().x == 40.0f && item->position().y == 50.0f);
    assert(moved == 1);
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_UP, 150.0f, 110.0f, 0, 1, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);

    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_DOWN, 300.0f, 200.0f, 2, 1, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert(document.dispatch_pointer_event(
               {TC_UI_POINTER_MOVE, 320.0f, 230.0f, 2, 0, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert(view->offset().x == 20.0f && view->offset().y == 30.0f);
    document.dispatch_pointer_event({TC_UI_POINTER_UP, 320.0f, 230.0f, 2, 1, 0, 0.0f, 0.0f});

    const tc_ui_point anchor{250.0f, 160.0f};
    const tc_ui_point before = view->screen_to_world(anchor);
    document.dispatch_pointer_event({TC_UI_POINTER_WHEEL, anchor.x, anchor.y, 0, 0, 0, 0.0f, 1.0f});
    const tc_ui_point after = view->screen_to_world(anchor);
    assert(view->zoom() > 1.0f);
    assert(std::fabs(before.x - after.x) < 0.001f);
    assert(std::fabs(before.y - after.y) < 0.001f);

    bool forwarded = false;
    view->set_pointer_handler(
        [&forwarded](SceneView&, tc_ui_point world, const tc_ui_pointer_event& event) {
            forwarded = event.type == TC_UI_POINTER_DOWN && std::isfinite(world.x);
            return forwarded;
        });
    document.dispatch_pointer_event({TC_UI_POINTER_DOWN, 200.0f, 120.0f, 1, 1, 0, 0.0f, 0.0f});
    assert(forwarded);

    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
    assert(tc_ui_document_destroy_widget(document.get(), view_handle));
}

void test_embedded_widget_uses_canonical_document_tree_and_detaches() {
    Document document;
    auto scene = std::make_shared<GraphicsScene>();
    auto item = std::make_shared<GraphicsItem>("editor");
    item->set_position({15.0f, 12.0f});
    item->set_size({100.0f, 30.0f});
    auto* button = new Button("Embedded");
    const tc_widget_handle button_handle = document.adopt(button);
    item->set_embedded_widget(button_handle);
    assert(scene->add_item(item));
    auto* view = new SceneView(scene);
    const tc_widget_handle view_handle = document.adopt(view);
    assert(document.add_root(*view));
    document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});

    assert(button->parent_widget() == view->c_widget());
    assert(button->bounds().x == 25.0f);
    assert(button->bounds().y == 32.0f);
    assert(tc_widget_handle_eq(document.hit_test(30.0f, 40.0f), button_handle));

    assert(scene->remove_item(item));
    document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
    assert(button->parent_widget() == nullptr);
    assert(tc_ui_document_is_alive(document.get(), button_handle));

    assert(scene->add_item(item));
    document.layout_roots({10.0f, 20.0f, 300.0f, 200.0f});
    assert(button->parent_widget() == view->c_widget());
    assert(tc_ui_document_destroy_widget(document.get(), view_handle));
    assert(button->parent_widget() == nullptr);
    assert(tc_ui_document_is_alive(document.get(), button_handle));
    assert(tc_ui_document_destroy_widget(document.get(), button_handle));
}

} // namespace

int main() {
    test_scene_ownership_hit_testing_and_selection();
    test_view_transform_drag_pan_zoom_paint_and_input_forwarding();
    test_embedded_widget_uses_canonical_document_tree_and_detaches();
    return EXIT_SUCCESS;
}
