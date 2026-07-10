#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace termin::gui_native;

namespace {

bool near(float a, float b, float epsilon = 0.001f) {
  return std::fabs(a - b) <= epsilon;
}

size_t count_commands(const tc_ui_draw_list *draw_list,
                      tc_ui_draw_command_type type) {
  size_t count = 0;
  for (size_t i = 0; i < tc_ui_draw_list_command_count(draw_list); ++i) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, i);
    if (command && command->type == type) {
      count += 1;
    }
  }
  return count;
}

bool test_text_measure(void *, const char *text, size_t byte_length,
                       float font_size, tc_ui_text_metrics *out_metrics) {
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

void install_test_text_measurer(Document &document) {
  document.set_text_measurer(&test_text_measure, nullptr);
}

struct TestClipboard {
  std::string text;
};

const char *test_clipboard_get(void *user_data) {
  return static_cast<TestClipboard *>(user_data)->text.c_str();
}

bool test_clipboard_set(void *user_data, const char *text, size_t byte_length) {
  if (!user_data || (!text && byte_length > 0)) {
    return false;
  }
  static_cast<TestClipboard *>(user_data)->text.assign(text ? text : "",
                                                       byte_length);
  return true;
}

void install_test_clipboard(Document &document, TestClipboard &clipboard) {
  document.set_clipboard(&test_clipboard_get, &test_clipboard_set, &clipboard);
}

class CapturingProbe final : public NativeWidget {
public:
  CapturingProbe() : NativeWidget("CapturingProbe") {
    set_preferred_size(tc_ui_size{80.0f, 32.0f});
  }

  int down_count = 0;
  int move_count = 0;
  int up_count = 0;

  tc_ui_event_result pointer_event(tc_ui_document *document,
                                   const tc_ui_pointer_event *event) override {
    if (!event) {
      return TC_UI_EVENT_IGNORED;
    }
    const bool inside = event->x >= bounds().x && event->y >= bounds().y &&
                        event->x <= bounds().x + bounds().width &&
                        event->y <= bounds().y + bounds().height;
    const bool captured =
        tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
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
    set_preferred_size(tc_ui_size{80.0f, 32.0f});
  }

  int key_count = 0;
  int text_count = 0;
  int last_key = 0;

  tc_ui_event_result key_event(tc_ui_document *,
                               const tc_ui_key_event *event) override {
    if (!event) {
      return TC_UI_EVENT_IGNORED;
    }
    key_count += 1;
    last_key = event->key;
    return TC_UI_EVENT_HANDLED;
  }

  tc_ui_event_result text_event(tc_ui_document *,
                                const tc_ui_text_event *event) override {
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

  assert(tc_ui_draw_list_command_count(draw_list) >= 7);

  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);
}

void test_widget_metadata_is_owned_and_exposed() {
  Document document;
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
}

void test_dirty_flags_track_layout_paint_and_state_changes() {
  Document document;
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
}

void test_box_layout_child_policies_allocate_primary_axis() {
  Document document;
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
}

void test_hstack_vstack_wrappers_use_expected_orientation() {
  Document document;
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
  assert(near(bottom.bounds().y, row.bounds().height));
  assert(near(left.bounds().x, 0.0f));
  assert(near(right.bounds().x, left.bounds().width));
}

void test_grid_layout_tracks_spans_and_hit_test() {
  Document document;
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
}

void test_grid_layout_recursive_destroy_children() {
  Document document;
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
}

void test_group_box_lays_out_content_and_routes_hit_test() {
  Document document;
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
}

void test_group_box_recursive_destroy_content() {
  Document document;
  DocumentBuilder ui(document);

  auto &group = ui.make_root<GroupBox>("Settings");
  auto &content = ui.make<Panel>("content");
  group.set_content(content);

  assert(tc_ui_document_live_widget_count(document.get()) == 2);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), group.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_splitter_layout_drag_and_hit_test() {
  Document document;
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

  auto &splitter = ui.make_root<Splitter>(Orientation::Vertical, "splitter");
  auto &first = ui.make<Panel>("first");
  auto &second = ui.make<Panel>("second");
  splitter.set_first(first);
  splitter.set_second(second);

  assert(tc_ui_document_live_widget_count(document.get()) == 3);
  assert(tc_ui_document_destroy_widget_recursive(document.get(),
                                                 splitter.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_scroll_area_lays_out_content_with_clip_and_scroll() {
  Document document;
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
  assert(tc_widget_handle_eq(document.hit_test(10.0f, 50.0f), bottom.handle()));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(paint_context);

  assert(tc_ui_draw_list_command_count(draw_list) >= 4);
  const tc_ui_draw_command *first = tc_ui_draw_list_command_at(draw_list, 0);
  const tc_ui_draw_command *last = tc_ui_draw_list_command_at(
      draw_list, tc_ui_draw_list_command_count(draw_list) - 1);
  assert(first && first->type == TC_UI_DRAW_PUSH_CLIP);
  assert(near(first->rect.width, 100.0f));
  assert(last && last->type == TC_UI_DRAW_POP_CLIP);

  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);
}

void test_scroll_area_wheel_clamps_and_recursive_destroy_content() {
  Document document;
  DocumentBuilder ui(document);

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &content = ui.make<VStack>("scroll-content");
  auto &child = ui.make<Panel>("child");
  content.add_fixed_child(child, 180.0f);
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

  assert(tc_ui_document_live_widget_count(document.get()) == 3);
  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), scroll.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_tab_view_switches_selected_page_and_clips_paint() {
  Document document;
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
}

void test_tab_view_recursive_destroy_pages() {
  Document document;
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
}

void test_tab_view_page_mutation_and_selection_signal() {
  Document document;
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
}

void test_box_layout_shrinks_flexible_children_before_overflowing() {
  Document document;
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
}

void test_box_layout_respects_child_extent_limits() {
  Document document;
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
}

void test_box_layout_allows_preferred_overflow_when_no_child_can_shrink() {
  Document document;
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
}

void test_document_hit_test_returns_deepest_child() {
  Document document;
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
}

void test_document_hit_test_prefers_topmost_root() {
  Document document;
  DocumentBuilder ui(document);

  auto &bottom = ui.make_root<Panel>("bottom-root");
  auto &top = ui.make<Panel>("top-root");
  assert(document.add_root(top));

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 100.0f});

  assert(tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), top.handle()));
  assert(
      !tc_widget_handle_eq(document.hit_test(20.0f, 20.0f), bottom.handle()));
}

void test_box_layout_hit_test_skips_stale_child_handles() {
  Document document;
  DocumentBuilder ui(document);

  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &child = ui.make<Panel>("child");
  root.add_stretch_child(child);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 100.0f, 40.0f});
  tc_widget_handle child_handle = child.handle();
  assert(tc_ui_document_destroy_widget(document.get(), child_handle));

  assert(tc_widget_handle_eq(document.hit_test(10.0f, 10.0f), root.handle()));
}

void test_pointer_dispatch_updates_hovered_widget() {
  Document document;
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
}

void test_pointer_capture_routes_events_outside_bounds_until_release() {
  Document document;
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
}

void test_destroy_clears_hover_and_pointer_capture() {
  Document document;
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
}

void test_focus_and_key_text_dispatch_follow_focused_widget() {
  Document document;
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
}

void test_focus_api_rejects_non_focusable_and_clears_on_destroy() {
  Document document;
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
}

void test_recursive_destroy_removes_container_children() {
  Document document;
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
}

void test_controls_handle_pointer_events() {
  Document document;
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
}

void test_separator_layout_and_paint_command() {
  Document document;
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
}

void test_text_input_focus_text_edit_and_submit() {
  Document document;
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

  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(submitted == 1);
}

void test_text_widgets_clip_text_paint() {
  Document document;
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
}

void test_text_measurement_uses_proportional_metrics() {
  Document document;
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
}

void test_text_input_edits_utf8_at_codepoint_boundaries() {
  Document document;
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
}

void test_text_input_scrolls_to_keep_caret_inside_clip() {
  Document document;
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
}

void test_text_input_utf8_selection_and_host_clipboard() {
  Document document;
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
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) >= 2);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);
}

void test_text_area_multiline_utf8_editing_navigation_and_scroll() {
  Document document;
  install_test_text_measurer(document);
  TestClipboard clipboard;
  install_test_clipboard(document, clipboard);
  DocumentBuilder ui(document);
  auto &area =
      ui.make_root<TextArea>("a\xc3\xa9\nWWWWWWWW\n\xf0\x9f\x99\x82z\nlast");
  document.layout_roots(tc_ui_rect{10.0f, 20.0f, 70.0f, 42.0f});
  assert(document.set_focus(area));
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

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 2);
  assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_POP_CLIP) == 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);
}

void test_spin_box_numeric_edit_buttons_and_keys() {
  Document document;
  install_test_text_measurer(document);
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

  pointer.x = spin.bounds().x + 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(spin.editing());
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
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
}

void test_slider_edit_owns_canonical_children_and_syncs_values() {
  Document document;
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

  int changes = 0;
  edit.changed().connect([&changes](SliderEdit &, float) { ++changes; });
  tc_widget *slider_widget =
      tc_ui_document_resolve_widget(document.get(), edit.slider_handle());
  auto *slider = static_cast<Slider *>(slider_widget->body);
  slider->set_value(7.0f);
  assert(near(edit.value(), 7.0f));
  tc_widget *spin_widget =
      tc_ui_document_resolve_widget(document.get(), edit.spin_box_handle());
  assert(near(static_cast<SpinBox *>(spin_widget->body)->value(), 7.0f));
  assert(changes == 1);

  const tc_widget_handle root_handle = edit.handle();
  assert(tc_ui_document_destroy_widget_recursive(document.get(), root_handle));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_combo_box_overlay_selection_and_destruction() {
  Document document;
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
}

void test_icon_image_and_canvas_media_contracts() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<VStack>("media-root");
  auto &icon = ui.make<IconButton>("I");
  auto &image = ui.make<ImageWidget>();
  auto &canvas = ui.make<Canvas>();
  image.set_texture(41, tc_ui_size{200.0f, 100.0f});
  canvas.set_texture(42, tc_ui_size{100.0f, 50.0f});
  canvas.set_overlay_texture(43);
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
  const tc_ui_point anchored = canvas.widget_to_image(center);
  assert(near(anchored.x, image_center.x));
  assert(near(anchored.y, image_center.y));

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
  assert(count_commands(draw_list, TC_UI_DRAW_LINE) >= 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(
      tc_ui_document_destroy_widget_recursive(document.get(), root.handle()));
  assert(tc_ui_document_live_widget_count(document.get()) == 0);
}

void test_widget_signals_are_emitted_from_interactions() {
  Document document;
  DocumentBuilder ui(document);
  auto &root = ui.make_root<BoxLayout>(Orientation::Horizontal, "root");
  auto &button = ui.make<Button>("Run");
  auto &checkbox = ui.make<Checkbox>(false);
  auto &slider = ui.make<Slider>(0.0f);
  root.add_preferred_child(button);
  root.add_preferred_child(checkbox);
  root.add_stretch_child(slider);

  int clicked_a = 0;
  int clicked_b = 0;
  const size_t disconnected =
      button.clicked().connect([&clicked_a](Button &) { clicked_a += 1; });
  button.clicked().connect([&clicked_b](Button &) { clicked_b += 1; });
  assert(disconnected != 0);
  assert(button.clicked().disconnect(disconnected));

  int checkbox_changes = 0;
  bool last_checked = false;
  checkbox.changed().connect(
      [&checkbox_changes, &last_checked](Checkbox &, bool checked) {
        checkbox_changes += 1;
        last_checked = checked;
      });

  int slider_changes = 0;
  float last_slider_value = 0.0f;
  slider.changed().connect(
      [&slider_changes, &last_slider_value](Slider &, float value) {
        slider_changes += 1;
        last_slider_value = value;
      });

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 260.0f, 40.0f});

  tc_ui_pointer_event event{};
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

  auto &first_box = ui.make_root<HStack>("first-box");
  auto &second_box = ui.make_root<HStack>("second-box");
  auto &moving_panel = ui.make<Panel>("moving-panel");
  first_box.add_child(moving_panel);
  assert(first_box.child_count() == 1);
  assert(first_box.child_at(0) == moving_panel.c_widget());
  assert(moving_panel.parent_widget() == first_box.c_widget());

  second_box.add_child(moving_panel);
  assert(first_box.child_count() == 0);
  assert(second_box.child_count() == 1);
  assert(moving_panel.parent_widget() == second_box.c_widget());
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 60.0f});
  assert(near(moving_panel.bounds().width, 200.0f));

  auto &grid = ui.make_root<GridLayout>("grid");
  auto &grid_child = ui.make<Panel>("grid-child");
  grid.add_child(grid_child, 0, 0);
  assert(grid.child_count() == 1);
  assert(grid_child.parent_widget() == grid.c_widget());

  auto &group = ui.make_root<GroupBox>("group");
  auto &group_first = ui.make<Panel>("group-first");
  auto &group_second = ui.make<Panel>("group-second");
  group.set_content(group_first);
  group.set_content(group_second);
  assert(group.child_count() == 1);
  assert(group.child_at(0) == group_second.c_widget());
  assert(group_first.parent_widget() == nullptr);
  assert(group_second.parent_widget() == group.c_widget());

  auto &splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
  auto &split_first = ui.make<Panel>("split-first");
  auto &split_second = ui.make<Panel>("split-second");
  auto &split_replacement = ui.make<Panel>("split-replacement");
  splitter.set_first(split_first);
  splitter.set_second(split_second);
  splitter.set_first(split_replacement);
  assert(splitter.child_count() == 2);
  assert(splitter.child_at(0) == split_replacement.c_widget());
  assert(splitter.child_at(1) == split_second.c_widget());
  assert(split_first.parent_widget() == nullptr);

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &scroll_first = ui.make<Panel>("scroll-first");
  auto &scroll_second = ui.make<Panel>("scroll-second");
  scroll.set_content(scroll_first);
  scroll.set_content(scroll_second);
  assert(scroll.child_count() == 1);
  assert(scroll.child_at(0) == scroll_second.c_widget());
  assert(scroll_first.parent_widget() == nullptr);

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &tab_first = ui.make<Panel>("tab-first");
  auto &tab_second = ui.make<Panel>("tab-second");
  tabs.add_page("First", tab_first);
  tabs.add_page("Second", tab_second);
  assert(tabs.child_count() == 2);
  assert(tabs.child_at(0) == tab_first.c_widget());
  assert(tabs.child_at(1) == tab_second.c_widget());
}

void test_common_visibility_enabled_and_mouse_transparent_state() {
  Document document;
  DocumentBuilder ui(document);
  auto &root = ui.make_root<HStack>("root");
  auto &hidden = ui.make<Panel>("hidden");
  auto &button = ui.make<Button>("button");
  hidden.set_preferred_size(tc_ui_size{40.0f, 30.0f});
  root.add_preferred_child(hidden);
  root.add_stretch_child(button);

  hidden.set_visible(false);
  button.set_enabled(false);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 160.0f, 40.0f});
  assert(near(button.bounds().x, 0.0f));
  assert(near(button.bounds().width, 160.0f));

  tc_widget_handle hit = document.hit_test(20.0f, 20.0f);
  assert(tc_widget_handle_eq(hit, root.handle()));
  tc_ui_pointer_event event{};
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

void test_cpp_theme_style_facade_inheritance_and_state() {
  Document document;
  DocumentBuilder ui(document);
  auto &root = ui.make_root<HStack>("style-root");
  auto &button = ui.make<Button>("Styled");
  root.add_child(button);
  assert(button.style_role() == TC_UI_STYLE_BUTTON);

  tc_ui_style_override inherited{};
  inherited.fields = TC_UI_STYLE_FONT_SIZE | TC_UI_STYLE_FOREGROUND;
  inherited.flags = TC_UI_STYLE_OVERRIDE_INHERIT;
  inherited.value.font_size = 18.0f;
  inherited.value.foreground = tc_ui_color{0.8f, 0.7f, 0.6f, 1.0f};
  assert(root.set_style_override(inherited));
  tc_ui_style style = document.resolve_style(button);
  assert(near(style.font_size, 18.0f));
  assert(near(style.foreground.g, 0.7f));

  button.set_enabled(false);
  style = document.resolve_style(button);
  assert(near(
      style.background.r,
      document.theme().roles[TC_UI_STYLE_BUTTON].disabled.value.background.r));

  tc_ui_theme theme = document.theme();
  theme.roles[TC_UI_STYLE_BUTTON].base.accent =
      tc_ui_color{0.91f, 0.2f, 0.1f, 1.0f};
  const uint64_t revision = document.theme_revision();
  button.clear_dirty(TC_WIDGET_DIRTY_MASK);
  assert(document.set_theme(theme));
  assert(document.theme_revision() == revision + 1);
  assert(button.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT |
                                TC_WIDGET_DIRTY_STATE));
  assert(near(document.resolve_style(button).accent.r, 0.91f));
}

void test_collection_and_selection_models_are_reusable() {
  CollectionModel model;
  model.set_items({
      CollectionItem{"a", "Alpha", "First", true},
      CollectionItem{"b", "Beta", "Second", true},
      CollectionItem{"c", "Gamma", "Third", true},
      CollectionItem{"d", "Delta", "Fourth", true},
  });
  const uint64_t revision = model.revision();
  model.update(1, CollectionItem{"b", "Beta 2", "Updated", true});
  assert(model.revision() == revision + 1);
  assert(model.item(1).text == "Beta 2");

  SelectionModel selection(SelectionMode::Multiple);
  assert(selection.select_only(1));
  assert(selection.extend_to(3));
  assert((selection.selected_indices() == std::vector<size_t>{1, 2, 3}));
  assert(selection.toggle(2));
  assert((selection.selected_indices() == std::vector<size_t>{1, 3}));
  assert(selection.select_all(model.size()));
  assert(selection.selected_indices().size() == 4);
  model.erase(3);
  assert(selection.items_erased(3, 1, model.size()));
  assert((selection.selected_indices() == std::vector<size_t>{0, 1, 2}));

  assert(selection.select_only(2));
  assert(selection.items_inserted(1, 2));
  assert((selection.selected_indices() == std::vector<size_t>{4}));

  bool rejected = false;
  try {
    model.append(CollectionItem{"bad", std::string("\xff", 1), {}, true});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void test_list_widget_virtualizes_large_models_and_reconciles_selection() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  std::vector<CollectionItem> items;
  items.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    items.push_back(CollectionItem{
        "item-" + std::to_string(index),
        "Item " + std::to_string(index),
        index % 2 == 0 ? "Even" : "Odd",
        true,
    });
  }
  model->set_items(std::move(items));
  auto &list = ui.make_root<ListWidget>(model);
  list.set_row_height(40.0f);
  list.set_row_spacing(2.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 126.0f});

  const auto [first, last] = list.visible_range();
  assert(first == 0);
  assert(last <= 5);
  assert(list.content_height() > 400000.0f);
  assert(list.child_count() == 0);

  tc_ui_pointer_event wheel{};
  wheel.type = TC_UI_POINTER_WHEEL;
  wheel.x = 10.0f;
  wheel.y = 10.0f;
  wheel.wheel_y = 1.0f;
  assert(list.pointer_event(document.get(), &wheel) == TC_UI_EVENT_IGNORED);
  wheel.wheel_y = -1.0f;
  assert(list.pointer_event(document.get(), &wheel) == TC_UI_EVENT_HANDLED);
  assert(list.scroll_y() > 0.0f);
  list.set_scroll_y(0.0f);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 10);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 6);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  list.selection().select_only(9999);
  list.ensure_visible(9999);
  assert(list.scroll_y() > 400000.0f);
  const auto [scrolled_first, scrolled_last] = list.visible_range();
  assert(scrolled_first > 9990);
  assert(scrolled_last == 10000);

  model->set_items({CollectionItem{"only", "Only", {}, true}});
  list.layout(document.get(), list.bounds());
  assert(list.selection().selected_indices().empty());
  assert(near(list.scroll_y(), 0.0f));
}

void test_list_widget_pointer_keyboard_and_multi_selection() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
      CollectionItem{"2", "Disabled", {}, false},
      CollectionItem{"3", "Three", {}, true},
      CollectionItem{"4", "Four", {}, true},
      CollectionItem{"5", "Five", {}, true},
  });
  auto &list = ui.make_root<ListWidget>(model);
  list.set_selection_mode(SelectionMode::Multiple);
  list.set_row_height(30.0f);
  list.set_row_spacing(0.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 240.0f, 90.0f});

  std::vector<std::vector<size_t>> changes;
  list.selection_changed().connect(
      [&changes](ListWidget &, const std::vector<size_t> &selected) {
        changes.push_back(selected);
      });
  size_t activated = SelectionModel::npos;
  list.activated().connect(
      [&activated](ListWidget &, size_t index, const CollectionItem &) {
        activated = index;
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 10.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{0}));

  pointer.y = 45.0f;
  pointer.modifiers = TC_UI_MOD_CTRL;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{0, 1}));

  list.set_scroll_y(60.0f);
  pointer.y = 75.0f;
  pointer.modifiers = TC_UI_MOD_SHIFT;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(
      (list.selection().selected_indices() == std::vector<size_t>{1, 2, 3, 4}));

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_UP_ARROW;
  key.modifiers = 0;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{3}));
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == 3);
  key.key = TC_UI_KEY_A;
  key.modifiers = TC_UI_MOD_CTRL;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(list.selection().selected_indices().size() == model->size() - 1);
  assert(changes.size() >= 5);
}

void test_list_widget_model_notifications_preserve_lifetime_and_shift_selection() {
  Document document;
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
      CollectionItem{"2", "Two", {}, true},
  });
  std::weak_ptr<CollectionModel> weak_model = model;
  auto &list = ui.make_root<ListWidget>(model);
  assert(list.select_index(2));
  model->erase(0);
  assert((list.selection().selected_indices() == std::vector<size_t>{1}));
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = list.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<CollectionModel>();
  destroying_model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
  });
  auto &destroying_list = ui.make_root<ListWidget>(destroying_model);
  assert(destroying_list.select_index(1));
  const tc_widget_handle destroying_handle = destroying_list.handle();
  destroying_list.selection_changed().connect([&document, destroying_handle](
                                                  ListWidget &,
                                                  const std::vector<size_t> &) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(0);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));
}

void test_tree_model_stable_ids_move_and_expansion_reconcile() {
  TreeModel model;
  const TreeNodeId root_a =
      model.append_root(CollectionItem{"a", "A", {}, true});
  const TreeNodeId root_b =
      model.append_root(CollectionItem{"b", "B", {}, true});
  const TreeNodeId child =
      model.append_child(root_a, CollectionItem{"child", "Child", {}, true});
  const TreeNodeId grandchild = model.append_child(
      child, CollectionItem{"grandchild", "Grandchild", {}, true});
  assert(model.size() == 4);
  assert(model.node(grandchild).parent == child);
  assert((model.children(root_a) == std::vector<TreeNodeId>{child}));

  model.move(child, root_b);
  assert(model.children(root_a).empty());
  assert((model.children(root_b) == std::vector<TreeNodeId>{child}));
  assert(model.node(child).parent == root_b);

  bool rejected_cycle = false;
  try {
    model.move(root_b, grandchild);
  } catch (const std::invalid_argument &) {
    rejected_cycle = true;
  }
  assert(rejected_cycle);

  TreeExpansionModel expansion;
  assert(expansion.set_expanded(root_b, true));
  assert(expansion.set_expanded(child, true));
  model.erase(child);
  assert(!model.contains(child));
  assert(!model.contains(grandchild));
  assert(expansion.reconcile(model));
  assert(expansion.expanded(root_b));
  assert(!expansion.expanded(child));

  bool rejected_utf8 = false;
  try {
    model.append_root(CollectionItem{"bad", std::string("\xff", 1), {}, true});
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);
}

void test_tree_widget_virtualizes_large_expanded_model() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  auto expansion = std::make_shared<TreeExpansionModel>();
  for (size_t root_index = 0; root_index < 100; ++root_index) {
    const TreeNodeId root =
        model->append_root(CollectionItem{"root-" + std::to_string(root_index),
                                          "Root " + std::to_string(root_index),
                                          {},
                                          true});
    expansion->set_expanded(root, true);
    for (size_t child_index = 0; child_index < 100; ++child_index) {
      model->append_child(root,
                          CollectionItem{"node-" + std::to_string(root_index) +
                                             "-" + std::to_string(child_index),
                                         "Node " + std::to_string(child_index),
                                         {},
                                         true});
    }
  }
  auto &tree = ui.make_root<TreeWidget>(model, expansion);
  tree.set_row_height(24.0f);
  tree.set_row_spacing(1.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 100.0f});
  assert(model->size() == 10100);
  assert(tree.visible_count() == 10100);
  assert(tree.child_count() == 0);
  assert(tree.content_height() > 250000.0f);
  const auto [first, last] = tree.visible_range();
  assert(first == 0);
  assert(last <= 6);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 12);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  const TreeNodeId last_root = model->roots().back();
  const TreeNodeId last_child = model->children(last_root).back();
  assert(tree.select_node(last_child));
  assert(tree.scroll_y() > 250000.0f);
  assert(tree.visible_range().first > 10000);
}

void test_tree_widget_pointer_keyboard_signals_and_lifetime() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  const TreeNodeId root =
      model->append_root(CollectionItem{"root", "Root", {}, true});
  const TreeNodeId first =
      model->append_child(root, CollectionItem{"first", "First", {}, true});
  const TreeNodeId disabled = model->append_child(
      root, CollectionItem{"disabled", "Disabled", {}, false});
  const TreeNodeId last =
      model->append_child(root, CollectionItem{"last", "Last", {}, true});
  std::weak_ptr<TreeModel> weak_model = model;
  auto &tree = ui.make_root<TreeWidget>(model);
  tree.set_row_height(30.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 90.0f});

  std::vector<TreeNodeId> selections;
  std::vector<std::pair<TreeNodeId, bool>> expansions;
  TreeNodeId activated = kInvalidTreeNodeId;
  TreeNodeId delete_requested = kInvalidTreeNodeId;
  TreeNodeId context_requested = kInvalidTreeNodeId;
  tree.selection_changed().connect(
      [&selections](TreeWidget &, TreeNodeId node) {
        selections.push_back(node);
      });
  tree.expansion_changed().connect(
      [&expansions](TreeWidget &, TreeNodeId node, bool value) {
        expansions.emplace_back(node, value);
      });
  tree.activated().connect(
      [&activated](TreeWidget &, TreeNodeId node, const CollectionItem &) {
        activated = node;
      });
  tree.delete_requested().connect(
      [&delete_requested](TreeWidget &, TreeNodeId node,
                          const CollectionItem &) { delete_requested = node; });
  tree.context_menu_requested().connect(
      [&context_requested](TreeWidget &, TreeNodeId node, float, float) {
        context_requested = node;
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 5.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.expanded(root));
  assert(expansions.back() == std::make_pair(root, true));

  pointer.x = 40.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == root);

  pointer.button = 1;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(context_requested == root);
  pointer.button = 0;

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == first);
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == last);
  assert(tree.selected_node() != disabled);
  key.key = TC_UI_KEY_LEFT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == root);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == first);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == first);
  key.key = TC_UI_KEY_DELETE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(delete_requested == first);
  assert(!selections.empty());

  model->erase(first);
  assert(tree.selected_node() == kInvalidTreeNodeId);
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = tree.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<TreeModel>();
  const TreeNodeId destroying_root = destroying_model->append_root(
      CollectionItem{"destroying-root", "Root", {}, true});
  const TreeNodeId destroying_child = destroying_model->append_child(
      destroying_root, CollectionItem{"destroying-child", "Child", {}, true});
  auto &destroying_tree = ui.make_root<TreeWidget>(destroying_model);
  assert(destroying_tree.select_node(destroying_child));
  const tc_widget_handle destroying_handle = destroying_tree.handle();
  destroying_tree.selection_changed().connect([&document, destroying_handle](
                                                  TreeWidget &, TreeNodeId) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(destroying_child);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));
}

void test_tree_widget_drag_drop_positions_and_capture() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  const TreeNodeId first =
      model->append_root(CollectionItem{"first", "First", {}, true});
  const TreeNodeId second =
      model->append_root(CollectionItem{"second", "Second", {}, true});
  auto &tree = ui.make_root<TreeWidget>(model);
  tree.set_row_height(30.0f);
  tree.set_draggable(true);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 90.0f});

  TreeNodeId dropped = kInvalidTreeNodeId;
  TreeNodeId target = kInvalidTreeNodeId;
  TreeDropPosition position = TreeDropPosition::Root;
  tree.drop_requested().connect([&](TreeWidget &, TreeNodeId dragged,
                                    TreeNodeId drop_target,
                                    TreeDropPosition drop_position) {
    dropped = dragged;
    target = drop_target;
    position = drop_position;
  });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = 40.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), tree.handle()));

  pointer.type = TC_UI_POINTER_MOVE;
  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.dragging());

  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));
  assert(!tree.dragging());
  assert(dropped == first);
  assert(target == second);
  assert(position == TreeDropPosition::Inside);
}

void test_table_models_preserve_row_ids_and_validate_columns() {
  TableModel model;
  const TableRowId first =
      model.append(TableRowData{"first", {"First", "1"}, true});
  const TableRowId last =
      model.append(TableRowData{"last", {"Last", "2"}, true});
  const TableRowId middle =
      model.insert(1, TableRowData{"middle", {"Middle", "3"}, false});
  assert(model.size() == 3);
  assert(model.index_of(first) == 0);
  assert(model.index_of(middle) == 1);
  assert(model.index_of(last) == 2);

  model.update(last, TableRowData{"last", {"Updated", "4"}, true});
  assert(model.row(last).data.cells.front() == "Updated");
  model.erase(middle);
  assert(!model.contains(middle));
  assert(model.index_of(last) == 1);

  bool rejected_utf8 = false;
  try {
    model.append(TableRowData{"bad", {std::string("\xff", 1)}, true});
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);

  TableColumnModel columns;
  columns.set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 80.0f, 50.0f,
                  120.0f, 1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
  });
  assert(columns.resize(0, 10.0f) == 50.0f);
  assert(columns.resize(0, 200.0f) == 120.0f);
  assert(columns.column(0).policy == TableColumnPolicy::Fixed);

  bool rejected_duplicate = false;
  try {
    columns.append(TableColumn{"name", "Duplicate", TableColumnPolicy::Stretch,
                               0.0f, 40.0f, 0.0f, 1.0f, true});
  } catch (const std::invalid_argument &) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);
}

void test_file_grid_widget_virtualizes_large_model_and_responsive_layout() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  std::vector<CollectionItem> items;
  items.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    items.push_back(CollectionItem{"file-" + std::to_string(index),
                                   index == 0
                                       ? "A very long UTF-8 filename пример.txt"
                                       : "File " + std::to_string(index),
                                   ".txt", true, index == 0 ? 77u : 0u});
  }
  model->set_items(std::move(items));
  auto &grid = ui.make_root<FileGridWidget>(model);
  grid.set_tile_size(50.0f, 60.0f);
  grid.set_tile_spacing(4.0f);
  grid.set_padding(4.0f);
  grid.set_icon_size(20.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 128.0f});

  assert(grid.child_count() == 0);
  assert(grid.column_count() == 4);
  assert(grid.row_count() == 2500);
  assert(grid.content_height() > 150000.0f);
  assert(grid.has_scrollbar());
  const auto [first, last] = grid.visible_range();
  assert(first == 0);
  assert(last <= 16);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 32);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXTURE) == 1);
  bool found_elided_name = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list);
       ++index) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, index);
    if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
        std::string(command->text).find("...") != std::string::npos) {
      found_elided_name = true;
    }
  }
  assert(found_elided_name);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(grid.select_index(9999));
  assert(grid.scroll_y() > 150000.0f);
  assert(grid.visible_range().first > 9980);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 112.0f, 128.0f});
  assert(grid.column_count() == 2);
  assert(grid.row_count() == 5000);
}

CommandData test_command(std::string id, std::string label, bool enabled = true,
                         bool checkable = false, bool checked = false) {
  return CommandData{
      std::move(id), std::move(label), {},      {}, {}, CommandKind::Action,
      enabled,       checkable,        checked, 0,  {}};
}

void test_command_model_stable_ids_validation_and_mutation() {
  CommandModel model;
  const CommandId first = model.append(test_command("first", "First"));
  const CommandId checked =
      model.append(test_command("checked", "Checked", true, true));
  model.insert(1, CommandData{"separator",
                              {},
                              {},
                              {},
                              {},
                              CommandKind::Separator,
                              true,
                              false,
                              false,
                              0,
                              {}});
  assert(model.size() == 3);
  assert(model.index_of(first) == 0);
  assert(model.index_of(checked) == 2);
  model.set_checked(checked, true);
  assert(model.command(checked).data.checked);
  model.set_enabled(first, false);
  assert(!model.command(first).data.enabled);

  bool rejected_duplicate = false;
  try {
    model.append(test_command("first", "Duplicate"));
  } catch (const std::invalid_argument &) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);

  bool rejected_checked = false;
  try {
    model.append(test_command("invalid-checked", "Invalid", true, false, true));
  } catch (const std::invalid_argument &) {
    rejected_checked = true;
  }
  assert(rejected_checked);

  model.erase(first);
  assert(!model.contains(first));
  assert(model.index_of(checked) == 1);
}

void test_tool_bar_layout_activation_capture_and_model_lifetime() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CommandModel>();
  const CommandId save = model->append(CommandData{"save",
                                                   "Save",
                                                   "S",
                                                   "Ctrl+S",
                                                   "Save scene",
                                                   CommandKind::Action,
                                                   true,
                                                   false,
                                                   false,
                                                   0,
                                                   {}});
  model->append(CommandData{"separator",
                            {},
                            {},
                            {},
                            {},
                            CommandKind::Separator,
                            true,
                            false,
                            false,
                            0,
                            {}});
  const CommandId snap = model->append(CommandData{"snap",
                                                   "Snap",
                                                   {},
                                                   {},
                                                   "Toggle snap",
                                                   CommandKind::Action,
                                                   true,
                                                   true,
                                                   false,
                                                   0,
                                                   {}});
  model->append(test_command("disabled", "Disabled", false));
  std::weak_ptr<CommandModel> weak_model = model;
  auto &toolbar = ui.make_root<ToolBar>(model);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 40.0f});
  assert(toolbar.item_rects().size() == 4);
  assert(toolbar.item_rects()[0].width > toolbar.item_height());
  assert(toolbar.item_rects()[1].width < toolbar.item_height());

  std::vector<CommandId> activated;
  toolbar.activated().connect(
      [&activated](ToolBar &, size_t, CommandId id, const CommandData &) {
        activated.push_back(id);
      });
  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = toolbar.item_rects()[0].x + 2.0f;
  pointer.y = 20.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(toolbar.hovered_tooltip() == "Save scene");
  pointer.type = TC_UI_POINTER_DOWN;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), toolbar.handle()));
  pointer.type = TC_UI_POINTER_UP;
  pointer.x = 500.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(activated.empty());
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = toolbar.item_rects()[2].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(activated == std::vector<CommandId>{snap});
  assert(model->command(snap).data.checked);

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = toolbar.item_rects()[3].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  assert(activated.size() == 1);

  model->set_enabled(save, false);
  assert(!model->command(save).data.enabled);
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = toolbar.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());
}

void test_status_bar_explicit_message_lifecycle_and_utf8_validation() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &status = ui.make_root<StatusBar>("Ready");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 24.0f});
  assert(status.displayed_text() == "Ready");
  status.show_message("Saved ✓");
  assert(status.has_message());
  assert(status.displayed_text() == "Saved ✓");
  status.set_text("Idle");
  assert(status.displayed_text() == "Saved ✓");
  status.clear_message();
  assert(status.displayed_text() == "Idle");

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_LINE) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) == 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  bool rejected_utf8 = false;
  try {
    status.show_message(std::string("\xff", 1));
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);
}

void test_menu_overlay_navigation_submenus_scrolling_and_dismissal() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto recent = std::make_shared<CommandModel>();
  const CommandId recent_scene = recent->append(CommandData{"recent-scene",
                                                            "Scene.termin",
                                                            {},
                                                            {},
                                                            {},
                                                            CommandKind::Action,
                                                            true,
                                                            false,
                                                            false,
                                                            0,
                                                            {}});
  auto model = std::make_shared<CommandModel>();
  model->append(test_command("disabled", "Disabled", false));
  model->append(CommandData{"separator",
                            {},
                            {},
                            {},
                            {},
                            CommandKind::Separator,
                            true,
                            false,
                            false,
                            0,
                            {}});
  model->append(CommandData{"recent",
                            "Recent",
                            {},
                            {},
                            {},
                            CommandKind::Action,
                            true,
                            false,
                            false,
                            0,
                            recent});
  model->append(CommandData{"snap",
                            "Snap",
                            {},
                            {},
                            {},
                            CommandKind::Action,
                            true,
                            true,
                            false,
                            0,
                            {}});
  auto &menu = ui.make<Menu>(model);
  menu.set_max_visible_height(64.0f);
  std::string activated;
  menu.activated().connect(
      [&activated](Menu &, size_t, CommandId, const CommandData &command) {
        activated = command.stable_id;
      });
  assert(menu.show(document.get(), tc_ui_point{390.0f, 290.0f},
                   tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f}));
  assert(document.overlay_count() == 1);
  assert(menu.bounds().x + menu.bounds().width <= 400.0f);
  assert(menu.bounds().y + menu.bounds().height <= 300.0f);
  assert(menu.content_height() > menu.bounds().height);

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(menu.current_index() == 2);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 2);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == "recent-scene");
  assert(document.overlay_count() == 0);

  assert(menu.show(document.get(), tc_ui_point{10.0f, 10.0f},
                   tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f}));
  tc_ui_pointer_event wheel{};
  wheel.type = TC_UI_POINTER_WHEEL;
  wheel.x = 20.0f;
  wheel.y = 20.0f;
  wheel.wheel_y = -1.0f;
  assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
  assert(menu.scroll_offset() > 0.0f);
  tc_ui_pointer_event outside{};
  outside.type = TC_UI_POINTER_DOWN;
  outside.x = 350.0f;
  outside.y = 250.0f;
  assert(document.dispatch_pointer_event(outside) == TC_UI_EVENT_HANDLED);
  assert(!menu.open());
  assert(document.overlay_count() == 0);
  assert(recent->contains(recent_scene));
}

void test_menu_bar_adjacent_switching_shortcuts_and_overlay_lifetime() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto file = std::make_shared<CommandModel>();
  file->append(CommandData{"save",
                           "Save",
                           {},
                           "Ctrl+S",
                           {},
                           CommandKind::Action,
                           true,
                           true,
                           false,
                           0,
                           {}});
  auto edit = std::make_shared<CommandModel>();
  const CommandId undo = edit->append(CommandData{"undo",
                                                  "Undo",
                                                  {},
                                                  "Ctrl+Z",
                                                  {},
                                                  CommandKind::Action,
                                                  true,
                                                  false,
                                                  false,
                                                  0,
                                                  {}});
  auto &root = ui.make_root<BoxLayout>(Orientation::Vertical, "menu-bar-root");
  auto &bar = ui.make<MenuBar>();
  root.add_fixed_child(bar, 30.0f);
  bar.set_entries({{"file", "File", file}, {"edit", "Edit", edit}});
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f});
  assert(bar.item_rects().size() == 2);
  std::vector<std::string> activated;
  bar.activated().connect(
      [&activated](MenuBar &, size_t, CommandId, const CommandData &command) {
        activated.push_back(command.stable_id);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = bar.item_rects()[0].x + 2.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 0);
  assert(document.overlay_count() == 1);
  assert((tc_ui_document_overlay_flags_at(document.get(), 0) &
          TC_UI_OVERLAY_ALLOW_ROOT_HIT) != 0);
  const tc_widget_handle anchor_hit =
      tc_ui_document_hit_test(document.get(), bar.item_rects()[1].x + 2.0f, 10.0f);
  assert(!tc_widget_handle_is_invalid(anchor_hit));
  assert(tc_widget_handle_eq(anchor_hit, bar.handle()));
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = bar.item_rects()[1].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 1);
  assert(document.overlay_count() == 1);
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = bar.item_rects()[0].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 0);
  assert(document.overlay_count() == 1);

  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(!bar.menu_open());
  assert(document.overlay_count() == 0);
  assert(bar.dispatch_shortcut('s', TC_UI_MOD_CTRL));
  assert(file->command_at(0).data.checked);
  assert(bar.dispatch_shortcut('Z', TC_UI_MOD_CTRL));
  assert(activated == std::vector<std::string>({"save", "undo"}));
  assert(edit->contains(undo));

  const tc_widget_handle bar_handle = bar.handle();
  assert(tc_ui_document_destroy_widget(document.get(), bar_handle));
}

void test_dialog_modal_stack_focus_actions_and_exactly_once_results() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &background = ui.make_root<TextInput>("Background");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f});
  assert(document.set_focus(background));

  auto &dialog = ui.make<Dialog>("Confirm");
  auto &content = ui.make<Label>("Apply changes?");
  const tc_widget_handle content_handle = content.handle();
  dialog.set_content(content);
  dialog.set_actions({
      DialogAction{"apply", "Apply", true, false},
      DialogAction{"cancel", "Cancel", false, true},
  });
  std::vector<DialogResult> results;
  dialog.finished().connect([&results](Dialog &, const DialogResult &result) {
    results.push_back(result);
  });
  assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(document.overlay_count() == 1);
  assert(dialog.button_handles().size() == 2);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[0]));
  assert(dialog.bounds().x > 0.0f && dialog.bounds().y > 0.0f);

  tc_ui_key_event tab{};
  tab.type = TC_UI_KEY_DOWN;
  tab.key = TC_UI_KEY_TAB;
  assert(document.dispatch_key_event(tab) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[1]));

  auto &nested = ui.make<Dialog>("Nested");
  nested.set_actions({
      DialogAction{"ok", "OK", true, false},
      DialogAction{"back", "Back", false, true},
  });
  std::vector<DialogResult> nested_results;
  nested.finished().connect(
      [&nested_results](Dialog &, const DialogResult &result) {
        nested_results.push_back(result);
      });
  assert(nested.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(document.overlay_count() == 2);
  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 1);
  assert(nested_results.size() == 1);
  assert(nested_results[0].action_id == "back");
  assert(nested_results[0].reason == DialogDismissReason::Escape);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[1]));

  tc_widget *default_button_widget =
      tc_ui_document_resolve_widget(document.get(), dialog.button_handles()[0]);
  auto *default_button = dynamic_cast<Button *>(
      static_cast<Widget *>(default_button_widget->body));
  assert(default_button && document.set_focus(*default_button));
  tc_ui_key_event enter{};
  enter.type = TC_UI_KEY_DOWN;
  enter.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(enter) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 0);
  assert(results.size() == 1);
  assert(results[0].action_id == "apply");
  assert(results[0].reason == DialogDismissReason::Action);
  assert(tc_widget_handle_eq(document.focused_widget(), background.handle()));

  assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(dialog.close(document.get()));
  assert(results.size() == 2);
  assert(results[1].action_id.empty());
  assert(results[1].reason == DialogDismissReason::Programmatic);
  assert(!dialog.close(document.get()));
  assert(results.size() == 2);

  const tc_widget_handle dialog_handle = dialog.handle();
  assert(tc_ui_document_destroy_widget(document.get(), dialog_handle));
  assert(!tc_ui_document_is_alive(document.get(), content_handle));
}

void test_message_box_and_input_dialog_share_modal_result_contract() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &message = ui.make<MessageBox>("Delete", "Delete selected entity?",
                                      MessageBoxKind::Question);
  std::vector<DialogResult> message_results;
  message.finished().connect(
      [&message_results](Dialog &, const DialogResult &result) {
        message_results.push_back(result);
      });
  assert(message.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(message.child_count() == 3);
  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(message_results.size() == 1);
  assert(message_results[0].action_id == "no");
  assert(message_results[0].reason == DialogDismissReason::Escape);

  auto &input = ui.make<InputDialog>("Rename", "New name", "Old name");
  std::vector<std::optional<std::string>> values;
  input.value_finished().connect(
      [&values](InputDialog &, const std::optional<std::string> &value) {
        values.push_back(value);
      });
  assert(input.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(input.value() == "Old name");
  input.set_value("New name");
  assert(input.value() == "New name");
  tc_ui_key_event enter{};
  enter.type = TC_UI_KEY_DOWN;
  enter.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(enter) == TC_UI_EVENT_HANDLED);
  assert(values.size() == 1 &&
         values[0] == std::optional<std::string>{"New name"});
  assert(!input.open());

  assert(input.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(values.size() == 2 && !values[1].has_value());
}

void test_file_grid_widget_input_scrollbar_signals_and_lifetime() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  for (size_t index = 0; index < 20; ++index) {
    model->append(CollectionItem{"file-" + std::to_string(index),
                                 "File " + std::to_string(index), ".txt",
                                 index != 3});
  }
  std::weak_ptr<CollectionModel> weak_model = model;
  auto &grid = ui.make_root<FileGridWidget>(model);
  grid.set_tile_size(50.0f, 30.0f);
  grid.set_tile_spacing(0.0f);
  grid.set_padding(0.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 90.0f});
  assert(grid.column_count() == 2);

  std::vector<size_t> activated;
  std::vector<size_t> deleted;
  std::vector<int64_t> contexts;
  grid.activated().connect(
      [&activated](FileGridWidget &, size_t index, const CollectionItem &) {
        activated.push_back(index);
      });
  grid.delete_requested().connect(
      [&deleted](FileGridWidget &, size_t index, const CollectionItem &) {
        deleted.push_back(index);
      });
  grid.context_menu_requested().connect(
      [&contexts](FileGridWidget &, int64_t index, float, float) {
        contexts.push_back(index);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = 10.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 0);

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 2);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 4);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  key.key = TC_UI_KEY_DELETE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == std::vector<size_t>{4});
  assert(deleted == std::vector<size_t>{4});

  pointer.button = 1;
  pointer.x = 10.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts == std::vector<int64_t>{0});
  pointer.x = 105.0f;
  pointer.y = 85.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts.back() == -1);

  pointer.button = 0;
  pointer.x = 118.0f;
  pointer.y = 5.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), grid.handle()));
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.y = 50.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(grid.scroll_y() > 0.0f);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = grid.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<CollectionModel>();
  destroying_model->set_items({
      CollectionItem{"first", "First", {}, true},
      CollectionItem{"last", "Last", {}, true},
  });
  auto &destroying_grid = ui.make_root<FileGridWidget>(destroying_model);
  assert(destroying_grid.select_index(1));
  const tc_widget_handle destroying_handle = destroying_grid.handle();
  destroying_grid.selection_changed().connect([&document, destroying_handle](
                                                  FileGridWidget &,
                                                  const std::vector<size_t> &) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(0);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));
}

void test_table_widget_virtualizes_large_model_and_lays_out_columns() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TableModel>();
  std::vector<TableRowData> rows;
  rows.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    rows.push_back(TableRowData{
        "row-" + std::to_string(index),
        {"Row " + std::to_string(index), std::to_string(index), "Ready"},
        true,
    });
  }
  model->set_rows(std::move(rows));
  auto columns = std::make_shared<TableColumnModel>();
  columns->set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 80.0f, 60.0f, 0.0f,
                  1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
      TableColumn{"state", "State", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  160.0f, 2.0f, true},
  });
  auto &table = ui.make_root<TableWidget>(model, columns);
  table.set_row_height(24.0f);
  table.set_header_height(28.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 400.0f, 128.0f});

  assert(table.child_count() == 0);
  assert(table.content_height() == 240000.0f);
  const auto [first, last] = table.visible_range();
  assert(first == 0);
  assert(last <= 6);
  const auto &layout = table.column_layout();
  assert(layout.size() == 3);
  assert(near(layout[0].width, 80.0f));
  assert(layout[1].width >= 40.0f);
  assert(near(layout[2].width, 160.0f));
  assert(near(layout[2].x + layout[2].width, 400.0f));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 21);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(table.select_row(9999));
  assert(table.scroll_y() > 239000.0f);
  assert(table.visible_range().first > 9990);
}

void test_table_widget_pointer_keyboard_resize_signals_and_lifetime() {
  Document document;
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TableModel>();
  const TableRowId first =
      model->append(TableRowData{"first", {"First", "1"}, true});
  const TableRowId disabled =
      model->append(TableRowData{"disabled", {"Disabled", "2"}, false});
  const TableRowId last =
      model->append(TableRowData{"last", {"Last", "3"}, true});
  auto columns = std::make_shared<TableColumnModel>();
  columns->set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 100.0f, 60.0f,
                  180.0f, 1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
  });
  std::weak_ptr<TableModel> weak_model = model;
  std::weak_ptr<TableColumnModel> weak_columns = columns;
  auto &table = ui.make_root<TableWidget>(model, columns);
  table.set_row_height(30.0f);
  table.set_header_height(30.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 260.0f, 120.0f});

  size_t clicked_column = SIZE_MAX;
  size_t resized_column = SIZE_MAX;
  float resized_width = 0.0f;
  TableRowId activated = kInvalidTableRowId;
  std::vector<int64_t> contexts;
  table.header_clicked().connect(
      [&clicked_column](TableWidget &, size_t index, const TableColumn &) {
        clicked_column = index;
      });
  table.column_resized().connect([&resized_column, &resized_width](
                                     TableWidget &, size_t index, float width) {
    resized_column = index;
    resized_width = width;
  });
  table.activated().connect(
      [&activated](TableWidget &, size_t, TableRowId id, const TableRowData &) {
        activated = id;
      });
  table.context_menu_requested().connect(
      [&contexts](TableWidget &, int64_t index, float, float) {
        contexts.push_back(index);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 20.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(clicked_column == 0);

  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(table.selection().current() == 0);
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(table.selection().current() == 2);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == last);

  pointer.button = 1;
  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts == std::vector<int64_t>{0});
  assert(table.clear_selection());
  pointer.button = 0;

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 100.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), table.handle()));
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = 140.0f;
  pointer.y = -50.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(resized_column == 0);
  assert(near(resized_width, 140.0f));
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  assert(table.select_row(0));
  model->insert(0, TableRowData{"inserted", {"Inserted", "0"}, true});
  assert(table.selection().current() == 1);
  model->erase(first);
  assert(table.selection().selected_indices().empty());
  assert(model->contains(disabled));

  model.reset();
  columns.reset();
  assert(!weak_model.expired());
  assert(!weak_columns.expired());
  const tc_widget_handle handle = table.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());
  assert(weak_columns.expired());

  auto destroying_model = std::make_shared<TableModel>();
  const TableRowId destroying_first = destroying_model->append(
      TableRowData{"destroying-first", {"First"}, true});
  destroying_model->append(TableRowData{"destroying-last", {"Last"}, true});
  auto &destroying_table = ui.make_root<TableWidget>(destroying_model);
  assert(destroying_table.select_row(1));
  const tc_widget_handle destroying_handle = destroying_table.handle();
  destroying_table.selection_changed().connect(
      [&document, destroying_handle](TableWidget &,
                                     const std::vector<size_t> &) {
        assert(
            tc_ui_document_destroy_widget(document.get(), destroying_handle));
      });
  destroying_model->erase(destroying_first);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));
}

void test_host_click_count_drives_collection_activation() {
  {
    Document document;
    DocumentBuilder ui(document);
    auto model = std::make_shared<CollectionModel>();
    model->append(CollectionItem{"item", "Item"});
    auto &list = ui.make_root<ListWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 80.0f});
    size_t activations = 0;
    list.activated().connect(
        [&activations](ListWidget &, size_t, const CollectionItem &) {
          ++activations;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.x = 10.0f;
    pointer.y = 10.0f;
    pointer.click_count = 1;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 0);
    pointer.click_count = 2;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
    pointer.click_count = 3;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
  }
  {
    Document document;
    DocumentBuilder ui(document);
    auto model = std::make_shared<TreeModel>();
    const TreeNodeId node = model->append_root(CollectionItem{"node", "Node"});
    auto &tree = ui.make_root<TreeWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 80.0f});
    TreeNodeId activated = kInvalidTreeNodeId;
    tree.activated().connect(
        [&activated](TreeWidget &, TreeNodeId id, const CollectionItem &) {
          activated = id;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 2;
    pointer.x = 30.0f;
    pointer.y = 10.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activated == node);
  }
  {
    Document document;
    DocumentBuilder ui(document);
    auto model = std::make_shared<TableModel>();
    const TableRowId row = model->append(TableRowData{"row", {"Row"}, true});
    auto columns = std::make_shared<TableColumnModel>();
    columns->set_columns({TableColumn{
        "value", "Value", TableColumnPolicy::Stretch, 0.0f, 20.0f}});
    auto &table = ui.make_root<TableWidget>(model, columns);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 100.0f});
    TableRowId activated = kInvalidTableRowId;
    table.activated().connect(
        [&activated](TableWidget &, size_t, TableRowId id,
                     const TableRowData &) { activated = id; });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 2;
    pointer.x = 10.0f;
    pointer.y = 40.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activated == row);
  }
  {
    Document document;
    DocumentBuilder ui(document);
    auto model = std::make_shared<CollectionModel>();
    model->append(CollectionItem{"file", "File"});
    auto &grid = ui.make_root<FileGridWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 120.0f});
    size_t activations = 0;
    grid.activated().connect(
        [&activations](FileGridWidget &, size_t, const CollectionItem &) {
          ++activations;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 2;
    pointer.x = 20.0f;
    pointer.y = 20.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
  }
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
  test_tab_view_page_mutation_and_selection_signal();
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
  test_text_input_utf8_selection_and_host_clipboard();
  test_text_area_multiline_utf8_editing_navigation_and_scroll();
  test_spin_box_numeric_edit_buttons_and_keys();
  test_slider_edit_owns_canonical_children_and_syncs_values();
  test_combo_box_overlay_selection_and_destruction();
  test_icon_image_and_canvas_media_contracts();
  test_widget_signals_are_emitted_from_interactions();
  test_containers_register_and_replace_canonical_children();
  test_common_visibility_enabled_and_mouse_transparent_state();
  test_cpp_theme_style_facade_inheritance_and_state();
  test_collection_and_selection_models_are_reusable();
  test_list_widget_virtualizes_large_models_and_reconciles_selection();
  test_list_widget_pointer_keyboard_and_multi_selection();
  test_list_widget_model_notifications_preserve_lifetime_and_shift_selection();
  test_tree_model_stable_ids_move_and_expansion_reconcile();
  test_tree_widget_virtualizes_large_expanded_model();
  test_tree_widget_pointer_keyboard_signals_and_lifetime();
  test_tree_widget_drag_drop_positions_and_capture();
  test_file_grid_widget_virtualizes_large_model_and_responsive_layout();
  test_file_grid_widget_input_scrollbar_signals_and_lifetime();
  test_command_model_stable_ids_validation_and_mutation();
  test_tool_bar_layout_activation_capture_and_model_lifetime();
  test_status_bar_explicit_message_lifecycle_and_utf8_validation();
  test_menu_overlay_navigation_submenus_scrolling_and_dismissal();
  test_menu_bar_adjacent_switching_shortcuts_and_overlay_lifetime();
  test_dialog_modal_stack_focus_actions_and_exactly_once_results();
  test_message_box_and_input_dialog_share_modal_result_contract();
  test_table_models_preserve_row_ids_and_validate_columns();
  test_table_widget_virtualizes_large_model_and_lays_out_columns();
  test_table_widget_pointer_keyboard_resize_signals_and_lifetime();
  test_host_click_count_drives_collection_activation();
  return EXIT_SUCCESS;
}
