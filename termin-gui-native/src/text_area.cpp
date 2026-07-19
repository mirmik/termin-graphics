#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
TextArea::TextArea(std::string text)
    : NativeWidget("TextArea"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_cursor_intent(TC_UI_CURSOR_TEXT);
    if (!valid_utf8(text_)) {
        tc_log_error("[termin-gui-native] TextArea rejected invalid UTF-8 initial text");
        text_.clear();
    }
    caret_ = text_.size();
    set_focusable(true);
    set_preferred_size(tc_ui_size {300.0f, 150.0f});
}

bool TextArea::has_selection() const {
    return selection_anchor_ != SIZE_MAX && selection_anchor_ != caret_;
}

size_t TextArea::selection_start() const {
    return has_selection() ? std::min(selection_anchor_, caret_) : caret_;
}

size_t TextArea::selection_end() const {
    return has_selection() ? std::max(selection_anchor_, caret_) : caret_;
}

std::string TextArea::selected_text() const {
    return has_selection()
        ? text_.substr(selection_start(), selection_end() - selection_start())
        : std::string {};
}

void TextArea::set_text(std::string text) {
    if (!valid_utf8(text)) {
        tc_log_error("[termin-gui-native] TextArea rejected invalid UTF-8 text");
        return;
    }
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    caret_ = utf8_floor_boundary(text_, std::min(caret_, text_.size()));
    selection_anchor_ = SIZE_MAX;
    desired_x_ = -1.0f;
    emit_changed();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT);
}

void TextArea::set_caret(size_t caret) {
    move_caret(caret, false);
}

void TextArea::select(size_t anchor, size_t caret) {
    selection_anchor_ = utf8_floor_boundary(text_, std::min(anchor, text_.size()));
    caret_ = utf8_floor_boundary(text_, std::min(caret, text_.size()));
    if (selection_anchor_ == caret_) {
        selection_anchor_ = SIZE_MAX;
    }
    desired_x_ = -1.0f;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TextArea::select_all() {
    select(0, text_.size());
}

void TextArea::clear_selection() {
    if (selection_anchor_ != SIZE_MAX) {
        selection_anchor_ = SIZE_MAX;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
}

std::vector<TextArea::Line> TextArea::lines() const {
    std::vector<Line> result;
    size_t start = 0;
    while (start <= text_.size()) {
        const size_t newline = text_.find('\n', start);
        if (newline == std::string::npos) {
            result.push_back(Line {start, text_.size()});
            break;
        }
        result.push_back(Line {start, newline});
        start = newline + 1;
        if (start == text_.size()) {
            result.push_back(Line {start, start});
            break;
        }
    }
    return result;
}

tc_ui_rect TextArea::text_clip_rect(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    return tc_ui_rect {
        bounds().x + style.padding_left,
        bounds().y + style.padding_top,
        std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
        std::max(0.0f, bounds().height - style.padding_top - style.padding_bottom)
    };
}

float TextArea::line_height(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    if (measure_text(document, "Mg", style.font_size, metrics)) {
        return metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
    }
    return style.font_size * 1.4f;
}

bool TextArea::measure_range(
    tc_ui_document* document,
    size_t start,
    size_t end,
    float font_size,
    float& width
) const {
    start = utf8_floor_boundary(text_, std::min(start, text_.size()));
    end = utf8_floor_boundary(text_, std::min(end, text_.size()));
    if (end < start) {
        std::swap(start, end);
    }
    tc_ui_text_metrics metrics {};
    if (!measure_text(document, std::string_view(text_).substr(start, end - start), font_size, metrics)) {
        width = 0.0f;
        return false;
    }
    width = metrics.width;
    return true;
}

size_t TextArea::line_index_for_offset(const std::vector<Line>& spans, size_t offset) const {
    for (size_t index = 0; index < spans.size(); ++index) {
        if (offset <= spans[index].end) {
            return index;
        }
    }
    return spans.empty() ? 0 : spans.size() - 1;
}

size_t TextArea::caret_from_line_x(
    tc_ui_document* document,
    const Line& line,
    float content_x
) const {
    const tc_ui_style style = computed_style(document);
    size_t current = line.start;
    float current_width = 0.0f;
    while (current < line.end) {
        const size_t next = utf8_next_boundary(text_, current);
        float next_width = current_width;
        if (!measure_range(document, line.start, next, style.font_size, next_width)) {
            return current;
        }
        if (content_x < (current_width + next_width) * 0.5f) {
            return current;
        }
        current = next;
        current_width = next_width;
    }
    return line.end;
}

size_t TextArea::caret_from_point(tc_ui_document* document, float x, float y) const {
    const auto spans = lines();
    const tc_ui_rect clip = text_clip_rect(document);
    const float height = std::max(1.0f, line_height(document));
    const float content_y = std::max(0.0f, y - clip.y + scroll_y_);
    const size_t line_index = std::min(
        spans.size() - 1,
        static_cast<size_t>(content_y / height)
    );
    return caret_from_line_x(
        document,
        spans[line_index],
        std::max(0.0f, x - clip.x + scroll_x_)
    );
}

tc_ui_size TextArea::measure(tc_ui_document*, tc_ui_constraints constraints) {
    tc_ui_size measured = preferred_size();
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void TextArea::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    ensure_caret_visible(document);
}

void TextArea::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect clip = text_clip_rect(document);
    const float height = std::max(1.0f, line_height(document));
    const auto spans = lines();
    const bool focused = tc_widget_handle_eq(tc_ui_document_focused_widget(document), handle());
    tc_ui_text_metrics metrics {};
    measure_text(document, "Mg", style.font_size, metrics);
    const float ascent = metrics.ascent > 0.0f ? metrics.ascent : style.font_size;

    tc_ui_painter_fill_rect(context, bounds(), style.background);
    tc_ui_painter_stroke_rect(context, bounds(), style.border, style.border_width);
    tc_ui_painter_push_clip(context, clip);

    const size_t first_line = std::min(
        spans.size() - 1,
        static_cast<size_t>(std::max(0.0f, scroll_y_) / height)
    );
    const size_t visible_count = static_cast<size_t>(clip.height / height) + 2;
    const size_t last_line = std::min(spans.size(), first_line + visible_count);
    for (size_t index = first_line; index < last_line; ++index) {
        const Line& line = spans[index];
        const float row_y = clip.y + static_cast<float>(index) * height - scroll_y_;
        if (has_selection()) {
            const size_t start = std::max(line.start, selection_start());
            const size_t end = std::min(line.end, selection_end());
            if (end > start) {
                float x0 = 0.0f;
                float x1 = 0.0f;
                measure_range(document, line.start, start, style.font_size, x0);
                measure_range(document, line.start, end, style.font_size, x1);
                tc_ui_color selection_color = style.accent;
                selection_color.a *= 0.45f;
                tc_ui_painter_fill_rect(
                    context,
                    tc_ui_rect {clip.x + x0 - scroll_x_, row_y, x1 - x0, height},
                    selection_color
                );
            }
        }
        const std::string line_text = text_.substr(line.start, line.end - line.start);
        tc_ui_painter_draw_text(
            context,
            line_text.c_str(),
            tc_ui_point {clip.x - scroll_x_, row_y + ascent},
            style.font_size,
            style.foreground
        );
    }

    if (focused) {
        const size_t line_index = line_index_for_offset(spans, caret_);
        float caret_x = 0.0f;
        measure_range(document, spans[line_index].start, caret_, style.font_size, caret_x);
        const float x = clip.x + caret_x - scroll_x_;
        const float y = clip.y + static_cast<float>(line_index) * height - scroll_y_;
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {x, y},
            tc_ui_point {x, y + height},
            style.accent,
            1.0f
        );
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result TextArea::pointer_event(
    tc_ui_document* document,
    const tc_ui_pointer_event* event
) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_WHEEL && rect_contains(bounds(), event->x, event->y)) {
        const float height = line_height(document);
        const float content_height = static_cast<float>(lines().size()) * height;
        const float max_scroll = std::max(0.0f, content_height - text_clip_rect(document).height);
        scroll_y_ = clamp_float(scroll_y_ - event->wheel_y * height * 3.0f, 0.0f, max_scroll);
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        tc_ui_document_set_focus(document, handle());
        const size_t next = caret_from_point(document, event->x, event->y);
        if ((event->modifiers & TC_UI_MOD_SHIFT) != 0) {
            move_caret(next, true);
        } else {
            caret_ = next;
            selection_anchor_ = caret_;
        }
        selecting_ = true;
        desired_x_ = -1.0f;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && selecting_) {
        caret_ = caret_from_point(document, event->x, event->y);
        desired_x_ = -1.0f;
        ensure_caret_visible(document);
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && selecting_) {
        selecting_ = false;
        tc_ui_document_release_pointer_capture(document, handle());
        if (selection_anchor_ == caret_) {
            clear_selection();
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result TextArea::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool extend = (event->modifiers & TC_UI_MOD_SHIFT) != 0;
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'a')) {
        select_all();
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'c')) {
        if (has_selection()) {
            const std::string selected = selected_text();
            tc_ui_document_set_clipboard_text(document, selected.data(), selected.size());
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'x')) {
        if (has_selection()) {
            const std::string selected = selected_text();
            tc_ui_document_set_clipboard_text(document, selected.data(), selected.size());
            if (delete_selection()) emit_changed();
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'v')) {
        const char* clipboard = tc_ui_document_clipboard_text(document);
        if (clipboard) {
            const std::string_view inserted(clipboard);
            if (!valid_utf8(inserted)) {
                tc_log_error("[termin-gui-native] TextArea rejected invalid UTF-8 clipboard text");
            } else if (replace_selection(inserted)) {
                emit_changed();
            }
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }

    const auto spans = lines();
    const size_t line_index = line_index_for_offset(spans, caret_);
    switch (event->key) {
    case TC_UI_KEY_LEFT:
        if (!extend && has_selection()) move_caret(selection_start(), false);
        else move_caret(utf8_previous_boundary(text_, caret_), extend);
        break;
    case TC_UI_KEY_RIGHT:
        if (!extend && has_selection()) move_caret(selection_end(), false);
        else move_caret(utf8_next_boundary(text_, caret_), extend);
        break;
    case TC_UI_KEY_UP_ARROW:
        move_vertical(document, -1, extend);
        break;
    case TC_UI_KEY_DOWN_ARROW:
        move_vertical(document, 1, extend);
        break;
    case TC_UI_KEY_HOME:
        move_caret(spans[line_index].start, extend);
        break;
    case TC_UI_KEY_END:
        move_caret(spans[line_index].end, extend);
        break;
    case TC_UI_KEY_ENTER:
        if (replace_selection("\n")) emit_changed();
        break;
    case TC_UI_KEY_BACKSPACE:
        if (delete_selection()) {
            emit_changed();
        } else if (caret_ > 0) {
            const size_t previous = utf8_previous_boundary(text_, caret_);
            text_.erase(previous, caret_ - previous);
            caret_ = previous;
            emit_changed();
        }
        break;
    case TC_UI_KEY_DELETE:
        if (delete_selection()) {
            emit_changed();
        } else if (caret_ < text_.size()) {
            const size_t next = utf8_next_boundary(text_, caret_);
            text_.erase(caret_, next - caret_);
            emit_changed();
        }
        break;
    default:
        return TC_UI_EVENT_IGNORED;
    }
    ensure_caret_visible(document);
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result TextArea::text_event(
    tc_ui_document* document,
    const tc_ui_text_event* event
) {
    if (!event || !event->text || event->text[0] == '\0') {
        return TC_UI_EVENT_IGNORED;
    }
    const std::string_view inserted(event->text);
    if (!valid_utf8(inserted)) {
        tc_log_error("[termin-gui-native] TextArea rejected invalid UTF-8 input event");
        return TC_UI_EVENT_IGNORED;
    }
    if (replace_selection(inserted)) {
        emit_changed();
    }
    ensure_caret_visible(document);
    return TC_UI_EVENT_HANDLED;
}

void TextArea::ensure_caret_visible(tc_ui_document* document) {
    const auto spans = lines();
    const size_t line_index = line_index_for_offset(spans, caret_);
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect clip = text_clip_rect(document);
    const float height = std::max(1.0f, line_height(document));
    float caret_x = 0.0f;
    measure_range(document, spans[line_index].start, caret_, style.font_size, caret_x);
    if (caret_x < scroll_x_) scroll_x_ = caret_x;
    else if (caret_x > scroll_x_ + std::max(0.0f, clip.width - 1.0f)) {
        scroll_x_ = caret_x - std::max(0.0f, clip.width - 1.0f);
    }
    const float caret_y = static_cast<float>(line_index) * height;
    if (caret_y < scroll_y_) scroll_y_ = caret_y;
    else if (caret_y + height > scroll_y_ + clip.height) {
        scroll_y_ = caret_y + height - clip.height;
    }

    float max_width = 0.0f;
    for (const Line& line : spans) {
        float width = 0.0f;
        measure_range(document, line.start, line.end, style.font_size, width);
        max_width = std::max(max_width, width);
    }
    scroll_x_ = clamp_float(scroll_x_, 0.0f, std::max(0.0f, max_width - clip.width));
    scroll_y_ = clamp_float(
        scroll_y_,
        0.0f,
        std::max(0.0f, static_cast<float>(spans.size()) * height - clip.height)
    );
}

void TextArea::move_caret(size_t next, bool extend_selection, bool preserve_column) {
    next = utf8_floor_boundary(text_, std::min(next, text_.size()));
    if (extend_selection) {
        if (selection_anchor_ == SIZE_MAX) selection_anchor_ = caret_;
    } else {
        selection_anchor_ = SIZE_MAX;
    }
    caret_ = next;
    if (selection_anchor_ == caret_) selection_anchor_ = SIZE_MAX;
    if (!preserve_column) desired_x_ = -1.0f;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TextArea::move_vertical(
    tc_ui_document* document,
    int direction,
    bool extend_selection
) {
    const auto spans = lines();
    const size_t current = line_index_for_offset(spans, caret_);
    if ((direction < 0 && current == 0) ||
        (direction > 0 && current + 1 >= spans.size())) {
        return;
    }
    const tc_ui_style style = computed_style(document);
    if (desired_x_ < 0.0f) {
        measure_range(document, spans[current].start, caret_, style.font_size, desired_x_);
    }
    const size_t target = direction < 0 ? current - 1 : current + 1;
    move_caret(caret_from_line_x(document, spans[target], desired_x_), extend_selection, true);
}

bool TextArea::delete_selection() {
    if (!has_selection()) return false;
    const size_t start = selection_start();
    const size_t end = selection_end();
    text_.erase(start, end - start);
    caret_ = start;
    selection_anchor_ = SIZE_MAX;
    desired_x_ = -1.0f;
    return true;
}

bool TextArea::replace_selection(std::string_view inserted) {
    if (inserted.empty() && !has_selection()) return false;
    delete_selection();
    text_.insert(caret_, inserted.data(), inserted.size());
    caret_ += inserted.size();
    selection_anchor_ = SIZE_MAX;
    desired_x_ = -1.0f;
    return true;
}

void TextArea::emit_changed() {
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, text_);
}


} // namespace termin::gui_native
