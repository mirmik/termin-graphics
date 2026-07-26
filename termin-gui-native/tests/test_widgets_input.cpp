#include <termin/gui_native/tc_document.hpp>

#include "widgets_test_support.hpp"

namespace termin_gui_native_test {

void test_standard_control_input_conformance() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<HStack>("controls");
  auto &button = ui.make<Button>("Run");
  auto &checkbox = ui.make<Checkbox>(false);
  auto &slider = ui.make<Slider>(0.5f);
  auto &icon = ui.make<IconButton>("I");
  root.add_preferred_child(button);
  root.add_preferred_child(checkbox);
  root.add_stretch_child(slider);
  root.add_preferred_child(icon);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 40.0f});

  assert(button.focusable());
  assert(checkbox.focusable());
  assert(slider.focusable());
  assert(icon.focusable());

  int button_clicks = 0;
  int icon_clicks = 0;
  button.clicked().connect([&](Button &) { ++button_clicks; });
  icon.clicked().connect([&](IconButton &) { ++icon_clicks; });

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_ENTER;
  assert(document.set_focus(button));
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(button_clicks == 1);
  key.repeat = true;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(button_clicks == 1);

  key.repeat = false;
  key.key = TC_UI_KEY_SPACE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(button.pressed());
  key.repeat = true;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  key.type = TC_UI_KEY_UP;
  key.repeat = false;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(!button.pressed());
  assert(button_clicks == 2);

  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_SPACE;
  assert(document.set_focus(checkbox));
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  key.type = TC_UI_KEY_UP;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(checkbox.checked());

  assert(document.set_focus(slider));
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_HOME;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(near(slider.value(), slider.min_value()));
  key.key = TC_UI_KEY_PAGE_UP;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(slider.value() > slider.min_value());
  key.key = TC_UI_KEY_END;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(near(slider.value(), slider.max_value()));

  assert(document.set_focus(icon));
  key.key = TC_UI_KEY_ENTER;
  key.repeat = false;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(icon_clicks == 1);

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = tcbase::mouse_button_value(tcbase::MouseButton::RIGHT);
  pointer.x = button.bounds().x + 4.0f;
  pointer.y = button.bounds().y + 4.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  assert(button_clicks == 2);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  tc_widget_set_enabled(button.c_widget(), false);
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = tcbase::mouse_button_value(tcbase::MouseButton::LEFT);
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  assert(button_clicks == 2);
  tc_widget_set_enabled(button.c_widget(), true);
  tc_widget_set_visible(button.c_widget(), false);
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  assert(button_clicks == 2);

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &first_page = ui.make<Panel>("first");
  auto &second_page = ui.make<Panel>("second");
  tabs.add_page("First", first_page);
  tabs.add_page("Second", second_page);
  tabs.layout(document.get(), tc_ui_rect{0.0f, 50.0f, 240.0f, 120.0f});
  assert(tabs.focusable());
  assert(document.set_focus(tabs));
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_PAGE_DOWN;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tabs.selected_index() == 1);
  key.key = TC_UI_KEY_HOME;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tabs.selected_index() == 0);

  auto &picker = ui.make_root<ColorPicker>();
  assert(picker.focusable());
  assert(document.set_focus(picker));
  const float old_hue = picker.model()->hue();
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(picker.model()->hue() > old_hue);

  tc_ui_document_destroy(document_handle);
}

void test_widget_signals_are_emitted_from_interactions() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
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

  tc_ui_document_destroy(document_handle);
}

} // namespace termin_gui_native_test
