#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>

using namespace termin::gui_native;

namespace {

bool near(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

void test_box_layout_sets_child_bounds_and_paints() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
    root.set_padding(EdgeInsets {4.0f, 6.0f, 4.0f, 6.0f})
        .set_spacing(2.0f)
        .set_background(Color {0.1f, 0.1f, 0.1f, 1.0f});

    auto& first = ui.make<Panel>("first");
    auto& second = ui.make<Panel>("second");
    root.add_child(first);
    root.add_child(second);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 108.0f, 112.0f});

    assert(first.bounds().x == 4.0f);
    assert(first.bounds().y == 6.0f);
    assert(first.bounds().width == 100.0f);
    assert(first.bounds().height == 49.0f);
    assert(second.bounds().x == 4.0f);
    assert(second.bounds().y == 57.0f);
    assert(second.bounds().height == 49.0f);

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);

    assert(tc_ui_draw_list_command_count(draw_list) >= 7);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_box_layout_child_policies_allocate_primary_axis() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& fixed = ui.make<Spacer>(tc_ui_size {10.0f, 12.0f});
    auto& preferred = ui.make<Spacer>(tc_ui_size {30.0f, 18.0f});
    auto& flex_one = ui.make<Panel>("flex-one");
    auto& flex_two = ui.make<Panel>("flex-two");

    root.add_fixed_child(fixed, 50.0f);
    root.add_preferred_child(preferred);
    root.add_flex_child(flex_one, 1.0f);
    root.add_flex_child(flex_two, 2.0f);

    assert(root.items().size() == 4);
    assert(root.items()[0].policy == LayoutPolicy::Fixed);
    assert(root.items()[1].policy == LayoutPolicy::Preferred);
    assert(root.items()[2].policy == LayoutPolicy::Flex);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 300.0f, 40.0f});

    assert(near(fixed.bounds().x, 0.0f));
    assert(near(fixed.bounds().width, 50.0f));
    assert(near(preferred.bounds().x, 50.0f));
    assert(near(preferred.bounds().width, 30.0f));
    assert(near(flex_one.bounds().x, 80.0f));
    assert(near(flex_one.bounds().width, 220.0f / 3.0f));
    assert(near(flex_two.bounds().x, 80.0f + 220.0f / 3.0f));
    assert(near(flex_two.bounds().width, 440.0f / 3.0f));
}

void test_recursive_destroy_removes_container_children() {
    Document document;
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& child = ui.make<Panel>("child");
    root.add_child(child);

    assert(tc_ui_document_live_widget_count(document.get()) == 2);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
    assert(!tc_ui_document_is_alive(document.get(), root.handle()));
    assert(!tc_ui_document_is_alive(document.get(), child.handle()));
}

void test_controls_handle_pointer_events() {
    Document document;
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& checkbox = ui.make<Checkbox>(false);
    auto& slider = ui.make<Slider>(0.0f);
    root.add_child(checkbox);
    root.add_child(slider);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 40.0f});

    tc_ui_pointer_event checkbox_event {};
    checkbox_event.type = TC_UI_POINTER_DOWN;
    checkbox_event.x = checkbox.bounds().x + 4.0f;
    checkbox_event.y = checkbox.bounds().y + 4.0f;
    assert(document.dispatch_pointer_event(checkbox_event) == TC_UI_EVENT_HANDLED);
    assert(checkbox.checked());

    tc_ui_pointer_event slider_event {};
    slider_event.type = TC_UI_POINTER_DOWN;
    slider_event.x = slider.bounds().x + slider.bounds().width;
    slider_event.y = slider.bounds().y + slider.bounds().height * 0.5f;
    assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
    assert(slider.value() > 0.95f);
}

} // namespace

int main() {
    test_box_layout_sets_child_bounds_and_paints();
    test_box_layout_child_policies_allocate_primary_axis();
    test_recursive_destroy_removes_container_children();
    test_controls_handle_pointer_events();
    return EXIT_SUCCESS;
}
