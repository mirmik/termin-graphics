#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>

using namespace termin::gui_native;

namespace termin_gui_native_test {

inline bool near(float a, float b, float epsilon = 0.001f) {
  return std::fabs(a - b) <= epsilon;
}

inline size_t count_commands(const tc_ui_draw_list *draw_list,
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

inline bool test_text_measure(void *, const char *text, size_t byte_length,
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

inline void install_test_text_measurer(Document &document) {
  document.set_text_measurer(&test_text_measure, nullptr);
}

struct TestClipboard {
  std::string text;
};

inline const char *test_clipboard_get(void *user_data) {
  return static_cast<TestClipboard *>(user_data)->text.c_str();
}

inline bool test_clipboard_set(void *user_data, const char *text, size_t byte_length) {
  if (!user_data || (!text && byte_length > 0)) {
    return false;
  }
  static_cast<TestClipboard *>(user_data)->text.assign(text ? text : "",
                                                       byte_length);
  return true;
}

inline void install_test_clipboard(Document &document, TestClipboard &clipboard) {
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

} // namespace termin_gui_native_test
