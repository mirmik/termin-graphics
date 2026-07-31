#include <termin/gui_native/tc_document.hpp>

#include "widgets_test_support.hpp"

namespace termin_gui_native_test {
namespace {

class WidthDependentWidget final : public NativeWidget {
public:
  std::vector<tc_ui_constraints> measurements;

  explicit WidthDependentWidget(const char *debug_name = nullptr)
      : NativeWidget(debug_name) {}

  tc_ui_size measure(tc_ui_document_handle,
                     tc_ui_constraints constraints) override {
    measurements.push_back(constraints);
    const float max_width =
        constraints.max_size.width > 0.0f
            ? constraints.max_size.width
            : 200.0f;
    const float width = std::max(
        constraints.min_size.width, std::min(200.0f, max_width));
    const float reflow_height =
        std::ceil(200.0f / std::max(1.0f, width)) * 10.0f;
    const float max_height =
        constraints.max_size.height > 0.0f
            ? constraints.max_size.height
            : reflow_height;
    return tc_ui_size{
        width,
        std::max(constraints.min_size.height,
                 std::min(reflow_height, max_height))};
  }
};

}  // namespace

void test_box_layout_sets_child_bounds_and_paints() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
  root.set_padding(EdgeInsets{4.0f, 6.0f, 4.0f, 6.0f})
      .set_spacing(2.0f)
      .set_background(Color{0.1f, 0.1f, 0.1f, 1.0f});

  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
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

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);

  /* Root fill + root clip pair + two panel fills. Panels are borderless by
     default; an outline must now be requested explicitly. */
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
  auto &root =
      ui.make_root<BoxLayout>(Orientation::Vertical, debug_name.c_str());
  debug_name = "mutated-debug";

  assert(root.debug_name());
  assert(std::strcmp(root.debug_name(), "initial-debug") == 0);
  assert(std::strcmp(tc_widget_debug_name(root.c_widget()), "initial-debug") ==
         0);

  root.set_stable_id("showcase.root");
  root.set_name("Root");
  root.set_debug_name("renamed-root");
  assert(std::strcmp(root.stable_id(), "showcase.root") == 0);
  assert(std::strcmp(root.name(), "Root") == 0);
  assert(std::strcmp(root.debug_name(), "renamed-root") == 0);
  assert(std::strcmp(tc_widget_stable_id(root.c_widget()), "showcase.root") ==
         0);
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  root.clear_dirty(TC_WIDGET_DIRTY_MASK);
  root.set_spacing(4.0f);
  assert(root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT));
  assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_STATE));

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});
  assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT));
  assert(root.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));

  root.clear_dirty(TC_WIDGET_DIRTY_MASK);
  root.set_background(Color{0.1f, 0.2f, 0.3f, 1.0f});
  assert(root.has_dirty_flags(TC_WIDGET_DIRTY_PAINT));
  assert(!root.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT));

  auto &button = ui.make<Button>("Run");
  button.clear_dirty(TC_WIDGET_DIRTY_MASK);
  button.set_text("Stop");
  assert(button.has_dirty_flags(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT));

  auto &slider = ui.make<Slider>(0.0f);
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &fixed = ui.make<Spacer>(tc_ui_size{10.0f, 12.0f});
  auto &preferred = ui.make<Spacer>(tc_ui_size{30.0f, 18.0f});
  auto &flex_one = ui.make<Spacer>(tc_ui_size{20.0f, 12.0f});
  auto &flex_two = ui.make<Spacer>(tc_ui_size{20.0f, 12.0f});

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

  auto &root = ui.make_root<VStack>("root-vstack");
  auto &row = ui.make<HStack>("row-hstack");
  auto &left = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
  auto &right = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
  auto &bottom = ui.make<Spacer>(tc_ui_size{30.0f, 8.0f});

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

  auto &grid = ui.make_root<GridLayout>("grid");
  grid.set_padding(EdgeInsets{2.0f, 3.0f, 4.0f, 5.0f}).set_spacing(10.0f, 6.0f);
  grid.add_column(LayoutPolicy::Fixed, 40.0f);
  grid.add_column(LayoutPolicy::Stretch);
  grid.add_column(LayoutPolicy::Flex, 2.0f);
  grid.add_row(LayoutPolicy::Preferred);
  grid.add_row(LayoutPolicy::Stretch);

  auto &fixed_cell = ui.make<Spacer>(tc_ui_size{30.0f, 20.0f});
  auto &spanning = ui.make<Spacer>(tc_ui_size{90.0f, 12.0f});
  auto &bottom = ui.make<Panel>("bottom");
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
  assert(
      tc_widget_handle_eq(document.hit_test(125.0f, 35.0f), bottom.handle()));

  tc_ui_document_destroy(document_handle);
}

void test_box_grid_and_scroll_remeasure_width_dependent_children() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &box = ui.make_root<BoxLayout>(Orientation::Vertical, "reflow-box");
  auto &box_child = ui.make<WidthDependentWidget>("box-child");
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

  auto &row = ui.make_root<BoxLayout>(Orientation::Horizontal, "reflow-row");
  row.set_cross_axis_alignment(CrossAxisAlignment::Start);
  auto &left = ui.make<WidthDependentWidget>("row-left");
  auto &right = ui.make<WidthDependentWidget>("row-right");
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

  auto &grid = ui.make_root<GridLayout>("reflow-grid");
  grid.add_column(LayoutPolicy::Stretch);
  grid.add_row(LayoutPolicy::Preferred);
  auto &grid_child = ui.make<WidthDependentWidget>("grid-child");
  grid.add_child(grid_child, 0, 0);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 50.0f, 100.0f});
  assert(near(grid_child.bounds().width, 50.0f));
  assert(near(grid_child.bounds().height, 40.0f));
  assert(grid_child.measurements.size() == 3);
  assert(near(grid_child.measurements.back().min_size.width, 50.0f));
  assert(near(grid_child.measurements.back().max_size.width, 50.0f));

  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), grid.handle()));

  auto &scroll = ui.make_root<ScrollArea>("reflow-scroll");
  scroll.set_scroll_axes(false, true);
  auto &scroll_child = ui.make<WidthDependentWidget>("scroll-child");
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

  auto &box = ui.make_root<BoxLayout>(Orientation::Vertical, "percent-box");
  box.set_cross_axis_alignment(CrossAxisAlignment::Start);
  auto &child = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
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
      document.get(),
      tc_ui_constraints{tc_ui_size{0.0f, 0.0f},
                        tc_ui_size{1000000.0f, 1000000.0f}});
  assert(near(intrinsic.width, 20.0f));

  assert(tc_ui_document_destroy_widget_recursive(document.get(), box.handle()));
  auto &grid = ui.make_root<GridLayout>("percent-grid");
  grid.add_column(LayoutPolicy::Stretch);
  grid.add_row(LayoutPolicy::Preferred);
  auto &grid_child = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
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

  auto &grid = ui.make_root<GridLayout>("grid");
  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
  grid.add_child(first, 0, 0);
  grid.add_child(second, 1, 1);

  assert(tc_ui_document_live_widget_count(document.get()) == 3);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), grid.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_group_box_lays_out_content_and_routes_hit_test() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &group = ui.make_root<GroupBox>("Settings");
  group.set_padding(EdgeInsets{8.0f, 6.0f, 10.0f, 12.0f});
  auto &content = ui.make<Panel>("content");
  group.set_content(content);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 180.0f, 120.0f});
  assert(near(content.bounds().x, 8.0f));
  assert(near(content.bounds().y, 36.0f));
  assert(near(content.bounds().width, 162.0f));
  assert(near(content.bounds().height, 72.0f));
  assert(
      tc_widget_handle_eq(document.hit_test(20.0f, 45.0f), content.handle()));
  assert(tc_widget_handle_eq(document.hit_test(20.0f, 12.0f), group.handle()));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
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

  auto &group = ui.make_root<GroupBox>("Settings");
  auto &content = ui.make<Panel>("content");
  group.set_content(content);

  assert(tc_ui_document_live_widget_count(document.get()) == 2);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), group.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_splitter_layout_drag_and_hit_test() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
  splitter.set_split_fraction(0.25f)
      .set_min_extents(20.0f, 20.0f)
      .set_divider_thickness(8.0f);
  auto &left = ui.make<Panel>("left");
  auto &right = ui.make<Panel>("right");
  splitter.set_first(left);
  splitter.set_second(right);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 208.0f, 80.0f});
  assert(near(left.bounds().width, 50.0f));
  assert(near(right.bounds().x, 58.0f));
  assert(near(right.bounds().width, 150.0f));
  assert(
      tc_widget_handle_eq(document.hit_test(54.0f, 10.0f), splitter.handle()));
  assert(tc_widget_handle_eq(document.hit_test(100.0f, 10.0f), right.handle()));

  tc_ui_pointer_event event{};
  event.type = TC_UI_POINTER_MOVE;
  event.x = 54.0f;
  event.y = 10.0f;
  assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_IGNORED);
  assert(tc_widget_handle_eq(document.hovered_widget(), splitter.handle()));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  const tc_ui_draw_command *divider_command = tc_ui_draw_list_command_at(
      draw_list, tc_ui_draw_list_command_count(draw_list) - 1);
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

  auto &splitter = ui.make_root<Splitter>(Orientation::Vertical, "splitter");
  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
  splitter.set_first(first);
  splitter.set_second(second);

  assert(tc_ui_document_live_widget_count(document.get()) == 3);
  assert(tc_ui_document_destroy_widget_recursive(document.get(),
                                                 splitter.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_scroll_area_lays_out_content_with_clip_and_scroll() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &content = ui.make<VStack>("scroll-content");
  auto &top = ui.make<Panel>("top");
  auto &bottom = ui.make<Panel>("bottom");
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
  assert(tc_widget_handle_eq(document.hit_test(10.0f, 70.0f),
                             tc_widget_handle_invalid()));

  scroll.set_scroll(0.0f, 40.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});
  assert(near(scroll.scroll_y(), 40.0f));
  assert(near(content.bounds().y, -40.0f));
  assert(tc_widget_handle_eq(document.hit_test(10.0f, 45.0f), bottom.handle()));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);

  assert(tc_ui_draw_list_command_count(draw_list) >= 4);
  const tc_ui_draw_command *first = tc_ui_draw_list_command_at(draw_list, 0);
  assert(first && first->type == TC_UI_DRAW_PUSH_CLIP);
  assert(near(first->rect.width, 100.0f));
  bool found_pop_clip = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list);
       ++index) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, index);
    found_pop_clip =
        found_pop_clip || (command && command->type == TC_UI_DRAW_POP_CLIP);
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

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &content = ui.make<VStack>("scroll-content");
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

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &content = ui.make<VStack>("scroll-content");
  auto &area = ui.make<TextArea>("short text");
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
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), scroll.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_scroll_area_programmatic_keyboard_thumb_and_focus_contract() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &content = ui.make<VStack>("content");
  auto &top = ui.make<Panel>("top");
  auto &bottom = ui.make<Button>("bottom");
  content.add_fixed_child(top, 80.0f);
  content.add_fixed_child(bottom, 30.0f);
  content.set_preferred_size(tc_ui_size{120.0f, 180.0f});
  scroll.set_content(content);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 60.0f});

  int change_count = 0;
  float observed_y = -1.0f;
  scroll.changed().connect(
      [&](ScrollArea &, float, float y) {
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
  assert(bottom.bounds().y + bottom.bounds().height <=
         scroll.bounds().y + scroll.bounds().height + 0.001f);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 300.0f});
  assert(near(scroll.scroll_y(), 0.0f));

  tc_ui_document_destroy(document_handle);
}

void test_nested_scroll_area_bubbles_wheel_at_inner_boundary() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &outer = ui.make_root<ScrollArea>("outer");
  auto &outer_content = ui.make<VStack>("outer-content");
  auto &inner = ui.make<ScrollArea>("inner");
  auto &inner_content = ui.make<Panel>("inner-content");
  auto &tail = ui.make<Panel>("tail");
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

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first = ui.make<Panel>("first-page");
  auto &second = ui.make<Panel>("second-page");
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

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);
  bool saw_body_clip = false;
  for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, i);
    if (command && command->type == TC_UI_DRAW_PUSH_CLIP &&
        near(command->rect.y, 32.0f)) {
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

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first = ui.make<Panel>("first-page");
  auto &second = ui.make<Panel>("second-page");
  tabs.add_page("First", first);
  tabs.add_page("Second", second);

  assert(tc_ui_document_live_widget_count(document.get()) == 3);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), tabs.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_tab_view_page_mutation_and_selection_signal() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first = ui.make<Panel>("first-page");
  auto &second = ui.make<Panel>("second-page");
  tabs.add_page("First", first);
  tabs.add_page("Second", second);
  std::vector<size_t> selected;
  tabs.selection_changed().connect(
      [&selected](TabView &, size_t index) { selected.push_back(index); });

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

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first_page =
      ui.make<BoxLayout>(Orientation::Vertical, "first-page");
  auto &second_page =
      ui.make<BoxLayout>(Orientation::Vertical, "second-page");
  auto &first_a = ui.make<FocusProbe>();
  auto &first_b = ui.make<FocusProbe>();
  auto &second_a = ui.make<FocusProbe>();
  auto &second_b = ui.make<FocusProbe>();
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

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first = ui.make<FocusProbe>();
  auto &second = ui.make<FocusProbe>();
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

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first = ui.make<FocusProbe>();
  auto &second = ui.make<FocusProbe>();
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &fixed = ui.make<Spacer>(tc_ui_size{10.0f, 12.0f});
  auto &preferred = ui.make<Spacer>(tc_ui_size{80.0f, 12.0f});
  auto &stretch_one = ui.make<Spacer>(tc_ui_size{100.0f, 12.0f});
  auto &stretch_two = ui.make<Spacer>(tc_ui_size{100.0f, 12.0f});

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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &capped = ui.make<Spacer>(tc_ui_size{50.0f, 12.0f});
  auto &uncapped = ui.make<Spacer>(tc_ui_size{50.0f, 12.0f});

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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &start = ui.make<Spacer>(tc_ui_size{20.0f, 10.0f});
  auto &center = ui.make<Spacer>(tc_ui_size{20.0f, 20.0f});
  auto &end = ui.make<Spacer>(tc_ui_size{20.0f, 30.0f});
  root.set_padding({5.0f, 4.0f, 5.0f, 6.0f})
      .set_spacing(2.0f)
      .set_cross_axis_alignment(CrossAxisAlignment::Center);
  root.add_child(start);
  root.add_child(center);
  root.add_child(end);
  assert(root.set_child_placement(
      start, LayoutPolicy::Fixed, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      CrossAxisAlignment::Start));
  assert(root.set_child_placement(
      center, LayoutPolicy::Preferred, 0.0f, 1.0f, 1.0f, 0.0f, 40.0f));
  assert(root.set_child_placement(
      end, LayoutPolicy::Flex, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f,
      CrossAxisAlignment::End));
  assert(!root.set_child_placement(
      end, LayoutPolicy::Fixed, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f));
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &first = ui.make<Spacer>(tc_ui_size{80.0f, 12.0f});
  auto &second = ui.make<Spacer>(tc_ui_size{60.0f, 12.0f});

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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
  root.add_stretch_child(first);
  root.add_stretch_child(second);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});

  assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), first.handle()));
  assert(
      tc_widget_handle_eq(document.hit_test(150.0f, 10.0f), second.handle()));
  assert(tc_widget_handle_is_invalid(document.hit_test(250.0f, 10.0f)));

  tc_ui_document_destroy(document_handle);
}

void test_document_hit_test_prefers_topmost_root() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &bottom = ui.make_root<Panel>("bottom-root");
  auto &top = ui.make<Panel>("top-root");
  assert(document.add_root(top));

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});

  assert(tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), top.handle()));
  assert(
      !tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), bottom.handle()));

  tc_ui_document_destroy(document_handle);
}

void test_box_layout_hit_test_skips_stale_child_handles() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &child = ui.make<Panel>("child");
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &probe = ui.make<CapturingProbe>();
  auto &panel = ui.make<Panel>("panel");
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &probe = ui.make<CapturingProbe>();
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &container =
      ui.make<BoxLayout>(Orientation::Horizontal, "container");
  auto &focusable = ui.make<FocusProbe>();
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &probe = ui.make<CapturingProbe>();
  auto &outside = ui.make<FocusProbe>();
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
  assert(probe.last_cancel_reason ==
         TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE);
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &button = ui.make<Button>("button");
  int activations = 0;
  button.clicked().connect([&](Button &) { activations += 1; });
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

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &focusable = ui.make<FocusProbe>();
  auto &panel = ui.make<Panel>("panel");
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

  auto &focusable = ui.make_root<FocusProbe>();
  auto &panel = ui.make<Panel>("panel");
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
  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &child = ui.make<Panel>("child");
  root.add_child(child);

  assert(tc_ui_document_live_widget_count(document.get()) == 2);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
  assert(!tc_ui_document_is_alive(document.get(), root.handle()));
  assert(!tc_ui_document_is_alive(document.get(), child.handle()));

  tc_ui_document_destroy(document_handle);
}

void test_controls_handle_pointer_events() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &checkbox = ui.make<Checkbox>(false);
  auto &slider = ui.make<Slider>(0.0f);
  root.add_child(checkbox);
  root.add_child(slider);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 40.0f});

  tc_ui_pointer_event checkbox_event{};
  checkbox_event.type = TC_UI_POINTER_DOWN;
  checkbox_event.x = checkbox.bounds().x + 4.0f;
  checkbox_event.y = checkbox.bounds().y + 4.0f;
  assert(document.dispatch_pointer_event(checkbox_event) ==
         TC_UI_EVENT_HANDLED);
  assert(!checkbox.checked());
  assert(tc_widget_handle_eq(document.pointer_capture(), checkbox.handle()));
  checkbox_event.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(checkbox_event) ==
         TC_UI_EVENT_HANDLED);
  assert(checkbox.checked());
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  tc_ui_pointer_event slider_event{};
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

  tc_ui_document_destroy(document_handle);
}

void test_separator_layout_and_paint_command() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
  root.set_padding(EdgeInsets{});
  auto &separator = ui.make<Separator>(Orientation::Horizontal);
  separator.set_thickness(3.0f);
  root.add_preferred_child(separator);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 20.0f});
  assert(near(separator.bounds().width, 120.0f));
  assert(near(separator.bounds().height, 3.0f));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);
  assert(tc_ui_draw_list_command_count(draw_list) >= 3);
  bool saw_fill = false;
  for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, i);
    if (command && command->type == TC_UI_DRAW_FILL_RECT &&
        near(command->rect.height, 3.0f)) {
      saw_fill = true;
    }
  }
  assert(saw_fill);
  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);

  tc_ui_document_destroy(document_handle);
}

void test_text_input_focus_text_edit_and_submit() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &input = ui.make<TextInput>("ab");
  root.add_preferred_child(input);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 40.0f});

  int changed = 0;
  int submitted = 0;
  std::string last_text;
  input.changed().connect(
      [&changed, &last_text](TextInput &, const std::string &text) {
        changed += 1;
        last_text = text;
      });
  input.submitted().connect(
      [&submitted](TextInput &, const std::string &) { submitted += 1; });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = input.bounds().x + 40.0f;
  pointer.y = input.bounds().y + 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.focused_widget(), input.handle()));

  tc_ui_text_event text{};
  text.text = "c";
  assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "abc");
  assert(input.caret() == 3);
  assert(changed == 1);
  assert(last_text == "abc");

  tc_ui_key_event key{};
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

  text.text = "\r\nb\nc";
  assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "a b c");
  assert(input.text().find('\r') == std::string::npos);
  assert(input.text().find('\n') == std::string::npos);
  assert(changed == 4);

  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(submitted == 1);

  input.set_text({});
  assert(changed == 5);
  input.set_placeholder("First placeholder");
  assert(input.placeholder() == "First placeholder");
  input.set_placeholder("Replacement placeholder");
  assert(input.placeholder() == "Replacement placeholder");

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  bool saw_placeholder = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
    const tc_ui_draw_command *command = tc_ui_draw_list_command_at(draw_list, index);
    if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
        std::strcmp(command->text, "Replacement placeholder") == 0) {
      saw_placeholder = true;
      break;
    }
  }
  assert(saw_placeholder);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  input.set_placeholder({});
  assert(input.placeholder().empty());

  tc_ui_document_destroy(document_handle);
}

void test_text_widgets_clip_text_paint() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<BoxLayout>(Orientation::Vertical, "root");
  auto &label =
      ui.make<Label>("Long label text that must stay inside its widget", 14.0f);
  auto &input =
      ui.make<TextInput>("Long input text that must stay inside the edit box");
  root.add_fixed_child(label, 20.0f);
  root.add_fixed_child(input, 34.0f);

  document.layout_roots(tc_ui_rect{10.0f, 20.0f, 180.0f, 80.0f});

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);

  bool saw_label_clip = false;
  bool saw_input_inner_clip = false;
  for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, i);
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
  assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) ==
         count_commands(draw_list, TC_UI_DRAW_POP_CLIP));

  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);

  tc_ui_document_destroy(document_handle);
}

void test_text_measurement_uses_proportional_metrics() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &narrow = ui.make<Label>("iii", 20.0f);
  auto &wide = ui.make<Label>("WWW", 20.0f);
  const tc_ui_constraints constraints{tc_ui_size{0.0f, 0.0f},
                                      tc_ui_size{1000.0f, 1000.0f}};

  const tc_ui_size narrow_size = narrow.measure(document.get(), constraints);
  const tc_ui_size wide_size = wide.measure(document.get(), constraints);
  assert(near(narrow_size.width, 15.0f));
  assert(near(wide_size.width, 54.0f));
  assert(wide_size.width > narrow_size.width * 3.0f);
  assert(near(narrow_size.height, 24.0f));

  tc_ui_document_destroy(document_handle);
}

void test_wrapped_label_and_wrap_layout_reflow() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);

  auto &label = ui.make<Label>("one two\nтри", 10.0f);
  label.set_wrap_mode(TextWrapMode::Word);
  const tc_ui_constraints narrow_constraints{
      tc_ui_size{0.0f, 0.0f}, tc_ui_size{25.0f, 200.0f}};
  const tc_ui_constraints wide_constraints{
      tc_ui_size{0.0f, 0.0f}, tc_ui_size{100.0f, 200.0f}};
  const tc_ui_size narrow =
      label.measure(document.get(), narrow_constraints);
  const tc_ui_size wide = label.measure(document.get(), wide_constraints);
  assert(near(narrow.width, 18.0f));
  assert(near(narrow.height, 36.0f));
  assert(near(wide.width, 35.0f));
  assert(near(wide.height, 24.0f));

  auto metrics =
      tc_ui_presentation_metrics_identity(tc_ui_size{200.0f, 200.0f});
  metrics.font_scale = 2.0f;
  assert(document.set_presentation_metrics(metrics));
  const tc_ui_size accessible =
      label.measure(document.get(), wide_constraints);
  assert(accessible.height > wide.height);
  assert(accessible.width <= wide_constraints.max_size.width);

  auto &ellipsis = ui.make<Label>("неразрывныйтокен", 10.0f);
  ellipsis.set_wrap_mode(TextWrapMode::Word)
      .set_overflow(TextOverflow::Ellipsis)
      .set_max_lines(1);
  assert(document.add_root(ellipsis));
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 40.0f, 30.0f});
  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  bool saw_ellipsis = false;
  for (size_t index = 0;
       index < tc_ui_draw_list_command_count(draw_list); ++index) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, index);
    if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
        std::string(command->text).find("\xE2\x80\xA6") !=
            std::string::npos) {
      saw_ellipsis = true;
    }
  }
  assert(saw_ellipsis);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  metrics.font_scale = 1.0f;
  assert(document.set_presentation_metrics(metrics));
  auto &flow =
      ui.make_root<WrapLayout>(Orientation::Horizontal, "flow");
  flow.set_spacing(5.0f).set_line_spacing(4.0f);
  auto &first = ui.make<IconButton>("1");
  auto &second = ui.make<IconButton>("2");
  auto &third = ui.make<IconButton>("3");
  first.set_preferred_size({30.0f, 20.0f});
  second.set_preferred_size({30.0f, 20.0f});
  third.set_preferred_size({30.0f, 20.0f});
  flow.add_child(first);
  flow.add_child(second);
  flow.add_child(third);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 70.0f, 100.0f});
  assert(near(first.bounds().y, second.bounds().y));
  assert(third.bounds().y > first.bounds().y);
  assert(document.set_focus(second));
  const tc_widget_handle focused = document.focused_widget();
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 34.0f, 100.0f});
  assert(second.bounds().y > first.bounds().y);
  assert(third.bounds().y > second.bounds().y);
  assert(tc_widget_handle_eq(document.focused_widget(), focused));

  flow.set_orientation(Orientation::Vertical);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 45.0f});
  assert(near(first.bounds().x, second.bounds().x));
  assert(third.bounds().x > first.bounds().x);
  assert(tc_widget_handle_eq(document.focused_widget(), focused));

  auto &scroll = ui.make_root<ScrollArea>();
  scroll.set_scroll_axes(false, true);
  auto &scroll_label =
      ui.make<Label>("one two three four five six", 10.0f);
  scroll_label.set_wrap_mode(TextWrapMode::Word);
  scroll.set_content(scroll_label);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 45.0f, 20.0f});
  const float narrow_content_height = scroll.content_size().height;
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 20.0f});
  assert(scroll.content_size().height < narrow_content_height);

  tc_ui_document_destroy(document_handle);
}

void test_text_input_edits_utf8_at_codepoint_boundaries() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  const std::string initial = "a\xc3\xa9\xf0\x9f\x99\x82"
                              "b";
  auto &input = ui.make_root<TextInput>(initial);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 40.0f});
  assert(input.caret() == 8);

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_LEFT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_IGNORED);
  assert(document.set_focus(input));
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.caret() == 7);

  key.key = TC_UI_KEY_BACKSPACE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "a\xc3\xa9"
                         "b");
  assert(input.caret() == 3);

  key.key = TC_UI_KEY_DELETE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "a\xc3\xa9");
  assert(input.caret() == 3);

  input.set_caret(2);
  assert(input.caret() == 1);
  tc_ui_text_event insert{};
  insert.text = "\xf0\x9f\x99\x82";
  assert(document.dispatch_text_event(insert) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");
  assert(input.caret() == 5);

  tc_ui_text_event invalid{};
  invalid.text = "\xc3(";
  assert(document.dispatch_text_event(invalid) == TC_UI_EVENT_IGNORED);
  assert(input.text() == "a\xf0\x9f\x99\x82\xc3\xa9");

  tc_ui_document_destroy(document_handle);
}

void test_text_input_scrolls_to_keep_caret_inside_clip() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &input = ui.make_root<TextInput>("WWWWWWWWWWWW");
  assert(document.set_focus(input));
  document.layout_roots(tc_ui_rect{20.0f, 30.0f, 80.0f, 34.0f});
  assert(input.scroll_x() > 0.0f);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);

  const float clip_left = input.bounds().x + 8.0f;
  const float clip_right = input.bounds().x + input.bounds().width - 8.0f;
  bool saw_shifted_text = false;
  bool saw_visible_caret = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list);
       ++index) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, index);
    if (!command) {
      continue;
    }
    if (command->type == TC_UI_DRAW_TEXT && command->p0.x < clip_left) {
      saw_shifted_text = true;
    }
    if (command->type == TC_UI_DRAW_LINE &&
        near(command->p0.x, command->p1.x) && command->p0.x >= clip_left &&
        command->p0.x <= clip_right) {
      saw_visible_caret = true;
    }
  }
  assert(saw_shifted_text);
  assert(saw_visible_caret);

  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);

  tc_ui_document_destroy(document_handle);
}

void test_text_input_utf8_selection_and_host_clipboard() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  TestClipboard clipboard;
  install_test_clipboard(document, clipboard);
  DocumentBuilder ui(document);
  auto &input = ui.make_root<TextInput>("a\xc3\xa9\xf0\x9f\x99\x82"
                                        "b");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 34.0f});
  assert(document.set_focus(input));

  input.select(1, 7);
  assert(input.has_selection());
  assert(input.selected_text() == "\xc3\xa9\xf0\x9f\x99\x82");

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.modifiers = TC_UI_MOD_CTRL;
  key.key = 'c';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(clipboard.text == "\xc3\xa9\xf0\x9f\x99\x82");

  key.key = 'x';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "ab");
  assert(input.caret() == 1);
  assert(!input.has_selection());

  key.key = 'v';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.text() == "a\xc3\xa9\xf0\x9f\x99\x82"
                         "b");
  assert(input.caret() == 7);

  key.modifiers = TC_UI_MOD_SHIFT;
  key.key = TC_UI_KEY_LEFT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.selected_text() == "\xf0\x9f\x99\x82");

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_ROUNDED_RECT) >= 1);
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) >= 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  input.set_caret(input.text().size());
  clipboard.text = "\r\nnext\nline";
  key.modifiers = TC_UI_MOD_CTRL;
  key.key = 'v';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(input.text().ends_with("b next line"));
  assert(input.text().find('\r') == std::string::npos);
  assert(input.text().find('\n') == std::string::npos);

  tc_ui_document_destroy(document_handle);
}

void test_text_area_multiline_utf8_editing_navigation_and_scroll() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  TestClipboard clipboard;
  install_test_clipboard(document, clipboard);
  DocumentBuilder ui(document);
  auto &area =
      ui.make_root<TextArea>("a\xc3\xa9\nWWWWWWWW\n\xf0\x9f\x99\x82z\nlast");
  document.layout_roots(tc_ui_rect{10.0f, 20.0f, 70.0f, 42.0f});
  assert(document.set_focus(area));
  int changed = 0;
  area.changed().connect([&changed](TextArea &, const std::string &) { ++changed; });
  assert(area.scroll_y() > 0.0f);
  area.set_caret(12);
  document.layout_roots(tc_ui_rect{10.0f, 20.0f, 70.0f, 42.0f});
  assert(area.scroll_x() > 0.0f);

  area.select(1, 13);
  assert(area.selected_text() == "\xc3\xa9\nWWWWWWWW\n");
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.modifiers = TC_UI_MOD_CTRL;
  key.key = 'c';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(clipboard.text == "\xc3\xa9\nWWWWWWWW\n");

  key.key = 'x';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(area.text() == "a\xf0\x9f\x99\x82z\nlast");
  assert(area.caret() == 1);

  key.key = 'v';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(area.text() == "a\xc3\xa9\nWWWWWWWW\n\xf0\x9f\x99\x82z\nlast");
  assert(area.caret() == 13);

  area.set_caret(area.text().size());
  key.modifiers = 0;
  key.key = TC_UI_KEY_UP_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(area.caret() == 18);
  key.modifiers = TC_UI_MOD_SHIFT;
  key.key = TC_UI_KEY_HOME;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(area.selected_text() == "\xf0\x9f\x99\x82z");

  tc_ui_text_event text{"Q"};
  assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
  assert(area.text() == "a\xc3\xa9\nWWWWWWWW\nQ\nlast");
  assert(changed == 3);

  area.set_text({});
  assert(changed == 4);
  area.set_placeholder("First placeholder");
  assert(area.placeholder() == "First placeholder");
  area.set_placeholder("Replacement placeholder");
  assert(area.placeholder() == "Replacement placeholder");

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  bool saw_placeholder = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
    const tc_ui_draw_command *command = tc_ui_draw_list_command_at(draw_list, index);
    if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
        std::strcmp(command->text, "Replacement placeholder") == 0) {
      saw_placeholder = true;
      break;
    }
  }
  assert(saw_placeholder);
  assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_POP_CLIP) == 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  area.set_placeholder({});
  assert(area.placeholder().empty());

  tc_ui_document_destroy(document_handle);
}

void test_spin_box_numeric_edit_buttons_and_keys() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  TestClipboard clipboard;
  install_test_clipboard(document, clipboard);
  DocumentBuilder ui(document);
  auto &spin = ui.make_root<SpinBox>(5.0f);
  spin.set_range(-10.0f, 10.0f);
  spin.set_step(0.5f);
  spin.set_decimals(1);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 34.0f});
  assert(document.set_focus(spin));
  int changes = 0;
  spin.changed().connect([&changes](SpinBox &, float) { ++changes; });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = spin.bounds().x + spin.bounds().width - 4.0f;
  pointer.y = spin.bounds().y + 4.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(near(spin.value(), 5.5f));

  pointer.x = spin.bounds().x + 18.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(spin.editing());
  assert(spin.caret() < spin.edit_text().size());

  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = spin.bounds().x + 34.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(spin.has_selection());
  assert(!spin.selected_text().empty());
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.modifiers = TC_UI_MOD_CTRL;
  key.key = 'a';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  key.key = 'c';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(clipboard.text == spin.edit_text());
  key.key = 'x';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(spin.edit_text().empty());
  clipboard.text = "6.5";
  key.key = 'v';
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(spin.edit_text() == "6.5");

  key.modifiers = 0;
  key.key = TC_UI_KEY_HOME;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  const size_t initial_length = spin.edit_text().size();
  key.key = TC_UI_KEY_DELETE;
  for (size_t index = 0; index < initial_length; ++index) {
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  }
  tc_ui_text_event text{"7.5"};
  assert(document.dispatch_text_event(text) == TC_UI_EVENT_HANDLED);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(!spin.editing());
  assert(near(spin.value(), 7.5f));
  assert(changes == 2);

  tc_ui_document_destroy(document_handle);
}

void test_slider_edit_owns_canonical_children_and_syncs_values() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &edit = ui.make_root<SliderEdit>(2.0f);
  assert(tc_widget_handle_is_invalid(edit.slider_handle()));
  assert(tc_widget_handle_is_invalid(edit.spin_box_handle()));
  edit.set_range(0.0f, 10.0f);
  edit.set_step(0.5f);
  edit.set_label("Exposure");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 52.0f});
  assert(edit.child_count() == 2);
  assert(tc_ui_document_is_alive(document.get(), edit.slider_handle()));
  assert(tc_ui_document_is_alive(document.get(), edit.spin_box_handle()));
  tc_widget *slider_widget =
      tc_ui_document_resolve_widget(document.get(), edit.slider_handle());
  tc_widget *spin_widget =
      tc_ui_document_resolve_widget(document.get(), edit.spin_box_handle());
  assert(tc_widget_bounds(slider_widget).width > 0.0f);
  assert(tc_widget_bounds(spin_widget).width > 0.0f);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);
  assert(count_commands(draw_list, TC_UI_DRAW_LINE) >= 2);
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) >= 3);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 4);
  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);

  const tc_ui_rect slider_bounds = tc_widget_bounds(slider_widget);
  assert(tc_widget_handle_eq(
      document.hit_test(
          slider_bounds.x + slider_bounds.width * 0.5f,
          slider_bounds.y + slider_bounds.height * 0.5f),
      edit.slider_handle()));
  const tc_ui_rect spin_bounds = tc_widget_bounds(spin_widget);
  assert(tc_widget_handle_eq(
      document.hit_test(
          spin_bounds.x + spin_bounds.width * 0.5f,
          spin_bounds.y + spin_bounds.height * 0.5f),
      edit.spin_box_handle()));

  int changes = 0;
  edit.changed().connect([&changes](SliderEdit &, float) { ++changes; });
  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = slider_bounds.x + slider_bounds.width - 1.0f;
  pointer.y = slider_bounds.y + slider_bounds.height * 0.5f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(near(edit.value(), 10.0f));
  assert(changes == 1);
  assert(near(static_cast<SpinBox *>(spin_widget->body)->value(), 10.0f));

  assert(tc_ui_document_set_focus(document.get(), edit.spin_box_handle()));
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(near(edit.value(), 9.5f));
  assert(changes == 2);

  auto *slider = static_cast<Slider *>(slider_widget->body);
  slider->set_value(7.0f);
  assert(near(edit.value(), 7.0f));
  assert(near(static_cast<SpinBox *>(spin_widget->body)->value(), 7.0f));
  assert(changes == 3);

  const tc_widget_handle slider_handle = edit.slider_handle();
  const tc_widget_handle spin_box_handle = edit.spin_box_handle();
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 52.0f});
  assert(edit.child_count() == 2);
  assert(tc_widget_handle_eq(edit.slider_handle(), slider_handle));
  assert(tc_widget_handle_eq(edit.spin_box_handle(), spin_box_handle));

  const tc_widget_handle root_handle = edit.handle();
  assert(tc_ui_document_destroy_widget_recursive(document.get(), root_handle));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_combo_box_overlay_selection_and_destruction() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &combo = ui.make_root<ComboBox>();
  combo.add_item("First");
  combo.add_item("Second");
  combo.add_item("Third");
  combo.add_item("Fourth");
  combo.add_item("Fifth");
  combo.add_item("Sixth");
  combo.add_item("Seventh");
  combo.add_item("Eighth");
  combo.add_item("Ninth");
  combo.add_item("Tenth");
  document.layout_roots(tc_ui_rect{10.0f, 10.0f, 180.0f, 34.0f});
  int changes = 0;
  combo.changed().connect(
      [&changes](ComboBox &, int, const std::string &) { ++changes; });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 20.0f;
  pointer.y = 20.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(combo.open());
  assert(tc_ui_document_overlay_count(document.get()) == 1);
  const tc_widget_handle popup_handle =
      tc_ui_document_overlay_at(document.get(), 0);
  const tc_widget *popup =
      tc_ui_document_resolve_widget_const(document.get(), popup_handle);
  assert(popup);
  pointer.type = TC_UI_POINTER_WHEEL;
  pointer.x = popup->bounds.x + 10.0f;
  pointer.y = popup->bounds.y + 10.0f;
  pointer.wheel_y = -1.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = popup->bounds.x + 10.0f;
  pointer.y = popup->bounds.y + 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(combo.selected_index() == 2);
  assert(combo.selected_text() == "Third");
  assert(changes == 1);
  assert(!combo.open());
  assert(tc_ui_document_overlay_count(document.get()) == 0);

  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), combo.handle()));
  assert(!tc_ui_document_is_alive(document.get(), popup_handle));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

void test_icon_image_and_canvas_media_contracts() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<VStack>("media-root");
  auto &icon = ui.make<IconButton>("I");
  auto &image = ui.make<ImageWidget>();
  auto &canvas = ui.make<Canvas>();
  image.set_texture(41, tc_ui_size{200.0f, 100.0f});
  assert(image.fit() == ImageFit::Contain);
  image.set_fit(ImageFit::Cover);
  assert(image.fit() == ImageFit::Cover);
  canvas.set_texture(42, tc_ui_size{100.0f, 50.0f});
  canvas.set_overlay_texture(43);
  assert(canvas.texture_sampling() == TC_UI_TEXTURE_SAMPLING_LINEAR);
  canvas.set_texture_sampling(TC_UI_TEXTURE_SAMPLING_NEAREST);
  assert(canvas.texture_sampling() == TC_UI_TEXTURE_SAMPLING_NEAREST);
  int custom_paints = 0;
  canvas.set_paint_callback(
      [&custom_paints](Canvas &, tc_ui_paint_context *context) {
        ++custom_paints;
        tc_ui_painter_draw_line(context, tc_ui_point{0.0f, 0.0f},
                                tc_ui_point{1.0f, 1.0f},
                                tc_ui_color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
      });
  root.add_fixed_child(icon, 28.0f);
  root.add_fixed_child(image, 80.0f);
  root.add_fixed_child(canvas, 120.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 240.0f, 240.0f});

  const tc_ui_point center{canvas.bounds().x + canvas.bounds().width * 0.5f,
                           canvas.bounds().y + canvas.bounds().height * 0.5f};
  canvas.fit_in_view();
  assert(canvas.fit_mode());
  const tc_ui_point image_center = canvas.widget_to_image(center);
  assert(near(image_center.x, 50.0f));
  assert(near(image_center.y, 25.0f));

  tc_ui_pointer_event wheel{};
  wheel.type = TC_UI_POINTER_WHEEL;
  wheel.x = center.x;
  wheel.y = center.y;
  wheel.wheel_y = 1.0f;
  const float old_zoom = canvas.zoom();
  assert(canvas.pointer_event(document.get(), &wheel) == TC_UI_EVENT_HANDLED);
  assert(canvas.zoom() > old_zoom);
  assert(!canvas.fit_mode());
  const tc_ui_point anchored = canvas.widget_to_image(center);
  assert(near(anchored.x, image_center.x));
  assert(near(anchored.y, image_center.y));

  canvas.fit_in_view();
  const float fitted_zoom = canvas.zoom();
  canvas.layout(document.get(), tc_ui_rect{canvas.bounds().x, canvas.bounds().y,
                                           canvas.bounds().width * 0.5f,
                                           canvas.bounds().height});
  assert(canvas.fit_mode());
  assert(canvas.zoom() < fitted_zoom);

  int clicks = 0;
  icon.clicked().connect([&clicks](IconButton &) { ++clicks; });
  tc_ui_pointer_event click{};
  click.type = TC_UI_POINTER_DOWN;
  click.x = icon.bounds().x + 4.0f;
  click.y = icon.bounds().y + 4.0f;
  assert(icon.pointer_event(document.get(), &click) == TC_UI_EVENT_HANDLED);
  click.type = TC_UI_POINTER_UP;
  assert(icon.pointer_event(document.get(), &click) == TC_UI_EVENT_HANDLED);
  assert(clicks == 1);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(custom_paints == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXTURE) == 3);
  size_t linear_textures = 0;
  size_t nearest_textures = 0;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
    const tc_ui_draw_command *command = tc_ui_draw_list_command_at(draw_list, index);
    if (!command || command->type != TC_UI_DRAW_TEXTURE) continue;
    linear_textures += command->texture_sampling == TC_UI_TEXTURE_SAMPLING_LINEAR;
    nearest_textures += command->texture_sampling == TC_UI_TEXTURE_SAMPLING_NEAREST;
  }
  assert(linear_textures == 1);
  assert(nearest_textures == 2);
  assert(count_commands(draw_list, TC_UI_DRAW_LINE) >= 1);
  canvas.clear_texture();
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);

  tc_ui_document_destroy(document_handle);
}

} // namespace termin_gui_native_test
