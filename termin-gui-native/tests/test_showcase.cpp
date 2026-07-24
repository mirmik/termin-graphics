#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/showcase.hpp>
#include <termin/gui_native/widgets.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace termin::gui_native;

namespace {

bool measure_test_text(void *, const char *text, size_t byte_length,
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

void require_equal(size_t actual, size_t expected, const char *label) {
  if (actual == expected) {
    return;
  }
  std::fprintf(stderr, "%s: expected %zu, got %zu\n", label, expected, actual);
  std::abort();
}

void test_showcase_builds_stable_headless_snapshot() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  document.set_text_measurer(&measure_test_text, nullptr);
  ShowcaseRefs refs = build_showcase(document);

  assert(refs.progress);
  assert(refs.slider);
  assert(refs.toolbar);
  assert(refs.menu_bar);
  assert(refs.message_box);
  assert(refs.status_bar);
  assert(refs.checkbox);
  assert(refs.file_grid);
  assert(refs.list);
  assert(refs.tree);
  assert(refs.table);
  assert(refs.content_scroll);
  assert(refs.text_area);
  assert(refs.text_input);
  assert(refs.tabs);
  assert(refs.tabs->page_count() == 2);
  assert((refs.list->selection().selected_indices() == std::vector<size_t>{1}));
  assert(refs.tree->visible_count() == 3);
  assert(refs.table->model()->size() == 3);
  assert(refs.file_grid->model()->size() == 4);
  assert(refs.toolbar->model()->size() == 3);
  assert(refs.menu_bar->entries().size() == 2);
  assert(refs.status_bar->displayed_text() == "Ready | Native UI");
  assert(tc_ui_document_root_count(document.get()) == 1);
  assert(tc_ui_document_live_widget_count(document.get()) >= 25);

  refs.slider->set_value(0.75f);
  assert(refs.progress->value() == 0.75f);

  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f});
  assert(refs.content_scroll->content_size().height >=
         refs.content_scroll->bounds().height);
  assert(refs.text_input->bounds().width > 0.0f);
  assert(refs.text_area->bounds().height > 0.0f);

  tc_ui_pointer_event open_menu{};
  open_menu.type = TC_UI_POINTER_DOWN;
  open_menu.button = 0;
  open_menu.x = refs.menu_bar->item_rects()[0].x + 2.0f;
  open_menu.y = refs.menu_bar->bounds().y + 2.0f;
  assert(document.dispatch_pointer_event(open_menu) == TC_UI_EVENT_HANDLED);
  assert(refs.menu_bar->menu_open());
  assert(document.overlay_count() == 1);
  assert(refs.message_box->show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(document.overlay_count() == 2);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *paint_context = tc_ui_paint_context_create(draw_list);
  document.paint(paint_context);

  require_equal(tc_ui_draw_list_command_count(draw_list), 224,
                "showcase total commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_FILL_RECT), 33,
                "showcase fill commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_STROKE_RECT), 14,
                "showcase stroke commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_LINE), 11,
                "showcase line commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_TEXT), 56,
                "showcase text commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP), 48,
                "showcase push clip commands");
  require_equal(count_commands(draw_list, TC_UI_DRAW_POP_CLIP), 48,
                "showcase pop clip commands");

  const tc_ui_draw_command *first = tc_ui_draw_list_command_at(draw_list, 0);
  const tc_ui_draw_command *last = tc_ui_draw_list_command_at(
      draw_list, tc_ui_draw_list_command_count(draw_list) - 1);
  assert(first && first->type == TC_UI_DRAW_FILL_RECT);
  assert(last && last->type == TC_UI_DRAW_POP_CLIP);

  tc_ui_paint_context_destroy(paint_context);
  tc_ui_draw_list_destroy(draw_list);
  tc_ui_document_destroy(document_handle);
}

} // namespace

int main() {
  test_showcase_builds_stable_headless_snapshot();
  return EXIT_SUCCESS;
}
