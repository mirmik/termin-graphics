#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace termin::gui_native;

namespace {

bool measure_text(void*, const char* text, size_t byte_length, float font_size,
                  tc_ui_text_metrics* metrics) {
    if (!text || !metrics || font_size <= 0.0f)
        return false;
    size_t codepoints = 0;
    for (size_t offset = 0; offset < byte_length; ++codepoints) {
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        if (first < 0x80u)
            offset += 1;
        else if ((first & 0xe0u) == 0xc0u && offset + 2 <= byte_length)
            offset += 2;
        else if ((first & 0xf0u) == 0xe0u && offset + 3 <= byte_length)
            offset += 3;
        else if ((first & 0xf8u) == 0xf0u && offset + 4 <= byte_length)
            offset += 4;
        else
            return false;
    }
    metrics->width = static_cast<float>(codepoints) * font_size * 0.5f;
    metrics->height = font_size;
    metrics->ascent = font_size * 0.8f;
    metrics->descent = font_size * 0.2f;
    metrics->line_height = font_size * 1.25f;
    return true;
}

struct Clipboard {
    std::string text;
};

bool set_clipboard(void* user_data, const char* text, size_t byte_length) {
    auto* clipboard = static_cast<Clipboard*>(user_data);
    if (!clipboard || (!text && byte_length > 0))
        return false;
    clipboard->text.assign(text ? text : "", byte_length);
    return true;
}

size_t count_commands(const tc_ui_draw_list* draw_list, tc_ui_draw_command_type type) {
    size_t count = 0;
    for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (command && command->type == type)
            ++count;
    }
    return count;
}

void test_model_owns_structured_utf8_and_parses_small_html_subset() {
    RichTextModel model;
    size_t changes = 0;
    model.changed().connect([&changes](RichTextModel&) { ++changes; });

    model.set_html("<pre>A<br><span style='color:#50fa7b; font-weight:bold; "
                   "font-style:italic'>B &amp; &#x3bb;</span></pre>");
    assert(model.text() == "A\nB & \xce\xbb");
    assert(model.lines().size() == 2);
    assert(model.lines()[1].size() == 1);
    const RichTextSegment& styled = model.lines()[1][0];
    assert(styled.style.bold && styled.style.italic && styled.style.color.has_value());
    assert(std::fabs(styled.style.color->r - 80.0f / 255.0f) < 0.001f);
    assert(std::fabs(styled.style.color->g - 250.0f / 255.0f) < 0.001f);
    assert(changes == 1);

    model.set_text("plain\ntext");
    assert(model.lines().size() == 2);
    assert(!model.lines()[0][0].style.bold);
    assert(changes == 2);

    bool rejected = false;
    try {
        model.set_lines({RichTextLine{RichTextSegment{"bad\nsegment", {}}}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    assert(model.text() == "plain\ntext");
}

void test_view_wrap_keeps_model_text_and_copy_selection_stable() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    document.set_text_measurer(&measure_text, nullptr);
    Clipboard clipboard;
    document.set_clipboard(nullptr, &set_clipboard, &clipboard);
    auto model = std::make_shared<RichTextModel>();
    model->set_text("alpha beta gamma\n\xce\xbb-line");
    auto* view = new RichTextView(model);
    const tc_widget_handle handle = document.adopt(view);
    assert(!tc_widget_handle_is_invalid(handle));
    assert(document.add_root(*view));
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 72.0f, 70.0f});
    assert(view->visual_line_count() > model->lines().size());

    view->select_all();
    assert(view->selected_text() == model->text());
    tc_ui_key_event copy{TC_UI_KEY_DOWN, TC_UI_KEY_C, 0, TC_UI_MOD_CTRL, false};
    assert(view->key_event(document.get(), &copy) == TC_UI_EVENT_HANDLED);
    assert(clipboard.text == model->text());

    const size_t wrapped_lines = view->visual_line_count();
    model->set_text("short");
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 72.0f, 70.0f});
    assert(view->visual_line_count() < wrapped_lines);
    assert(!view->has_selection());

    model.reset();
    assert(view->model()->text() == "short");
    assert(tc_ui_document_destroy_widget_recursive(document.get(), handle));
    assert(!tc_ui_document_is_alive(document.get(), handle));
    tc_ui_document_destroy(document_handle);
}

void test_view_paint_scrollbar_capture_and_styled_commands() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    document.set_text_measurer(&measure_text, nullptr);
    auto model = std::make_shared<RichTextModel>();
    RichTextStyle bold;
    bold.bold = true;
    bold.color = tc_ui_color{0.2f, 0.8f, 0.3f, 1.0f};
    std::vector<RichTextLine> lines;
    for (size_t index = 0; index < 20; ++index) {
        lines.push_back(RichTextLine{RichTextSegment{"styled row", bold}});
    }
    model->set_lines(std::move(lines));
    auto* view = new RichTextView(model);
    const tc_widget_handle handle = document.adopt(view);
    assert(document.add_root(*view));
    document.layout_roots(tc_ui_rect{10.0f, 20.0f, 180.0f, 80.0f});

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(context);
    assert(count_commands(draw_list, TC_UI_DRAW_PUSH_CLIP) == 1);
    assert(count_commands(draw_list, TC_UI_DRAW_POP_CLIP) == 1);
    assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 2);
    assert(count_commands(draw_list, TC_UI_DRAW_FILL_ROUNDED_RECT) >= 2);
    assert(view->content_height() > view->bounds().height);

    tc_ui_pointer_event wheel{TC_UI_POINTER_WHEEL, 40.0f, 40.0f, 0, 0, 0, 0.0f, -1.0f};
    assert(view->pointer_event(document.get(), &wheel) == TC_UI_EVENT_HANDLED);
    assert(view->scroll_y() > 0.0f);

    tc_ui_pointer_event down{TC_UI_POINTER_DOWN, 188.0f, 45.0f, 0, 1, 0, 0.0f, 0.0f};
    assert(view->pointer_event(document.get(), &down) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.pointer_capture(), handle));
    tc_ui_pointer_event move{TC_UI_POINTER_MOVE, 188.0f, 70.0f, 0, 0, 0, 0.0f, 0.0f};
    assert(view->pointer_event(document.get(), &move) == TC_UI_EVENT_HANDLED);
    tc_ui_pointer_event up{TC_UI_POINTER_UP, 188.0f, 70.0f, 0, 0, 0, 0.0f, 0.0f};
    assert(view->pointer_event(document.get(), &up) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));

    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
    tc_ui_document_destroy(document_handle);
}

} // namespace

int main() {
    test_model_owns_structured_utf8_and_parses_small_html_subset();
    test_view_wrap_keeps_model_text_and_copy_selection_stable();
    test_view_paint_scrollbar_capture_and_styled_commands();
    return EXIT_SUCCESS;
}
