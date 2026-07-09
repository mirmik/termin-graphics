#include <termin/gui_native/showcase.hpp>
#include <termin/gui_native/widgets.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace termin::gui_native;

namespace {

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

void require_equal(size_t actual, size_t expected, const char* label) {
    if (actual == expected) {
        return;
    }
    std::fprintf(stderr, "%s: expected %zu, got %zu\n", label, expected, actual);
    std::abort();
}

void test_showcase_builds_stable_headless_snapshot() {
    Document document;
    ShowcaseRefs refs = build_showcase(document);

    assert(refs.progress);
    assert(refs.slider);
    assert(refs.checkbox);
    assert(refs.content_scroll);
    assert(refs.text_input);
    assert(refs.tabs);
    assert(refs.tabs->page_count() == 2);
    assert(tc_ui_document_root_count(document.get()) == 1);
    assert(tc_ui_document_live_widget_count(document.get()) >= 25);

    refs.slider->set_value(0.75f);
    assert(refs.progress->value() == 0.75f);

    document.layout_roots(tc_ui_rect {0.0f, 0.0f, 800.0f, 600.0f});
    assert(refs.content_scroll->content_size().height >= refs.content_scroll->bounds().height);
    assert(refs.text_input->bounds().width > 0.0f);

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(paint_context);

    require_equal(tc_ui_draw_list_command_count(draw_list), 81, "showcase total commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_FILL_RECT), 25, "showcase fill commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_STROKE_RECT), 19, "showcase stroke commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_LINE), 8, "showcase line commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_TEXT), 9, "showcase text commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP), 10, "showcase push clip commands");
    require_equal(count_commands(draw_list, TC_UI_DRAW_POP_CLIP), 10, "showcase pop clip commands");

    const tc_ui_draw_command* first = tc_ui_draw_list_command_at(draw_list, 0);
    const tc_ui_draw_command* last = tc_ui_draw_list_command_at(
        draw_list,
        tc_ui_draw_list_command_count(draw_list) - 1
    );
    assert(first && first->type == TC_UI_DRAW_FILL_RECT);
    assert(last && last->type == TC_UI_DRAW_POP_CLIP);

    tc_ui_paint_context_destroy(paint_context);
    tc_ui_draw_list_destroy(draw_list);
}

} // namespace

int main() {
    test_showcase_builds_stable_headless_snapshot();
    return EXIT_SUCCESS;
}
