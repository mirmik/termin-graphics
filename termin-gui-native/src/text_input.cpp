#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;
TextInput::TextInput(std::string text)
    : NativeWidget("TextInput"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    if (!valid_utf8(text_)) {
        tc_log_error("[termin-gui-native] TextInput rejected invalid UTF-8 initial text");
        text_.clear();
    }
    caret_ = text_.size();
    set_focusable(true);
    update_unmeasured_size();
}

void TextInput::set_text(std::string text) {
    if (!valid_utf8(text)) {
        tc_log_error("[termin-gui-native] TextInput rejected invalid UTF-8 text");
        return;
    }
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    caret_ = utf8_floor_boundary(text_, std::min(caret_, text_.size()));
    clear_selection();
    update_unmeasured_size();
    emit_changed();
}

void TextInput::set_caret(size_t caret) {
    const size_t next = utf8_floor_boundary(text_, caret);
    if (caret_ == next) {
        return;
    }
    caret_ = next;
    clear_selection();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

bool TextInput::has_selection() const {
    return selection_anchor_ != SIZE_MAX && selection_anchor_ != caret_;
}

size_t TextInput::selection_start() const {
    return has_selection() ? std::min(selection_anchor_, caret_) : caret_;
}

size_t TextInput::selection_end() const {
    return has_selection() ? std::max(selection_anchor_, caret_) : caret_;
}

std::string TextInput::selected_text() const {
    return has_selection()
        ? text_.substr(selection_start(), selection_end() - selection_start())
        : std::string {};
}

void TextInput::select(size_t anchor, size_t caret) {
    selection_anchor_ = utf8_floor_boundary(text_, std::min(anchor, text_.size()));
    caret_ = utf8_floor_boundary(text_, std::min(caret, text_.size()));
    if (selection_anchor_ == caret_) {
        selection_anchor_ = SIZE_MAX;
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void TextInput::select_all() {
    select(0, text_.size());
}

void TextInput::clear_selection() {
    if (selection_anchor_ == SIZE_MAX) {
        return;
    }
    selection_anchor_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size TextInput::measure(tc_ui_document* document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    tc_ui_size measured = preferred_size();
    if (measure_text(document, text_, style.font_size, metrics)) {
        measured.width = std::max(
            style.min_width,
            metrics.width + style.padding_left + style.padding_right + style.border_width * 2.0f
        );
        const float line_height = metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
        measured.height = std::max(
            style.min_height,
            line_height + style.padding_top + style.padding_bottom + style.border_width * 2.0f
        );
    }
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void TextInput::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    ensure_caret_visible(document);
}

void TextInput::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    ensure_caret_visible(document);
    const tc_ui_style style = computed_style(document);
    const bool focused = tc_widget_handle_eq(tc_ui_document_focused_widget(document), handle());
    tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rounded_rect(
            context, bounds(), style.corner_radius, style.border, style.border_width);
    }
    const tc_ui_rect text_clip = text_clip_rect(document);
    tc_ui_text_metrics metrics {};
    const bool has_metrics = measure_text(document, text_, style.font_size, metrics);
    const float line_height = has_metrics && metrics.line_height > 0.0f
        ? metrics.line_height
        : style.font_size;
    const float ascent = has_metrics && metrics.ascent > 0.0f ? metrics.ascent : style.font_size;
    const float baseline = text_clip.y + std::max(0.0f, (text_clip.height - line_height) * 0.5f) + ascent;
    const float text_x = text_clip.x - scroll_x_;
    tc_ui_painter_push_clip(context, text_clip);
    if (has_selection()) {
        float selection_x = 0.0f;
        float selection_width = 0.0f;
        measure_prefix(document, selection_start(), style.font_size, selection_x);
        tc_ui_text_metrics selection_metrics {};
        measure_text(
            document,
            std::string_view(text_).substr(
                selection_start(),
                selection_end() - selection_start()
            ),
            style.font_size,
            selection_metrics
        );
        selection_width = selection_metrics.width;
        tc_ui_color selection_color = style.accent;
        selection_color.a *= 0.45f;
        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {
                text_x + selection_x,
                text_clip.y + std::max(0.0f, (text_clip.height - line_height) * 0.5f),
                selection_width,
                line_height
            },
            selection_color
        );
    }
    tc_ui_painter_draw_text(
        context,
        text_.c_str(),
        tc_ui_point {text_x, baseline},
        style.font_size,
        style.foreground
    );
    if (focused) {
        float caret_width = 0.0f;
        measure_prefix(document, caret_, style.font_size, caret_width);
        const float caret_x = text_x + caret_width;
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {caret_x, bounds().y + 7.0f},
            tc_ui_point {caret_x, bounds().y + bounds().height - 7.0f},
            style.accent,
            1.0f
        );
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result TextInput::pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) {
    if (!event) {
        return TC_UI_EVENT_IGNORED;
    }
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        tc_ui_document_set_focus(document, handle());
        const tc_ui_rect clip = text_clip_rect(document);
        const float content_x = std::max(0.0f, event->x - clip.x + scroll_x_);
        const size_t next = caret_from_content_x(document, content_x);
        if ((event->modifiers & TC_UI_MOD_SHIFT) != 0) {
            move_caret(next, true);
        } else {
            caret_ = next;
            selection_anchor_ = caret_;
        }
        selecting_ = true;
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && selecting_) {
        const tc_ui_rect clip = text_clip_rect(document);
        caret_ = caret_from_content_x(document, std::max(0.0f, event->x - clip.x + scroll_x_));
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        ensure_caret_visible(document);
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

tc_ui_event_result TextInput::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) {
        return TC_UI_EVENT_IGNORED;
    }
    const bool extend_selection = (event->modifiers & TC_UI_MOD_SHIFT) != 0;
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
            if (delete_selection()) {
                emit_changed();
            }
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'v')) {
        const char* clipboard = tc_ui_document_clipboard_text(document);
        if (clipboard) {
            const std::string_view inserted(clipboard);
            if (!valid_utf8(inserted)) {
                tc_log_error("[termin-gui-native] TextInput rejected invalid UTF-8 clipboard text");
            } else if (replace_selection(inserted)) {
                update_unmeasured_size();
                emit_changed();
            }
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    }
    switch (event->key) {
    case TC_UI_KEY_BACKSPACE:
        if (delete_selection()) {
            update_unmeasured_size();
            emit_changed();
        } else if (caret_ > 0) {
            const size_t previous = utf8_previous_boundary(text_, caret_);
            text_.erase(previous, caret_ - previous);
            caret_ = previous;
            update_unmeasured_size();
            emit_changed();
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_DELETE:
        if (delete_selection()) {
            update_unmeasured_size();
            emit_changed();
        } else if (caret_ < text_.size()) {
            const size_t next = utf8_next_boundary(text_, caret_);
            text_.erase(caret_, next - caret_);
            update_unmeasured_size();
            emit_changed();
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_LEFT:
        if (!extend_selection && has_selection()) {
            move_caret(selection_start(), false);
        } else {
            move_caret(utf8_previous_boundary(text_, caret_), extend_selection);
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_RIGHT:
        if (!extend_selection && has_selection()) {
            move_caret(selection_end(), false);
        } else {
            move_caret(utf8_next_boundary(text_, caret_), extend_selection);
        }
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_HOME:
        move_caret(0, extend_selection);
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_END:
        move_caret(text_.size(), extend_selection);
        ensure_caret_visible(document);
        return TC_UI_EVENT_HANDLED;
    case TC_UI_KEY_ENTER:
        submitted_.emit(*this, text_);
        return TC_UI_EVENT_HANDLED;
    default:
        return TC_UI_EVENT_IGNORED;
    }
}

tc_ui_event_result TextInput::text_event(tc_ui_document* document, const tc_ui_text_event* event) {
    if (!event || !event->text || event->text[0] == '\0') {
        return TC_UI_EVENT_IGNORED;
    }
    const std::string_view inserted(event->text);
    if (!valid_utf8(inserted)) {
        tc_log_error("[termin-gui-native] TextInput rejected invalid UTF-8 input event");
        return TC_UI_EVENT_IGNORED;
    }
    if (replace_selection(inserted)) {
        update_unmeasured_size();
        emit_changed();
    }
    ensure_caret_visible(document);
    return TC_UI_EVENT_HANDLED;
}

tc_ui_rect TextInput::text_clip_rect(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    return tc_ui_rect {
        bounds().x + style.padding_left,
        bounds().y + style.padding_top,
        std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
        std::max(0.0f, bounds().height - style.padding_top - style.padding_bottom)
    };
}

bool TextInput::measure_prefix(
    tc_ui_document* document,
    size_t byte_offset,
    float font_size,
    float& width
) const {
    tc_ui_text_metrics metrics {};
    const size_t boundary = utf8_floor_boundary(text_, byte_offset);
    if (!measure_text(document, std::string_view(text_).substr(0, boundary), font_size, metrics)) {
        width = 0.0f;
        return false;
    }
    width = metrics.width;
    return true;
}

void TextInput::ensure_caret_visible(tc_ui_document* document) {
    const tc_ui_style style = computed_style(document);
    const float viewport_width = text_clip_rect(document).width;
    float caret_width = 0.0f;
    tc_ui_text_metrics full_metrics {};
    if (viewport_width <= 0.0f ||
        !measure_prefix(document, caret_, style.font_size, caret_width) ||
        !measure_text(document, text_, style.font_size, full_metrics)) {
        scroll_x_ = 0.0f;
        return;
    }
    const float visible_width = std::max(0.0f, viewport_width - 1.0f);
    if (caret_width < scroll_x_) {
        scroll_x_ = caret_width;
    } else if (caret_width > scroll_x_ + visible_width) {
        scroll_x_ = caret_width - visible_width;
    }
    scroll_x_ = clamp_float(scroll_x_, 0.0f, std::max(0.0f, full_metrics.width - visible_width));
}

size_t TextInput::caret_from_content_x(tc_ui_document* document, float content_x) const {
    const tc_ui_style style = computed_style(document);
    size_t current = 0;
    float current_width = 0.0f;
    if (!measure_prefix(document, current, style.font_size, current_width)) {
        return 0;
    }
    while (current < text_.size()) {
        const size_t next = utf8_next_boundary(text_, current);
        float next_width = current_width;
        if (!measure_prefix(document, next, style.font_size, next_width)) {
            return current;
        }
        if (content_x < (current_width + next_width) * 0.5f) {
            return current;
        }
        current = next;
        current_width = next_width;
    }
    return text_.size();
}

void TextInput::update_unmeasured_size() {
    set_preferred_size(tc_ui_size {160.0f, 34.0f});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void TextInput::emit_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, text_);
}

void TextInput::move_caret(size_t next, bool extend_selection) {
    next = utf8_floor_boundary(text_, std::min(next, text_.size()));
    if (extend_selection) {
        if (selection_anchor_ == SIZE_MAX) {
            selection_anchor_ = caret_;
        }
    } else {
        selection_anchor_ = SIZE_MAX;
    }
    caret_ = next;
    if (selection_anchor_ == caret_) {
        selection_anchor_ = SIZE_MAX;
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

bool TextInput::delete_selection() {
    if (!has_selection()) {
        return false;
    }
    const size_t start = selection_start();
    const size_t end = selection_end();
    text_.erase(start, end - start);
    caret_ = start;
    selection_anchor_ = SIZE_MAX;
    return true;
}

bool TextInput::replace_selection(std::string_view inserted) {
    if (inserted.empty() && !has_selection()) {
        return false;
    }
    delete_selection();
    text_.insert(caret_, inserted.data(), inserted.size());
    caret_ += inserted.size();
    selection_anchor_ = SIZE_MAX;
    return true;
}

} // namespace termin::gui_native
