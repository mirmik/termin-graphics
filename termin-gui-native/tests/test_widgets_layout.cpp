#include <termin/gui_native/tc_document.hpp>

#include "widgets_test_support.hpp"

namespace termin_gui_native_test {
    using termin::SrgbColor;
    namespace {

        class WidthDependentWidget final : public NativeWidget {
        public:
            std::vector<tc_ui_constraints> measurements;

            explicit WidthDependentWidget(const char* debug_name = nullptr)
                : NativeWidget(debug_name) {}

            tc_ui_size measure(tc_ui_document_handle, tc_ui_constraints constraints) override {
                measurements.push_back(constraints);
                const float max_width = constraints.max_size.width > 0.0f ? constraints.max_size.width : 200.0f;
                const float width = std::max(constraints.min_size.width, std::min(200.0f, max_width));
                const float reflow_height = std::ceil(200.0f / std::max(1.0f, width)) * 10.0f;
                const float max_height =
                    constraints.max_size.height > 0.0f ? constraints.max_size.height : reflow_height;
                return tc_ui_size{width, std::max(constraints.min_size.height, std::min(reflow_height, max_height))};
            }
        };

    } // namespace

    void test_box_layout_sets_child_bounds_and_paints() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
        root.set_padding(EdgeInsets{4.0f, 6.0f, 4.0f, 6.0f})
            .set_spacing(2.0f)
            .set_background(SrgbColor{0.1f, 0.1f, 0.1f, 1.0f});

        auto& first = ui.make<Panel>("first");
        auto& second = ui.make<Panel>("second");
        root.add_child(first);
        root.add_child(second);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 108.0f, 112.0f});

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

        /* Root fill + root clip pair + two panel fills. Panels are borderless
           by default; an outline must now be requested explicitly. */
        assert(tc_ui_draw_list_command_count(draw_list) == 5);

        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        tc_ui_document_destroy(document_handle);
    }

    void test_widget_metadata_is_owned_and_exposed() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
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

        tc_ui_document_destroy(document_handle);
    }

    void test_dirty_flags_track_layout_paint_and_state_changes() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        root.clear_dirty(TC_WIDGET_DIRTY_MASK);
        root.set_spacing(4.0f);
        assert(root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT));
        assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_STATE));

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});
        assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT));
        assert(root.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));

        root.clear_dirty(TC_WIDGET_DIRTY_MASK);
        root.set_background(SrgbColor{0.1f, 0.2f, 0.3f, 1.0f});
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

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_child_policies_allocate_primary_axis() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& fixed = ui.make<Spacer>(tc_ui_size{10.0f, 12.0f});
        auto& preferred = ui.make<Spacer>(tc_ui_size{30.0f, 18.0f});
        auto& flex_one = ui.make<Spacer>(tc_ui_size{20.0f, 12.0f});
        auto& flex_two = ui.make<Spacer>(tc_ui_size{20.0f, 12.0f});

        root.add_fixed_child(fixed, 50.0f);
        root.add_preferred_child(preferred);
        root.add_flex_child(flex_one, 1.0f);
        root.add_flex_child(flex_two, 2.0f);

        assert(root.items().size() == 4);
        assert(root.items()[0].policy == LayoutPolicy::Fixed);
        assert(root.items()[1].policy == LayoutPolicy::Preferred);
        assert(root.items()[2].policy == LayoutPolicy::Flex);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 40.0f});

        assert(near(fixed.bounds().x, 0.0f));
        assert(near(fixed.bounds().width, 50.0f));
        assert(near(preferred.bounds().x, 50.0f));
        assert(near(preferred.bounds().width, 30.0f));
        assert(near(flex_one.bounds().x, 80.0f));
        assert(near(flex_one.bounds().width, 80.0f));
        assert(near(flex_two.bounds().x, 160.0f));
        assert(near(flex_two.bounds().width, 140.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_hstack_vstack_wrappers_use_expected_orientation() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<VStack>("root-vstack");
        auto& row = ui.make<HStack>("row-hstack");
        auto& left = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
        auto& right = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
        auto& bottom = ui.make<Spacer>(tc_ui_size{30.0f, 8.0f});

        root.add_preferred_child(row);
        root.add_preferred_child(bottom);
        row.add_preferred_child(left);
        row.add_preferred_child(right);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});

        assert(near(row.bounds().y, 0.0f));
        assert(near(row.bounds().height, 10.0f));
        assert(near(bottom.bounds().y, row.bounds().height));
        assert(near(bottom.bounds().y, 10.0f));
        assert(near(bottom.bounds().height, 8.0f));
        assert(near(left.bounds().x, 0.0f));
        assert(near(right.bounds().x, left.bounds().width));

        tc_ui_document_destroy(document_handle);
    }

    void test_grid_layout_tracks_spans_and_hit_test() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& grid = ui.make_root<GridLayout>("grid");
        grid.set_padding(EdgeInsets{2.0f, 3.0f, 4.0f, 5.0f}).set_spacing(10.0f, 6.0f);
        grid.add_column(LayoutPolicy::Fixed, 40.0f);
        grid.add_column(LayoutPolicy::Stretch);
        grid.add_column(LayoutPolicy::Flex, 2.0f);
        grid.add_row(LayoutPolicy::Preferred);
        grid.add_row(LayoutPolicy::Stretch);

        auto& fixed_cell = ui.make<Spacer>(tc_ui_size{30.0f, 20.0f});
        auto& spanning = ui.make<Spacer>(tc_ui_size{90.0f, 12.0f});
        auto& bottom = ui.make<Panel>("bottom");
        bottom.set_preferred_size(tc_ui_size{20.0f, 30.0f});
        grid.add_child(fixed_cell, 0, 0);
        grid.add_child(spanning, 0, 1, 1, 2);
        grid.add_child(bottom, 1, 2);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 100.0f});

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

        tc_ui_document_destroy(document_handle);
    }

    void test_box_grid_and_scroll_remeasure_width_dependent_children() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& box = ui.make_root<BoxLayout>(Orientation::Vertical, "reflow-box");
        auto& box_child = ui.make<WidthDependentWidget>("box-child");
        box.add_preferred_child(box_child);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});
        assert(near(box_child.bounds().width, 100.0f));
        assert(near(box_child.bounds().height, 20.0f));
        assert(box_child.measurements.size() == 2);
        assert(near(box_child.measurements.back().min_size.width, 100.0f));
        assert(near(box_child.measurements.back().max_size.width, 100.0f));

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 50.0f, 100.0f});
        assert(near(box_child.bounds().width, 50.0f));
        assert(near(box_child.bounds().height, 40.0f));
        assert(box_child.measurements.size() == 4);

        assert(tc_ui_document_destroy_widget_recursive(document.get(), box.handle()));

        auto& row = ui.make_root<BoxLayout>(Orientation::Horizontal, "reflow-row");
        row.set_cross_axis_alignment(CrossAxisAlignment::Start);
        auto& left = ui.make<WidthDependentWidget>("row-left");
        auto& right = ui.make<WidthDependentWidget>("row-right");
        row.add_flex_child(left, 1.0f);
        row.add_flex_child(right, 1.0f);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});
        assert(near(left.bounds().width, 50.0f));
        assert(near(right.bounds().width, 50.0f));
        assert(near(left.bounds().height, 40.0f));
        assert(near(right.bounds().height, 40.0f));
        assert(left.measurements.size() == 2);
        assert(right.measurements.size() == 2);

        assert(tc_ui_document_destroy_widget_recursive(document.get(), row.handle()));

        auto& grid = ui.make_root<GridLayout>("reflow-grid");
        grid.add_column(LayoutPolicy::Stretch);
        grid.add_row(LayoutPolicy::Preferred);
        auto& grid_child = ui.make<WidthDependentWidget>("grid-child");
        grid.add_child(grid_child, 0, 0);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 50.0f, 100.0f});
        assert(near(grid_child.bounds().width, 50.0f));
        assert(near(grid_child.bounds().height, 40.0f));
        assert(grid_child.measurements.size() == 3);
        assert(near(grid_child.measurements.back().min_size.width, 50.0f));
        assert(near(grid_child.measurements.back().max_size.width, 50.0f));

        assert(tc_ui_document_destroy_widget_recursive(document.get(), grid.handle()));

        auto& scroll = ui.make_root<ScrollArea>("reflow-scroll");
        scroll.set_scroll_axes(false, true);
        auto& scroll_child = ui.make<WidthDependentWidget>("scroll-child");
        scroll.set_content(scroll_child);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 50.0f, 30.0f});
        assert(near(scroll.content_size().width, 50.0f));
        assert(near(scroll.content_size().height, 40.0f));
        assert(near(scroll_child.bounds().width, 50.0f));
        assert(near(scroll_child.bounds().height, 40.0f));
        scroll.set_scroll(0.0f, 100.0f);
        assert(near(scroll.scroll_y(), 10.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_resolves_percent_and_limits_from_definite_parent() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& box = ui.make_root<BoxLayout>(Orientation::Vertical, "percent-box");
        box.set_cross_axis_alignment(CrossAxisAlignment::Start);
        auto& child = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
        tc_ui_widget_layout_spec spec = tc_ui_widget_layout_spec_default();
        spec.width = tc_ui_length{TC_UI_LENGTH_PERCENT, 0.5f};
        spec.min_width = 60.0f;
        spec.max_width = 80.0f;
        spec.margin = tc_ui_insets{5.0f, 2.0f, 7.0f, 3.0f};
        assert(child.set_layout_spec(spec));
        box.add_preferred_child(child);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});
        assert(near(child.bounds().x, 5.0f));
        assert(near(child.bounds().y, 2.0f));
        assert(near(child.bounds().width, 60.0f));
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});
        assert(near(child.bounds().width, 80.0f));

        const tc_ui_size intrinsic = child.measure(
            document.get(), tc_ui_constraints{tc_ui_size{0.0f, 0.0f}, tc_ui_size{1000000.0f, 1000000.0f}});
        assert(near(intrinsic.width, 20.0f));

        assert(tc_ui_document_destroy_widget_recursive(document.get(), box.handle()));
        auto& grid = ui.make_root<GridLayout>("percent-grid");
        grid.add_column(LayoutPolicy::Stretch);
        grid.add_row(LayoutPolicy::Preferred);
        auto& grid_child = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
        assert(grid_child.set_layout_spec(spec));
        grid.add_child(grid_child, 0, 0);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});
        assert(near(grid_child.bounds().x, 5.0f));
        assert(near(grid_child.bounds().width, 80.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_grid_layout_recursive_destroy_children() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& grid = ui.make_root<GridLayout>("grid");
        auto& first = ui.make<Panel>("first");
        auto& second = ui.make<Panel>("second");
        grid.add_child(first, 0, 0);
        grid.add_child(second, 1, 1);

        assert(tc_ui_document_live_widget_count(document.get()) == 3);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), grid.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_group_box_lays_out_content_and_routes_hit_test() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& group = ui.make_root<GroupBox>("Settings");
        group.set_padding(EdgeInsets{8.0f, 6.0f, 10.0f, 12.0f});
        auto& content = ui.make<Panel>("content");
        group.set_content(content);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 180.0f, 120.0f});
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

        tc_ui_document_destroy(document_handle);
    }

    void test_group_box_recursive_destroy_content() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& group = ui.make_root<GroupBox>("Settings");
        auto& content = ui.make<Panel>("content");
        group.set_content(content);

        assert(tc_ui_document_live_widget_count(document.get()) == 2);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), group.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_splitter_layout_drag_and_hit_test() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
        splitter.set_split_fraction(0.25f).set_min_extents(20.0f, 20.0f).set_divider_thickness(8.0f);
        auto& left = ui.make<Panel>("left");
        auto& right = ui.make<Panel>("right");
        splitter.set_first(left);
        splitter.set_second(right);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 208.0f, 80.0f});
        assert(near(left.bounds().width, 50.0f));
        assert(near(right.bounds().x, 58.0f));
        assert(near(right.bounds().width, 150.0f));
        assert(tc_widget_handle_eq(document.hit_test(54.0f, 10.0f), splitter.handle()));
        assert(tc_widget_handle_eq(document.hit_test(100.0f, 10.0f), right.handle()));

        tc_ui_pointer_event event{};
        event.type = TC_UI_POINTER_MOVE;
        event.x = 54.0f;
        event.y = 10.0f;
        assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_IGNORED);
        assert(tc_widget_handle_eq(document.hovered_widget(), splitter.handle()));

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(context);
        const tc_ui_draw_command* divider_command =
            tc_ui_draw_list_command_at(draw_list, tc_ui_draw_list_command_count(draw_list) - 1);
        assert(divider_command && divider_command->type == TC_UI_DRAW_FILL_RECT);
        assert(near(divider_command->rect.width, 2.0f));
        assert(near(divider_command->color.b, 0.88f));
        tc_ui_paint_context_destroy(context);
        tc_ui_draw_list_destroy(draw_list);

        event.type = TC_UI_POINTER_DOWN;
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

        tc_ui_document_destroy(document_handle);
    }

    void test_splitter_recursive_destroy_children() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& splitter = ui.make_root<Splitter>(Orientation::Vertical, "splitter");
        auto& first = ui.make<Panel>("first");
        auto& second = ui.make<Panel>("second");
        splitter.set_first(first);
        splitter.set_second(second);

        assert(tc_ui_document_live_widget_count(document.get()) == 3);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), splitter.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_scroll_area_lays_out_content_with_clip_and_scroll() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& scroll = ui.make_root<ScrollArea>("scroll");
        auto& content = ui.make<VStack>("scroll-content");
        auto& top = ui.make<Panel>("top");
        auto& bottom = ui.make<Panel>("bottom");
        content.add_fixed_child(top, 80.0f);
        content.add_fixed_child(bottom, 80.0f);
        content.set_preferred_size(tc_ui_size{120.0f, 200.0f});
        scroll.set_content(content);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});
        assert(near(content.bounds().x, 0.0f));
        assert(near(content.bounds().y, 0.0f));
        assert(near(scroll.content_size().width, 120.0f));
        assert(near(scroll.content_size().height, 200.0f));
        assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), top.handle()));
        assert(tc_widget_handle_eq(document.hit_test(10.0f, 70.0f), tc_widget_handle_invalid()));

        scroll.set_scroll(0.0f, 40.0f);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});
        assert(near(scroll.scroll_y(), 40.0f));
        assert(near(content.bounds().y, -40.0f));
        assert(tc_widget_handle_eq(document.hit_test(10.0f, 45.0f), bottom.handle()));

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        document.paint_roots(paint_context);

        assert(tc_ui_draw_list_command_count(draw_list) >= 4);
        const tc_ui_draw_command* first = tc_ui_draw_list_command_at(draw_list, 0);
        assert(first && first->type == TC_UI_DRAW_PUSH_CLIP);
        assert(near(first->rect.width, 100.0f));
        bool found_pop_clip = false;
        for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
            const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
            found_pop_clip = found_pop_clip || (command && command->type == TC_UI_DRAW_POP_CLIP);
        }
        assert(found_pop_clip);

        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);

        tc_ui_document_destroy(document_handle);
    }

    void test_scroll_area_can_fit_content_to_disabled_scroll_axis() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& scroll = ui.make_root<ScrollArea>("scroll");
        auto& content = ui.make<VStack>("scroll-content");
        content.set_preferred_size(tc_ui_size{200.0f, 180.0f});
        scroll.set_content(content);
        scroll.set_scroll_axes(false, true);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});

        assert(!scroll.horizontal_scroll_enabled());
        assert(scroll.vertical_scroll_enabled());
        assert(near(scroll.content_size().width, 100.0f));
        assert(near(content.bounds().width, 100.0f));
        assert(near(scroll.content_size().height, 180.0f));
        scroll.set_scroll(50.0f, 40.0f);
        assert(near(scroll.scroll_x(), 0.0f));
        assert(near(scroll.scroll_y(), 40.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_scroll_area_wheel_clamps_and_recursive_destroy_content() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& scroll = ui.make_root<ScrollArea>("scroll");
        auto& content = ui.make<VStack>("scroll-content");
        auto& area = ui.make<TextArea>("short text");
        content.add_fixed_child(area, 180.0f);
        scroll.set_content(content);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 50.0f});

        tc_ui_pointer_event wheel{};
        wheel.type = TC_UI_POINTER_WHEEL;
        wheel.x = 10.0f;
        wheel.y = 10.0f;
        wheel.wheel_y = -10.0f;
        assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
        assert(near(scroll.scroll_y(), 130.0f));

        wheel.wheel_y = 10.0f;
        assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
        assert(near(scroll.scroll_y(), 0.0f));
        assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_IGNORED);

        assert(tc_ui_document_live_widget_count(document.get()) == 3);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), scroll.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_scroll_area_programmatic_keyboard_thumb_and_focus_contract() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& scroll = ui.make_root<ScrollArea>("scroll");
        auto& content = ui.make<VStack>("content");
        auto& top = ui.make<Panel>("top");
        auto& bottom = ui.make<Button>("bottom");
        content.add_fixed_child(top, 80.0f);
        content.add_fixed_child(bottom, 30.0f);
        content.set_preferred_size(tc_ui_size{120.0f, 180.0f});
        scroll.set_content(content);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});

        int change_count = 0;
        float observed_y = -1.0f;
        scroll.changed().connect([&](ScrollArea&, float, float y) {
            ++change_count;
            observed_y = y;
        });

        scroll.set_scroll(12.0f, 40.0f);
        assert(near(content.bounds().x, -12.0f));
        assert(near(content.bounds().y, -40.0f));
        assert(change_count == 1);
        assert(near(observed_y, 40.0f));

        scroll.set_scrollbar_policy(ScrollBarPolicy::Hidden, ScrollBarPolicy::Always);
        assert(!scroll.horizontal_scrollbar_visible());
        assert(scroll.vertical_scrollbar_visible());
        scroll.set_scrollbar_policy(ScrollBarPolicy::Auto, ScrollBarPolicy::Auto);
        assert(scroll.horizontal_scrollbar_visible());
        assert(scroll.vertical_scrollbar_visible());

        assert(document.set_focus(scroll));
        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = TC_UI_KEY_END;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(near(scroll.scroll_y(), 120.0f));
        assert(near(content.bounds().y, -120.0f));
        key.key = TC_UI_KEY_DOWN_ARROW;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
        key.key = TC_UI_KEY_HOME;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(near(scroll.scroll_y(), 0.0f));

        scroll.set_scroll_axes(false, true);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});
        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.button = tcbase::mouse_button_value(tcbase::MouseButton::LEFT);
        pointer.x = 92.0f;
        pointer.y = 5.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.pointer_capture(), scroll.handle()));
        pointer.type = TC_UI_POINTER_MOVE;
        pointer.y = 50.0f;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(near(scroll.scroll_y(), 120.0f));
        assert(near(content.bounds().y, -120.0f));
        pointer.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        scroll.set_scroll(0.0f, 0.0f);
        assert(document.set_focus(bottom));
        assert(scroll.scroll_y() > 0.0f);
        assert(bottom.bounds().y + bottom.bounds().height <= scroll.bounds().y + scroll.bounds().height + 0.001f);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 300.0f});
        assert(near(scroll.scroll_y(), 0.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_nested_scroll_area_bubbles_wheel_at_inner_boundary() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& outer = ui.make_root<ScrollArea>("outer");
        auto& outer_content = ui.make<VStack>("outer-content");
        auto& inner = ui.make<ScrollArea>("inner");
        auto& inner_content = ui.make<Panel>("inner-content");
        auto& tail = ui.make<Panel>("tail");
        inner_content.set_preferred_size(tc_ui_size{100.0f, 200.0f});
        inner.set_content(inner_content);
        inner.set_scroll_axes(false, true);
        outer_content.add_fixed_child(inner, 100.0f);
        outer_content.add_fixed_child(tail, 300.0f);
        outer_content.set_preferred_size(tc_ui_size{100.0f, 400.0f});
        outer.set_content(outer_content);
        outer.set_scroll_axes(false, true);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});

        inner.set_scroll(0.0f, 100.0f);
        assert(near(inner.scroll_y(), 100.0f));
        tc_ui_pointer_event wheel{};
        wheel.type = TC_UI_POINTER_WHEEL;
        wheel.x = 10.0f;
        wheel.y = 10.0f;
        wheel.wheel_y = -1.0f;
        assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
        assert(near(inner.scroll_y(), 100.0f));
        assert(near(outer.scroll_y(), 48.0f));
        assert(near(outer_content.bounds().y, -48.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_switches_selected_page_and_clips_paint() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first = ui.make<Panel>("first-page");
        auto& second = ui.make<Panel>("second-page");
        tabs.add_page("First", first);
        tabs.add_page("Second", second);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 100.0f});
        assert(tabs.page_count() == 2);
        assert(tabs.selected_index() == 0);
        assert(near(first.bounds().y, 32.0f));
        assert(near(first.bounds().height, 68.0f));
        assert(near(second.bounds().width, 0.0f));
        assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), tabs.handle()));
        assert(tc_widget_handle_eq(document.hit_test(10.0f, 40.0f), first.handle()));

        tc_ui_pointer_event event{};
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

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_recursive_destroy_pages() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first = ui.make<Panel>("first-page");
        auto& second = ui.make<Panel>("second-page");
        tabs.add_page("First", first);
        tabs.add_page("Second", second);

        assert(tc_ui_document_live_widget_count(document.get()) == 3);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), tabs.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_page_mutation_and_selection_signal() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first = ui.make<Panel>("first-page");
        auto& second = ui.make<Panel>("second-page");
        tabs.add_page("First", first);
        tabs.add_page("Second", second);
        std::vector<size_t> selected;
        tabs.selection_changed().connect([&selected](TabView&, size_t index) { selected.push_back(index); });

        tabs.set_selected_index(1);
        assert((selected == std::vector<size_t>{1}));
        assert(tabs.set_page_title(1, "Renamed"));
        assert(tabs.page_title(1) == "Renamed");
        assert(tc_widget_handle_eq(tabs.page_handle(1), second.handle()));
        assert(tabs.remove_page(1));
        assert(tabs.page_count() == 1);
        assert(tabs.selected_index() == 0);
        assert(second.parent_widget() == nullptr);
        assert((selected == std::vector<size_t>{1, 0}));

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_focus_traversal_uses_only_selected_page() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first_page = ui.make<BoxLayout>(Orientation::Vertical, "first-page");
        auto& second_page = ui.make<BoxLayout>(Orientation::Vertical, "second-page");
        auto& first_a = ui.make<FocusProbe>();
        auto& first_b = ui.make<FocusProbe>();
        auto& second_a = ui.make<FocusProbe>();
        auto& second_b = ui.make<FocusProbe>();
        first_page.add_preferred_child(first_a);
        first_page.add_preferred_child(first_b);
        second_page.add_preferred_child(second_a);
        second_page.add_preferred_child(second_b);
        tabs.add_page("First", first_page);
        tabs.add_page("Second", second_page);

        assert(first_page.tree_participating());
        assert(!second_page.tree_participating());
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), first_a.handle()));
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), first_b.handle()));

        tabs.set_selected_index(1);
        assert(tc_widget_handle_is_invalid(document.focused_widget()));
        assert(!first_page.tree_participating());
        assert(second_page.tree_participating());
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), second_a.handle()));
        assert(document.set_focus(tabs));
        assert(document.focus_previous());
        assert(tc_widget_handle_eq(document.focused_widget(), second_b.handle()));

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_removing_selected_page_clears_focus_and_restores_reuse_state() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first = ui.make<FocusProbe>();
        auto& second = ui.make<FocusProbe>();
        tabs.add_page("First", first);
        tabs.add_page("Second", second);
        tabs.set_selected_index(1);
        assert(document.set_focus(second));

        assert(tabs.remove_page(1));
        assert(tc_widget_handle_is_invalid(document.focused_widget()));
        assert(second.parent_widget() == nullptr);
        assert(second.tree_participating());
        assert(first.tree_participating());
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), first.handle()));

        tabs.add_page("Second again", second);
        assert(second.parent_widget() == tabs.c_widget());
        assert(!second.tree_participating());
        assert(second.visible());
        assert(second.enabled());

        tc_ui_document_destroy(document_handle);
    }

    void test_tab_view_selected_hidden_or_disabled_page_has_no_focus_fallback() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& tabs = ui.make_root<TabView>("tabs");
        auto& first = ui.make<FocusProbe>();
        auto& second = ui.make<FocusProbe>();
        tabs.add_page("First", first);
        tabs.add_page("Second", second);
        tabs.set_selected_index(1);

        second.set_visible(false);
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.clear_focus(tabs));
        assert(first.visible());
        assert(first.enabled());
        assert(!first.tree_participating());

        second.set_visible(true);
        second.set_enabled(false);
        assert(document.focus_previous());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.clear_focus(tabs));
        assert(second.tree_participating());

        second.set_enabled(true);
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), tabs.handle()));
        assert(document.focus_next());
        assert(tc_widget_handle_eq(document.focused_widget(), second.handle()));
        assert(first.visible());
        assert(first.enabled());

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_shrinks_flexible_children_before_overflowing() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& fixed = ui.make<Spacer>(tc_ui_size{10.0f, 12.0f});
        auto& preferred = ui.make<Spacer>(tc_ui_size{80.0f, 12.0f});
        auto& stretch_one = ui.make<Spacer>(tc_ui_size{100.0f, 12.0f});
        auto& stretch_two = ui.make<Spacer>(tc_ui_size{100.0f, 12.0f});

        root.add_fixed_child(fixed, 50.0f);
        root.add_preferred_child(preferred);
        root.add_stretch_child(stretch_one);
        root.add_stretch_child(stretch_two);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 180.0f, 24.0f});

        assert(near(fixed.bounds().width, 50.0f));
        assert(near(preferred.bounds().width, 80.0f));
        assert(near(stretch_one.bounds().x, 130.0f));
        assert(near(stretch_one.bounds().width, 25.0f));
        assert(near(stretch_two.bounds().x, 155.0f));
        assert(near(stretch_two.bounds().width, 25.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_respects_child_extent_limits() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& capped = ui.make<Spacer>(tc_ui_size{50.0f, 12.0f});
        auto& uncapped = ui.make<Spacer>(tc_ui_size{50.0f, 12.0f});

        root.add_stretch_child(capped);
        root.add_stretch_child(uncapped);
        assert(root.set_child_extent_limits(capped, 0.0f, 80.0f));
        assert(root.items()[0].max_extent == 80.0f);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 24.0f});

        assert(near(capped.bounds().width, 80.0f));
        assert(near(uncapped.bounds().x, 80.0f));
        assert(near(uncapped.bounds().width, 220.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_cross_axis_alignment_and_exact_placement() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& start = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
        auto& center = ui.make<Spacer>(tc_ui_size{20.0f, 20.0f});
        auto& end = ui.make<Spacer>(tc_ui_size{20.0f, 30.0f});
        root.set_padding({5.0f, 4.0f, 5.0f, 6.0f})
            .set_spacing(2.0f)
            .set_cross_axis_alignment(CrossAxisAlignment::Center);
        root.add_child(start);
        root.add_child(center);
        root.add_child(end);
        assert(root.set_child_placement(
            start, LayoutPolicy::Fixed, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, CrossAxisAlignment::Start));
        assert(root.set_child_placement(center, LayoutPolicy::Preferred, 0.0f, 1.0f, 1.0f, 0.0f, 40.0f));
        assert(
            root.set_child_placement(end, LayoutPolicy::Flex, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, CrossAxisAlignment::End));
        assert(!root.set_child_placement(end, LayoutPolicy::Fixed, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f));
        assert(root.items()[2].policy == LayoutPolicy::Flex);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 144.0f, 60.0f});

        assert(root.orientation() == Orientation::Horizontal);
        assert(root.cross_axis_alignment() == CrossAxisAlignment::Center);
        assert(near(start.bounds().x, 5.0f));
        assert(near(start.bounds().y, 4.0f));
        assert(near(start.bounds().width, 30.0f));
        assert(near(start.bounds().height, 10.0f));
        assert(near(center.bounds().x, 37.0f));
        assert(near(center.bounds().y, 19.0f));
        assert(near(center.bounds().width, 40.0f));
        assert(near(center.bounds().height, 20.0f));
        assert(near(end.bounds().x, 79.0f));
        assert(near(end.bounds().y, 24.0f));
        assert(near(end.bounds().width, 60.0f));
        assert(near(end.bounds().height, 30.0f));

        root.set_orientation(Orientation::Vertical);
        assert(root.orientation() == Orientation::Vertical);

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_allows_preferred_overflow_when_no_child_can_shrink() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& first = ui.make<Spacer>(tc_ui_size{80.0f, 12.0f});
        auto& second = ui.make<Spacer>(tc_ui_size{60.0f, 12.0f});

        root.add_preferred_child(first);
        root.add_preferred_child(second);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 24.0f});

        assert(near(first.bounds().width, 80.0f));
        assert(near(second.bounds().x, 80.0f));
        assert(near(second.bounds().width, 60.0f));

        tc_ui_document_destroy(document_handle);
    }

    void test_document_hit_test_returns_deepest_child() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& first = ui.make<Panel>("first");
        auto& second = ui.make<Panel>("second");
        root.add_stretch_child(first);
        root.add_stretch_child(second);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});

        assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), first.handle()));
        assert(tc_widget_handle_eq(document.hit_test(150.0f, 10.0f), second.handle()));
        assert(tc_widget_handle_is_invalid(document.hit_test(250.0f, 10.0f)));

        tc_ui_document_destroy(document_handle);
    }

    void test_document_hit_test_prefers_topmost_root() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& bottom = ui.make_root<Panel>("bottom-root");
        auto& top = ui.make<Panel>("top-root");
        assert(document.add_root(top));

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});

        assert(tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), top.handle()));
        assert(!tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), bottom.handle()));

        tc_ui_document_destroy(document_handle);
    }

    void test_box_layout_hit_test_skips_stale_child_handles() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& child = ui.make<Panel>("child");
        root.add_stretch_child(child);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});
        tc_widget_handle child_handle = child.handle();
        assert(tc_ui_document_destroy_widget(document.get(), child_handle));

        assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), root.handle()));

        tc_ui_document_destroy(document_handle);
    }

    void test_pointer_dispatch_updates_hovered_widget() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& first = ui.make<Panel>("first");
        auto& second = ui.make<Panel>("second");
        root.add_stretch_child(first);
        root.add_stretch_child(second);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});

        tc_ui_pointer_event event{};
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

        tc_ui_document_destroy(document_handle);
    }

    void test_pointer_capture_routes_events_outside_bounds_until_release() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& probe = ui.make<CapturingProbe>();
        auto& panel = ui.make<Panel>("panel");
        root.add_preferred_child(probe);
        root.add_stretch_child(panel);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 160.0f, 40.0f});

        tc_ui_pointer_event event{};
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

        tc_ui_document_destroy(document_handle);
    }

    void test_destroy_clears_hover_and_pointer_capture() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& probe = ui.make<CapturingProbe>();
        root.add_stretch_child(probe);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});

        tc_ui_pointer_event event{};
        event.type = TC_UI_POINTER_DOWN;
        event.x = 10.0f;
        event.y = 10.0f;
        assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.hovered_widget(), probe.handle()));
        assert(tc_widget_handle_eq(document.pointer_capture(), probe.handle()));

        assert(tc_ui_document_destroy_widget(document.get(), probe.handle()));
        assert(tc_widget_handle_is_invalid(document.hovered_widget()));
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));

        tc_ui_document_destroy(document_handle);
    }

    void test_remove_child_clears_subtree_focus_and_preserves_reuse() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& container = ui.make<BoxLayout>(Orientation::Horizontal, "container");
        auto& focusable = ui.make<FocusProbe>();
        container.add_preferred_child(focusable);
        root.add_stretch_child(container);

        assert(document.set_focus(focusable));
        assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));
        assert(root.remove_child(container));
        assert(tc_widget_handle_is_invalid(document.focused_widget()));
        assert(container.parent_widget() == nullptr);
        assert(tc_ui_document_is_alive(document.get(), container.handle()));
        assert(tc_ui_document_is_alive(document.get(), focusable.handle()));

        assert(root.append_child(container));
        assert(document.set_focus(focusable));
        assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));

        tc_ui_document_destroy(document_handle);
    }

    void test_detach_clears_pointer_interaction_state_only_inside_subtree() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& probe = ui.make<CapturingProbe>();
        auto& outside = ui.make<FocusProbe>();
        root.add_stretch_child(probe);
        root.add_preferred_child(outside);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 160.0f, 40.0f});

        tc_ui_pointer_event event{};
        event.type = TC_UI_POINTER_DOWN;
        event.x = 10.0f;
        event.y = 10.0f;
        assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
        assert(tc_widget_handle_eq(document.hovered_widget(), probe.handle()));
        assert(tc_widget_handle_eq(document.pointer_capture(), probe.handle()));
        assert(tc_widget_handle_eq(document.pressed_widget(), probe.handle()));
        assert(document.set_focus(outside));

        assert(probe.detach());
        assert(probe.cancel_count == 1);
        assert(probe.last_cancel_reason == TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE);
        assert(tc_widget_handle_is_invalid(document.hovered_widget()));
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));
        assert(tc_widget_handle_is_invalid(document.pressed_widget()));
        assert(tc_widget_handle_eq(document.focused_widget(), outside.handle()));
        assert(tc_ui_document_is_alive(document.get(), probe.handle()));

        tc_ui_document_destroy(document_handle);
    }

    void test_pointer_cancel_clears_controls_and_blocks_late_release() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& button = ui.make<Button>("button");
        int activations = 0;
        button.clicked().connect([&](Button&) { activations += 1; });
        root.add_stretch_child(button);
        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});

        tc_ui_pointer_event event{};
        event.type = TC_UI_POINTER_DOWN;
        event.button = tcbase::mouse_button_value(tcbase::MouseButton::LEFT);
        event.x = 10.0f;
        event.y = 10.0f;
        assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_HANDLED);
        assert(button.pressed());

        tc_widget_set_enabled(button.c_widget(), false);
        assert(!button.pressed());
        assert(tc_widget_handle_is_invalid(document.pointer_capture()));
        assert(tc_widget_handle_is_invalid(document.pressed_widget()));

        tc_widget_set_enabled(button.c_widget(), true);
        event.type = TC_UI_POINTER_UP;
        assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_IGNORED);
        assert(activations == 0);

        tc_ui_document_destroy(document_handle);
    }

    void test_focus_and_key_text_dispatch_follow_focused_widget() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);

        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& focusable = ui.make<FocusProbe>();
        auto& panel = ui.make<Panel>("panel");
        root.add_preferred_child(focusable);
        root.add_stretch_child(panel);

        document.layout_roots(tc_ui_rect{0.0f, 0.0f, 160.0f, 40.0f});
        assert(focusable.focusable());
        assert(!panel.focusable());

        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = 10.0f;
        pointer.y = 10.0f;
        document.dispatch_pointer_event(pointer);
        assert(tc_widget_handle_eq(document.focused_widget(), focusable.handle()));

        tc_ui_key_event key{};
        key.type = TC_UI_KEY_DOWN;
        key.key = 65;
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
        assert(focusable.key_count == 1);
        assert(focusable.last_key == 65);

        tc_ui_text_event text{};
        text.text = "a";
        assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
        assert(focusable.text_count == 1);

        pointer.x = panel.bounds().x + 4.0f;
        document.dispatch_pointer_event(pointer);
        assert(tc_widget_handle_is_invalid(document.focused_widget()));
        assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
        assert(focusable.key_count == 1);

        tc_ui_document_destroy(document_handle);
    }

    void test_focus_api_rejects_non_focusable_and_clears_on_destroy() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
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

        tc_ui_document_destroy(document_handle);
    }

    void test_recursive_destroy_removes_container_children() {
        tc_ui_document_handle document_handle = tc_ui_document_create();
        TcDocument document(document_handle);
        DocumentBuilder ui(document);
        auto& root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
        auto& child = ui.make<Panel>("child");
        root.add_child(child);

        assert(tc_ui_document_live_widget_count(document.get()) == 2);
        assert(tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
        assert(tc_ui_document_live_widget_count(document.get()) == 0);
        assert(!tc_ui_document_is_alive(document.get(), root.handle()));
        assert(!tc_ui_document_is_alive(document.get(), child.handle()));

        tc_ui_document_destroy(document_handle);
    }

} // namespace termin_gui_native_test
