#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>

using namespace termin::gui_native;

namespace {

bool near(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

size_t count_commands(const tc_ui_draw_list* draw_list, tc_ui_draw_command_type type) {
    size_t count = 0;
    for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
        if (command && command->type == type) {
            count += 1;
        }
    }
    return count;
}

bool test_text_measure(
    void*,
    const char* text,
    size_t byte_length,
    float font_size,
    tc_ui_text_metrics* out_metrics
) {
    if (!text || !out_metrics || font_size <= 0.0f) {
        return false;
    }
    float width = 0.0f;
    size_t offset = 0;
    while (offset < byte_length) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        if (first < 0x80u) {
            if (first == static_cast<uint8_t>('i')) {
                width += font_size * 0.25f;
            } else if (first == static_cast<uint8_t>('W')) {
                width += font_size * 0.90f;
            } else {
                width += font_size * 0.50f;
            }
            offset += 1;
        } else if ((first & 0xe0u) == 0xc0u && offset + 2 <= byte_length) {
            width += font_size * 0.60f;
            offset += 2;
        } else if ((first & 0xf0u) == 0xe0u && offset + 3 <= byte_length) {
            width += font_size * 0.70f;
            offset += 3;
        } else if ((first & 0xf8u) == 0xf0u && offset + 4 <= byte_length) {
            width += font_size;
            offset += 4;
        } else {
            return false;
        }
    }
    out_metrics->width = width;
    out_metrics->height = font_size;
    out_metrics->ascent = font_size * 0.8f;
    out_metrics->descent = font_size * 0.2f;
    out_metrics->line_height = font_size * 1.2f;
    return true;
}

void install_test_text_measurer(Document& document) {
    document.set_text_measurer(&test_text_measure, nullptr);
}

class CapturingProbe final : public NativeWidget {
public:
    CapturingProbe() : NativeWidget("CapturingProbe") {
        set_preferred_size(tc_ui_size {80.0f, 32.0f});
    }

    int down_count = 0;
    int move_count = 0;
    int up_count = 0;

    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override {
        if (!event) {
            return TC_UI_EVENT_IGNORED;
        }
        const bool inside = event->x >= bounds().x && event->y >= bounds().y &&
            event->x <= bounds().x + bounds().width &&
            event->y <= bounds().y + bounds().height;
        const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
        if (event->type == TC_UI_POINTER_DOWN && inside) {
            down_count += 1;
            tc_ui_document_set_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_MOVE && captured) {
            move_count += 1;
            return TC_UI_EVENT_HANDLED;
        }
        if (event->type == TC_UI_POINTER_UP && captured) {
            up_count += 1;
            tc_ui_document_release_pointer_capture(document, handle());
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }
};

class FocusProbe final : public NativeWidget {
public:
    FocusProbe() : NativeWidget("FocusProbe") {
        set_focusable(true);
        set_preferred_size(tc_ui_size {80.0f, 32.0f});
    }

    int key_count = 0;
    int text_count = 0;
    int last_key = 0;

    tc_ui_event_result key_event(tc_ui_document*, const tc_ui_key_event* event) override {
        if (!event) {
            return TC_UI_EVENT_IGNORED;
        }
        key_count += 1;
        last_key = event->key;
        return TC_UI_EVENT_HANDLED;
    }

    tc_ui_event_result text_event(tc_ui_document*, const tc_ui_text_event* event) override {
        if (!event || !event->text) {
            return TC_UI_EVENT_IGNORED;
        }
        text_count += 1;
        return TC_UI_EVENT_HANDLED;
    }
};

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

void test_widget_metadata_is_owned_and_exposed() {
    Document document;
    DocumentBuilder ui(document);

    std::string debug_name = "initial-debug";
    auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, debug_name.c_str());
    debug_name = "mutated-debug";

    assert(root.debug_name());
    assert(std::strcmp(root.debug_name(), "initial-debug") == 0);
    assert(std::strcmp(tc_widget_debug_name(root.c_widget()), "initial-debug") == 0);

    root.set_stable_id("showcase.root");
    root.set_name("Root");
    root.set_debug_name("renamed-root");
    assert(std::strcmp(root.stable_id(), "showcase.root") == 0);
    assert(std::strcmp(root.name(), "Root") == 0);
    assert(std::strcmp(root.debug_name(), "renamed-root") == 0);
    assert(std::strcmp(tc_widget_stable_id(root.c_widget()), "showcase.root") == 0);
    assert(std::strcmp(tc_widget_name(root.c_widget()), "Root") == 0);

    root.set_name({});
    assert(root.name() == nullptr);
    assert(tc_widget_name(root.c_widget()) == nullptr);
}

void test_dirty_flags_track_layout_paint_and_state_changes() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    root.clear_dirty(TC_WIDGET_DIRTY_MASK);
    root.set_spacing(4.0f);
    assert(root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT));
    assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_STATE));

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 40.0f});
    assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT));
    assert(root.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));

    root.clear_dirty(TC_WIDGET_DIRTY_MASK);
    root.set_background(Color {0.1f, 0.2f, 0.3f, 1.0f});
    assert(root.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));
    assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT));

    auto& button = ui.make<Button>("Run");
    button.clear_dirty(TC_WIDGET_DIRTY_MASK);
    button.set_text("Stop");
    assert(button.has_dirty_flags(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT));

    auto& slider = ui.make<Slider>(0.0f);
    slider.clear_dirty(TC_WIDGET_DIRTY_MASK);
    slider.set_focusable(true);
    assert(slider.dirty_flags() == 0);
    slider.set_value(0.5f);
    assert(slider.has_dirty_flags(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT));
    slider.clear_dirty(TC_WIDGET_DIRTY_STATE);
    assert(!slider.has_dirty_flags(TC_WIDGET_DIRTY_STATE));
    assert(slider.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));
}

void test_box_layout_child_policies_allocate_primary_axis() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& fixed = ui.make<Spacer>(tc_ui_size {10.0f, 12.0f});
    auto& preferred = ui.make<Spacer>(tc_ui_size {30.0f, 18.0f});
    auto& flex_one = ui.make<Spacer>(tc_ui_size {20.0f, 12.0f});
    auto& flex_two = ui.make<Spacer>(tc_ui_size {20.0f, 12.0f});

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
    assert(near(flex_one.bounds().width, 80.0f));
    assert(near(flex_two.bounds().x, 160.0f));
    assert(near(flex_two.bounds().width, 140.0f));
}

void test_hstack_vstack_wrappers_use_expected_orientation() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<VStack>("root-vstack");
    auto& row = ui.make<HStack>("row-hstack");
    auto& left = ui.make<Spacer>(tc_ui_size {20.0f, 10.0f});
    auto& right = ui.make<Spacer>(tc_ui_size {20.0f, 10.0f});
    auto& bottom = ui.make<Spacer>(tc_ui_size {30.0f, 8.0f});

    root.add_preferred_child(row);
    root.add_preferred_child(bottom);
    row.add_preferred_child(left);
    row.add_preferred_child(right);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 100.0f});

    assert(near(row.bounds().y, 0.0f));
    assert(near(bottom.bounds().y, row.bounds().height));
    assert(near(left.bounds().x, 0.0f));
    assert(near(right.bounds().x, left.bounds().width));
}

void test_grid_layout_tracks_spans_and_hit_test() {
    Document document;
    DocumentBuilder ui(document);

    auto& grid = ui.make_root<GridLayout>("grid");
    grid.set_padding(EdgeInsets {2.0f, 3.0f, 4.0f, 5.0f})
        .set_spacing(10.0f, 6.0f);
    grid.add_column(LayoutPolicy::Fixed, 40.0f);
    grid.add_column(LayoutPolicy::Stretch);
    grid.add_column(LayoutPolicy::Flex, 2.0f);
    grid.add_row(LayoutPolicy::Preferred);
    grid.add_row(LayoutPolicy::Stretch);

    auto& fixed_cell = ui.make<Spacer>(tc_ui_size {30.0f, 20.0f});
    auto& spanning = ui.make<Spacer>(tc_ui_size {90.0f, 12.0f});
    auto& bottom = ui.make<Panel>("bottom");
    bottom.set_preferred_size(tc_ui_size {20.0f, 30.0f});
    grid.add_child(fixed_cell, 0, 0);
    grid.add_child(spanning, 0, 1, 1, 2);
    grid.add_child(bottom, 1, 2);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 100.0f});

    assert(near(fixed_cell.bounds().x, 2.0f));
    assert(near(fixed_cell.bounds().y, 3.0f));
    assert(near(fixed_cell.bounds().width, 40.0f));
    assert(near(fixed_cell.bounds().height, 20.0f));

    assert(near(spanning.bounds().x, 52.0f));
    assert(near(spanning.bounds().width, 144.0f));
    assert(near(spanning.bounds().height, 20.0f));

    assert(near(bottom.bounds().x, 120.0f));
    assert(near(bottom.bounds().y, 29.0f));
    assert(near(bottom.bounds().width, 76.0f));
    assert(near(bottom.bounds().height, 66.0f));
    assert(tc_widget_handle_eq(document.hit_test(125.0f, 35.0f), bottom.handle()));
}

void test_grid_layout_recursive_destroy_children() {
    Document document;
    DocumentBuilder ui(document);

    auto& grid = ui.make_root<GridLayout>("grid");
    auto& first = ui.make<Panel>("first");
    auto& second = ui.make<Panel>("second");
    grid.add_child(first, 0, 0);
    grid.add_child(second, 1, 1);

    assert(tc_ui_document_live_widget_count(document.get()) == 3);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), grid.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_group_box_lays_out_content_and_routes_hit_test() {
    Document document;
    DocumentBuilder ui(document);

    auto& group = ui.make_root<GroupBox>("Settings");
    group.set_padding(EdgeInsets {8.0f, 6.0f, 10.0f, 12.0f});
    auto& content = ui.make<Panel>("content");
    group.set_content(content);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 180.0f, 120.0f});
    assert(near(content.bounds().x, 8.0f));
    assert(near(content.bounds().y, 36.0f));
    assert(near(content.bounds().width, 162.0f));
    assert(near(content.bounds().height, 72.0f));
    assert(tc_widget_handle_eq(document.hit_test(20.0f, 45.0f), content.handle()));
    assert(tc_widget_handle_eq(document.hit_test(20.0f, 12.0f), group.handle()));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);
    assert(count_commands(draw_list, TC_UI_DRAW_TEXT) == 1);
    assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == 2);
    assert(count_commands(draw_list, TC_UI_DRAW_POP_CLIP) == 2);
    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_group_box_recursive_destroy_content() {
    Document document;
    DocumentBuilder ui(document);

    auto& group = ui.make_root<GroupBox>("Settings");
    auto& content = ui.make<Panel>("content");
    group.set_content(content);

    assert(tc_ui_document_live_widget_count(document.get()) == 2);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), group.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_splitter_layout_drag_and_hit_test() {
    Document document;
    DocumentBuilder ui(document);

    auto& splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
    splitter.set_split_fraction(0.25f).set_min_extents(20.0f, 20.0f).set_divider_thickness(8.0f);
    auto& left = ui.make<Panel>("left");
    auto& right = ui.make<Panel>("right");
    splitter.set_first(left);
    splitter.set_second(right);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 208.0f, 80.0f});
    assert(near(left.bounds().width, 50.0f));
    assert(near(right.bounds().x, 58.0f));
    assert(near(right.bounds().width, 150.0f));
    assert(tc_widget_handle_eq(document.hit_test(54.0f, 10.0f), splitter.handle()));
    assert(tc_widget_handle_eq(document.hit_test(100.0f, 10.0f), right.handle()));

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 54.0f;
    event.y = 10.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.pointer_capture(), splitter.handle()));

    event.type = TC_UI_POINTER_MOVE;
    event.x = 140.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(splitter.split_fraction() > 0.65f);
    assert(left.bounds().width > 130.0f);

    event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));
}

void test_splitter_recursive_destroy_children() {
    Document document;
    DocumentBuilder ui(document);

    auto& splitter = ui.make_root<Splitter>(Orientation::Vertical, "splitter");
    auto& first = ui.make<Panel>("first");
    auto& second = ui.make<Panel>("second");
    splitter.set_first(first);
    splitter.set_second(second);

    assert(tc_ui_document_live_widget_count(document.get()) == 3);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), splitter.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_scroll_area_lays_out_content_with_clip_and_scroll() {
    Document document;
    DocumentBuilder ui(document);

    auto& scroll = ui.make_root<ScrollArea>("scroll");
    auto& content = ui.make<VStack>("scroll-content");
    auto& top = ui.make<Panel>("top");
    auto& bottom = ui.make<Panel>("bottom");
    content.add_fixed_child(top, 80.0f);
    content.add_fixed_child(bottom, 80.0f);
    scroll.set_content(content);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 60.0f});
    assert(near(content.bounds().x, 0.0f));
    assert(near(content.bounds().y, 0.0f));
    assert(near(scroll.content_size().height, 160.0f));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), top.handle()));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 70.0f), tc_widget_handle_invalid()));

    scroll.set_scroll(0.0f, 40.0f);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 60.0f});
    assert(near(scroll.scroll_y(), 40.0f));
    assert(near(content.bounds().y, -40.0f));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 50.0f), bottom.handle()));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);

    assert(tc_ui_draw_list_command_count(draw_list) >= 4);
    const tc_ui_draw_command* first = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* last = tc_ui_draw_list_command_at(draw_list, tc_ui_draw_list_command_count(draw_list) - 1);
    assert(first && first->type == TC_UI_DRAW_PUSH_CLIP);
    assert(near(first->rect.width, 100.0f));
    assert(last && last->type == TC_UI_DRAW_POP_CLIP);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_scroll_area_wheel_clamps_and_recursive_destroy_content() {
    Document document;
    DocumentBuilder ui(document);

    auto& scroll = ui.make_root<ScrollArea>("scroll");
    auto& content = ui.make<VStack>("scroll-content");
    auto& child = ui.make<Panel>("child");
    content.add_fixed_child(child, 180.0f);
    scroll.set_content(content);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 50.0f});

    tc_ui_pointer_event wheel {};
    wheel.type = TC_UI_POINTER_WHEEL;
    wheel.x = 10.0f;
    wheel.y = 10.0f;
    wheel.wheel_y = -10.0f;
    assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
    assert(near(scroll.scroll_y(), 130.0f));

    wheel.wheel_y = 10.0f;
    assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
    assert(near(scroll.scroll_y(), 0.0f));

    assert(tc_ui_document_live_widget_count(document.get()) == 3);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), scroll.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_tab_view_switches_selected_page_and_clips_paint() {
    Document document;
    DocumentBuilder ui(document);

    auto& tabs = ui.make_root<TabView>("tabs");
    auto& first = ui.make<Panel>("first-page");
    auto& second = ui.make<Panel>("second-page");
    tabs.add_page("First", first);
    tabs.add_page("Second", second);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 100.0f});
    assert(tabs.page_count() == 2);
    assert(tabs.selected_index() == 0);
    assert(near(first.bounds().y, 32.0f));
    assert(near(first.bounds().height, 68.0f));
    assert(near(second.bounds().width, 0.0f));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), tabs.handle()));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 40.0f), first.handle()));

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 120.0f;
    event.y = 10.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(tabs.selected_index() == 1);
    assert(near(second.bounds().y, 32.0f));
    assert(tc_widget_handle_eq(document.hit_test(10.0f, 40.0f), second.handle()));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);
    bool saw_body_clip = false;
    for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
        if (command && command->type == TC_UI_DRAW_PUSH_CLIP && near(command->rect.y, 32.0f)) {
            saw_body_clip = true;
        }
    }
    assert(saw_body_clip);
    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_tab_view_recursive_destroy_pages() {
    Document document;
    DocumentBuilder ui(document);

    auto& tabs = ui.make_root<TabView>("tabs");
    auto& first = ui.make<Panel>("first-page");
    auto& second = ui.make<Panel>("second-page");
    tabs.add_page("First", first);
    tabs.add_page("Second", second);

    assert(tc_ui_document_live_widget_count(document.get()) == 3);
    assert(tc_ui_document_destroy_widget_recursive(document.get(), tabs.handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_box_layout_shrinks_flexible_children_before_overflowing() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& fixed = ui.make<Spacer>(tc_ui_size {10.0f, 12.0f});
    auto& preferred = ui.make<Spacer>(tc_ui_size {80.0f, 12.0f});
    auto& stretch_one = ui.make<Spacer>(tc_ui_size {100.0f, 12.0f});
    auto& stretch_two = ui.make<Spacer>(tc_ui_size {100.0f, 12.0f});

    root.add_fixed_child(fixed, 50.0f);
    root.add_preferred_child(preferred);
    root.add_stretch_child(stretch_one);
    root.add_stretch_child(stretch_two);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 180.0f, 24.0f});

    assert(near(fixed.bounds().width, 50.0f));
    assert(near(preferred.bounds().width, 80.0f));
    assert(near(stretch_one.bounds().x, 130.0f));
    assert(near(stretch_one.bounds().width, 25.0f));
    assert(near(stretch_two.bounds().x, 155.0f));
    assert(near(stretch_two.bounds().width, 25.0f));
}

void test_box_layout_respects_child_extent_limits() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& capped = ui.make<Spacer>(tc_ui_size {50.0f, 12.0f});
    auto& uncapped = ui.make<Spacer>(tc_ui_size {50.0f, 12.0f});

    root.add_stretch_child(capped);
    root.add_stretch_child(uncapped);
    assert(root.set_child_extent_limits(capped, 0.0f, 80.0f));
    assert(root.items()[0].max_extent == 80.0f);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 300.0f, 24.0f});

    assert(near(capped.bounds().width, 80.0f));
    assert(near(uncapped.bounds().x, 80.0f));
    assert(near(uncapped.bounds().width, 220.0f));
}

void test_box_layout_allows_preferred_overflow_when_no_child_can_shrink() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& first = ui.make<Spacer>(tc_ui_size {80.0f, 12.0f});
    auto& second = ui.make<Spacer>(tc_ui_size {60.0f, 12.0f});

    root.add_preferred_child(first);
    root.add_preferred_child(second);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 24.0f});

    assert(near(first.bounds().width, 80.0f));
    assert(near(second.bounds().x, 80.0f));
    assert(near(second.bounds().width, 60.0f));
}

void test_document_hit_test_returns_deepest_child() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& first = ui.make<Panel>("first");
    auto& second = ui.make<Panel>("second");
    root.add_stretch_child(first);
    root.add_stretch_child(second);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 40.0f});

    assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), first.handle()));
    assert(tc_widget_handle_eq(document.hit_test(150.0f, 10.0f), second.handle()));
    assert(tc_widget_handle_is_invalid(document.hit_test(250.0f, 10.0f)));
}

void test_document_hit_test_prefers_topmost_root() {
    Document document;
    DocumentBuilder ui(document);

    auto& bottom = ui.make_root<Panel>("bottom-root");
    auto& top = ui.make<Panel>("top-root");
    assert(document.add_root(top));

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 100.0f});

    assert(tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), top.handle()));
    assert(!tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), bottom.handle()));
}

void test_box_layout_hit_test_skips_stale_child_handles() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& child = ui.make<Panel>("child");
    root.add_stretch_child(child);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 40.0f});
    tc_widget_handle child_handle = child.handle();
    assert(tc_ui_document_destroy_widget(document.get(), child_handle));

    assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), root.handle()));
}

void test_pointer_dispatch_updates_hovered_widget() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& first = ui.make<Panel>("first");
    auto& second = ui.make<Panel>("second");
    root.add_stretch_child(first);
    root.add_stretch_child(second);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 40.0f});

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_MOVE;
    event.x = 20.0f;
    event.y = 10.0f;
    document.dispatch_pointer_event(event);
    assert(tc_widget_handle_eq(document.hovered_widget(), first.handle()));

    event.x = 150.0f;
    document.dispatch_pointer_event(event);
    assert(tc_widget_handle_eq(document.hovered_widget(), second.handle()));

    event.x = 240.0f;
    document.dispatch_pointer_event(event);
    assert(tc_widget_handle_is_invalid(document.hovered_widget()));
}

void test_pointer_capture_routes_events_outside_bounds_until_release() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& probe = ui.make<CapturingProbe>();
    auto& panel = ui.make<Panel>("panel");
    root.add_preferred_child(probe);
    root.add_stretch_child(panel);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 160.0f, 40.0f});

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 10.0f;
    event.y = 10.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(probe.down_count == 1);
    assert(tc_widget_handle_eq(document.pointer_capture(), probe.handle()));

    event.type = TC_UI_POINTER_MOVE;
    event.x = 300.0f;
    event.y = 10.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(probe.move_count == 1);
    assert(tc_widget_handle_is_invalid(document.hovered_widget()));

    event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(probe.up_count == 1);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));
}

void test_destroy_clears_hover_and_pointer_capture() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& probe = ui.make<CapturingProbe>();
    root.add_stretch_child(probe);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 100.0f, 40.0f});

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 10.0f;
    event.y = 10.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.hovered_widget(), probe.handle()));
    assert(tc_widget_handle_eq(document.pointer_capture(), probe.handle()));

    assert(tc_ui_document_destroy_widget(document.get(), probe.handle()));
    assert(tc_widget_handle_is_invalid(document.hovered_widget()));
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));
}

void test_focus_and_key_text_dispatch_follow_focused_widget() {
    Document document;
    DocumentBuilder ui(document);

    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& focusable = ui.make<FocusProbe>();
    auto& panel = ui.make<Panel>("panel");
    root.add_preferred_child(focusable);
    root.add_stretch_child(panel);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 160.0f, 40.0f});
    assert(focusable.focusable());
    assert(!panel.focusable());

    tc_ui_pointer_event pointer {};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = 10.0f;
    pointer.y = 10.0f;
    document.dispatch_pointer_event(pointer);
    assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));

    tc_ui_key_event key {};
    key.type = TC_UI_KEY_DOWN;
    key.key = 65;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(focusable.key_count == 1);
    assert(focusable.last_key == 65);

    tc_ui_text_event text {};
    text.text = "a";
    assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
    assert(focusable.text_count == 1);

    pointer.x = panel.bounds().x + 4.0f;
    document.dispatch_pointer_event(pointer);
    assert(tc_widget_handle_is_invalid(document.focused_widget()));
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
    assert(focusable.key_count == 1);
}

void test_focus_api_rejects_non_focusable_and_clears_on_destroy() {
    Document document;
    DocumentBuilder ui(document);

    auto& focusable = ui.make_root<FocusProbe>();
    auto& panel = ui.make<Panel>("panel");
    assert(!document.set_focus(panel));
    assert(tc_widget_handle_is_invalid(document.focused_widget()));
    assert(document.set_focus(focusable));
    assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));
    assert(!document.clear_focus(panel));
    assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));
    assert(document.clear_focus(focusable));
    assert(tc_widget_handle_is_invalid(document.focused_widget()));

    assert(document.set_focus(focusable));
    assert(tc_ui_document_destroy_widget(document.get(), focusable.handle()));
    assert(tc_widget_handle_is_invalid(document.focused_widget()));
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
    assert(!checkbox.checked());
    assert(tc_widget_handle_eq(document.pointer_capture(), checkbox.handle()));
    checkbox_event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(checkbox_event) == TC_UI_EVENT_HANDLED);
    assert(checkbox.checked());
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));

    tc_ui_pointer_event slider_event {};
    slider_event.type = TC_UI_POINTER_DOWN;
    slider_event.x = slider.bounds().x + 10.0f;
    slider_event.y = slider.bounds().y + slider.bounds().height * 0.5f;
    assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.pointer_capture(), slider.handle()));
    slider_event.type = TC_UI_POINTER_MOVE;
    slider_event.x = slider.bounds().x + slider.bounds().width + 80.0f;
    assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
    assert(slider.value() > 0.95f);
    slider_event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(slider_event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));
}

void test_separator_layout_and_paint_command() {
    Document document;
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
    root.set_padding(EdgeInsets {});
    auto& separator = ui.make<Separator>(Orientation::Horizontal);
    separator.set_thickness(3.0f);
    root.add_preferred_child(separator);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 120.0f, 20.0f});
    assert(near(separator.bounds().width, 120.0f));
    assert(near(separator.bounds().height, 3.0f));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);
    assert(tc_ui_draw_list_command_count(draw_list) >= 3);
    bool saw_fill = false;
    for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
        if (command && command->type == TC_UI_DRAW_FILL_RECT && near(command->rect.height, 3.0f)) {
            saw_fill = true;
        }
    }
    assert(saw_fill);
    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_text_input_focus_text_edit_and_submit() {
    Document document;
    install_test_text_measurer(document);
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& input = ui.make<TextInput>("ab");
    root.add_preferred_child(input);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 220.0f, 40.0f});

    int changed = 0;
    int submitted = 0;
    std::string last_text;
    input.changed().connect([&changed, &last_text](TextInput&, const std::string& text) {
        changed += 1;
        last_text = text;
    });
    input.submitted().connect([&submitted](TextInput&, const std::string&) {
        submitted += 1;
    });

    tc_ui_pointer_event pointer {};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = input.bounds().x + 40.0f;
    pointer.y = input.bounds().y + 10.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.focused_widget(), input.handle()));

    tc_ui_text_event text {};
    text.text = "c";
    assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "abc");
    assert(input.caret() == 3);
    assert(changed == 1);
    assert(last_text == "abc");

    tc_ui_key_event key {};
    key.type = TC_UI_KEY_DOWN;
    key.key = TC_UI_KEY_LEFT;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.caret() == 2);

    key.key = TC_UI_KEY_BACKSPACE;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "ac");
    assert(input.caret() == 1);
    assert(changed == 2);

    key.key = TC_UI_KEY_DELETE;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "a");
    assert(input.caret() == 1);
    assert(changed == 3);

    key.key = TC_UI_KEY_ENTER;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(submitted == 1);
}

void test_text_widgets_clip_text_paint() {
    Document document;
    install_test_text_measurer(document);
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
    auto& label = ui.make<Label>("Long label text that must stay inside its widget", 14.0f);
    auto& input = ui.make<TextInput>("Long input text that must stay inside the edit box");
    root.add_fixed_child(label, 20.0f);
    root.add_fixed_child(input, 34.0f);

    document.layout_roots(tc_ui_rect {10.0f, 20.0f, 180.0f, 80.0f});

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);

    bool saw_label_clip = false;
    bool saw_input_inner_clip = false;
    for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, i);
        if (!command || command->type != TC_UI_DRAW_PUSH_CLIP) {
            continue;
        }
        if (near(command->rect.x, label.bounds().x) &&
            near(command->rect.y, label.bounds().y) &&
            near(command->rect.width, label.bounds().width) &&
            near(command->rect.height, label.bounds().height)) {
            saw_label_clip = true;
        }
        if (near(command->rect.x, input.bounds().x + 8.0f) &&
            near(command->rect.y, input.bounds().y + 2.0f) &&
            near(command->rect.width, input.bounds().width - 16.0f) &&
            near(command->rect.height, input.bounds().height - 4.0f)) {
            saw_input_inner_clip = true;
        }
    }

    assert(saw_label_clip);
    assert(saw_input_inner_clip);
    assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == count_commands(draw_list, TC_UI_DRAW_POP_CLIP));

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_text_measurement_uses_proportional_metrics() {
    Document document;
    install_test_text_measurer(document);
    DocumentBuilder ui(document);
    auto& narrow = ui.make<Label>("iii", 20.0f);
    auto& wide = ui.make<Label>("WWW", 20.0f);
    const tc_ui_constraints constraints {
        tc_ui_size {0.0f, 0.0f},
        tc_ui_size {1000.0f, 1000.0f}
    };

    const tc_ui_size narrow_size = narrow.measure(document.get(), constraints);
    const tc_ui_size wide_size = wide.measure(document.get(), constraints);
    assert(near(narrow_size.width, 15.0f));
    assert(near(wide_size.width, 54.0f));
    assert(wide_size.width > narrow_size.width * 3.0f);
    assert(near(narrow_size.height, 24.0f));
}

void test_text_input_edits_utf8_at_codepoint_boundaries() {
    Document document;
    install_test_text_measurer(document);
    DocumentBuilder ui(document);
    const std::string initial = "a\xc3\xa9\xf0\x9f\x99\x82" "b";
    auto& input = ui.make_root<TextInput>(initial);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 220.0f, 40.0f});
    assert(input.caret() == 8);

    tc_ui_key_event key {};
    key.type = TC_UI_KEY_DOWN;
    key.key = TC_UI_KEY_LEFT;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
    assert(document.set_focus(input));
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.caret() == 7);

    key.key = TC_UI_KEY_BACKSPACE;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "a\xc3\xa9" "b");
    assert(input.caret() == 3);

    key.key = TC_UI_KEY_DELETE;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "a\xc3\xa9");
    assert(input.caret() == 3);

    input.set_caret(2);
    assert(input.caret() == 1);
    tc_ui_text_event insert {};
    insert.text = "\xf0\x9f\x99\x82";
    assert(document.dispatch_text_event(insert) == TC_UI_EVENT_HANDLED);
    assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");
    assert(input.caret() == 5);

    tc_ui_text_event invalid {};
    invalid.text = "\xc3(";
    assert(document.dispatch_text_event(invalid) == TC_UI_EVENT_IGNORED);
    assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");
}

void test_text_input_scrolls_to_keep_caret_inside_clip() {
    Document document;
    install_test_text_measurer(document);
    DocumentBuilder ui(document);
    auto& input = ui.make_root<TextInput>("WWWWWWWWWWWW");
    assert(document.set_focus(input));
    document.layout_roots(tc_ui_rect {20.0f, 30.0f, 80.0f, 34.0f});
    assert(input.scroll_x() > 0.0f);

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);

    const float clip_left = input.bounds().x + 8.0f;
    const float clip_right = input.bounds().x + input.bounds().width - 8.0f;
    bool saw_shifted_text = false;
    bool saw_visible_caret = false;
    for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (!command) {
            continue;
        }
        if (command->type == TC_UI_DRAW_TEXT && command->p0.x < clip_left) {
            saw_shifted_text = true;
        }
        if (command->type == TC_UI_DRAW_LINE && near(command->p0.x, command->p1.x) &&
            command->p0.x >= clip_left && command->p0.x <= clip_right) {
            saw_visible_caret = true;
        }
    }
    assert(saw_shifted_text);
    assert(saw_visible_caret);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

void test_widget_signals_are_emitted_from_interactions() {
    Document document;
    DocumentBuilder ui(document);
    auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
    auto& button = ui.make<Button>("Run");
    auto& checkbox = ui.make<Checkbox>(false);
    auto& slider = ui.make<Slider>(0.0f);
    root.add_preferred_child(button);
    root.add_preferred_child(checkbox);
    root.add_stretch_child(slider);

    int clicked_a = 0;
    int clicked_b = 0;
    const size_t disconnected = button.clicked().connect([&clicked_a](Button&) {
        clicked_a += 1;
    });
    button.clicked().connect([&clicked_b](Button&) {
        clicked_b += 1;
    });
    assert(disconnected != 0);
    assert(button.clicked().disconnect(disconnected));

    int checkbox_changes = 0;
    bool last_checked = false;
    checkbox.changed().connect([&checkbox_changes, &last_checked](Checkbox&, bool checked) {
        checkbox_changes += 1;
        last_checked = checked;
    });

    int slider_changes = 0;
    float last_slider_value = 0.0f;
    slider.changed().connect([&slider_changes, &last_slider_value](Slider&, float value) {
        slider_changes += 1;
        last_slider_value = value;
    });

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 260.0f, 40.0f});

    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = button.bounds().x + 4.0f;
    event.y = button.bounds().y + 4.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(clicked_a == 0);
    assert(clicked_b == 0);
    assert(tc_widget_handle_eq(document.pointer_capture(), button.handle()));
    event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(clicked_a == 0);
    assert(clicked_b == 1);

    event.type = TC_UI_POINTER_DOWN;
    event.x = checkbox.bounds().x + 4.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(checkbox_changes == 0);
    event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(checkbox_changes == 1);
    assert(last_checked);

    slider.set_value(0.25f);
    slider.set_value(0.25f);
    assert(slider_changes == 1);
    assert(near(last_slider_value, 0.25f));

    event.type = TC_UI_POINTER_DOWN;
    event.x = slider.bounds().x + slider.bounds().width;
    event.y = slider.bounds().y + slider.bounds().height * 0.5f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(slider_changes == 2);
    assert(last_slider_value > 0.95f);
    assert(tc_widget_handle_eq(document.pointer_capture(), slider.handle()));
    event.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));
}

void test_containers_register_and_replace_canonical_children() {
    Document document;
    DocumentBuilder ui(document);

    auto& first_box = ui.make_root<HStack>("first-box");
    auto& second_box = ui.make_root<HStack>("second-box");
    auto& moving_panel = ui.make<Panel>("moving-panel");
    first_box.add_child(moving_panel);
    assert(first_box.child_count() == 1);
    assert(first_box.child_at(0) == moving_panel.c_widget());
    assert(moving_panel.parent_widget() == first_box.c_widget());

    second_box.add_child(moving_panel);
    assert(first_box.child_count() == 0);
    assert(second_box.child_count() == 1);
    assert(moving_panel.parent_widget() == second_box.c_widget());
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 200.0f, 60.0f});
    assert(near(moving_panel.bounds().width, 200.0f));

    auto& grid = ui.make_root<GridLayout>("grid");
    auto& grid_child = ui.make<Panel>("grid-child");
    grid.add_child(grid_child, 0, 0);
    assert(grid.child_count() == 1);
    assert(grid_child.parent_widget() == grid.c_widget());

    auto& group = ui.make_root<GroupBox>("group");
    auto& group_first = ui.make<Panel>("group-first");
    auto& group_second = ui.make<Panel>("group-second");
    group.set_content(group_first);
    group.set_content(group_second);
    assert(group.child_count() == 1);
    assert(group.child_at(0) == group_second.c_widget());
    assert(group_first.parent_widget() == nullptr);
    assert(group_second.parent_widget() == group.c_widget());

    auto& splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
    auto& split_first = ui.make<Panel>("split-first");
    auto& split_second = ui.make<Panel>("split-second");
    auto& split_replacement = ui.make<Panel>("split-replacement");
    splitter.set_first(split_first);
    splitter.set_second(split_second);
    splitter.set_first(split_replacement);
    assert(splitter.child_count() == 2);
    assert(splitter.child_at(0) == split_replacement.c_widget());
    assert(splitter.child_at(1) == split_second.c_widget());
    assert(split_first.parent_widget() == nullptr);

    auto& scroll = ui.make_root<ScrollArea>("scroll");
    auto& scroll_first = ui.make<Panel>("scroll-first");
    auto& scroll_second = ui.make<Panel>("scroll-second");
    scroll.set_content(scroll_first);
    scroll.set_content(scroll_second);
    assert(scroll.child_count() == 1);
    assert(scroll.child_at(0) == scroll_second.c_widget());
    assert(scroll_first.parent_widget() == nullptr);

    auto& tabs = ui.make_root<TabView>("tabs");
    auto& tab_first = ui.make<Panel>("tab-first");
    auto& tab_second = ui.make<Panel>("tab-second");
    tabs.add_page("First", tab_first);
    tabs.add_page("Second", tab_second);
    assert(tabs.child_count() == 2);
    assert(tabs.child_at(0) == tab_first.c_widget());
    assert(tabs.child_at(1) == tab_second.c_widget());
}

void test_common_visibility_enabled_and_mouse_transparent_state() {
    Document document;
    DocumentBuilder ui(document);
    auto& root = ui.make_root<HStack>("root");
    auto& hidden = ui.make<Panel>("hidden");
    auto& button = ui.make<Button>("button");
    hidden.set_preferred_size(tc_ui_size {40.0f, 30.0f});
    root.add_preferred_child(hidden);
    root.add_stretch_child(button);

    hidden.set_visible(false);
    button.set_enabled(false);
    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 160.0f, 40.0f});
    assert(near(button.bounds().x, 0.0f));
    assert(near(button.bounds().width, 160.0f));

    tc_widget_handle hit = document.hit_test(20.0f, 20.0f);
    assert(tc_widget_handle_eq(hit, button.handle()));
    tc_ui_pointer_event event {};
    event.type = TC_UI_POINTER_DOWN;
    event.x = 20.0f;
    event.y = 20.0f;
    assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_IGNORED);
    assert(tc_widget_handle_is_invalid(document.focused_widget()));

    button.set_mouse_transparent(true);
    hit = document.hit_test(20.0f, 20.0f);
    assert(tc_widget_handle_eq(hit, root.handle()));
    root.set_mouse_transparent(true);
    assert(tc_widget_handle_is_invalid(document.hit_test(20.0f, 20.0f)));
}

} // namespace

int main() {
    test_box_layout_sets_child_bounds_and_paints();
    test_widget_metadata_is_owned_and_exposed();
    test_dirty_flags_track_layout_paint_and_state_changes();
    test_box_layout_child_policies_allocate_primary_axis();
    test_hstack_vstack_wrappers_use_expected_orientation();
    test_grid_layout_tracks_spans_and_hit_test();
    test_grid_layout_recursive_destroy_children();
    test_group_box_lays_out_content_and_routes_hit_test();
    test_group_box_recursive_destroy_content();
    test_splitter_layout_drag_and_hit_test();
    test_splitter_recursive_destroy_children();
    test_scroll_area_lays_out_content_with_clip_and_scroll();
    test_scroll_area_wheel_clamps_and_recursive_destroy_content();
    test_tab_view_switches_selected_page_and_clips_paint();
    test_tab_view_recursive_destroy_pages();
    test_box_layout_shrinks_flexible_children_before_overflowing();
    test_box_layout_respects_child_extent_limits();
    test_box_layout_allows_preferred_overflow_when_no_child_can_shrink();
    test_document_hit_test_returns_deepest_child();
    test_document_hit_test_prefers_topmost_root();
    test_box_layout_hit_test_skips_stale_child_handles();
    test_pointer_dispatch_updates_hovered_widget();
    test_pointer_capture_routes_events_outside_bounds_until_release();
    test_destroy_clears_hover_and_pointer_capture();
    test_focus_and_key_text_dispatch_follow_focused_widget();
    test_focus_api_rejects_non_focusable_and_clears_on_destroy();
    test_recursive_destroy_removes_container_children();
    test_controls_handle_pointer_events();
    test_separator_layout_and_paint_command();
    test_text_input_focus_text_edit_and_submit();
    test_text_widgets_clip_text_paint();
    test_text_measurement_uses_proportional_metrics();
    test_text_input_edits_utf8_at_codepoint_boundaries();
    test_text_input_scrolls_to_keep_caret_inside_clip();
    test_widget_signals_are_emitted_from_interactions();
    test_containers_register_and_replace_canonical_children();
    test_common_visibility_enabled_and_mouse_transparent_state();
    return EXIT_SUCCESS;
}
