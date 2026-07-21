#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

namespace {
std::string numeric_edit_characters(std::string_view text) {
    std::string filtered;
    for (const char ch : text) {
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || ch == 'e' || ch == 'E') {
            filtered.push_back(ch);
        }
    }
    return filtered;
}
} // namespace

SpinBox::SpinBox(float value)
    : NativeWidget("SpinBox") {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size {120.0f, 34.0f});
    set_value(value);
}

bool SpinBox::has_selection() const {
    return selection_anchor_ != SIZE_MAX && selection_anchor_ != caret_;
}

size_t SpinBox::selection_start() const {
    return has_selection() ? std::min(selection_anchor_, caret_) : caret_;
}

size_t SpinBox::selection_end() const {
    return has_selection() ? std::max(selection_anchor_, caret_) : caret_;
}

std::string SpinBox::selected_text() const {
    return has_selection()
        ? edit_text_.substr(selection_start(), selection_end() - selection_start())
        : std::string {};
}

void SpinBox::set_value(float value) {
    if (!std::isfinite(value)) {
        tc_log_error("[termin-gui-native] SpinBox rejected non-finite value");
        return;
    }
    const float next = clamp_float(value, min_value_, max_value_);
    if (std::fabs(next - value_) <= 0.000001f) return;
    value_ = next;
    if (editing_) {
        edit_text_ = formatted_value();
        caret_ = edit_text_.size();
        selection_anchor_ = SIZE_MAX;
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    changed_.emit(*this, value_);
}

void SpinBox::set_range(float min_value, float max_value) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value < min_value) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid range");
        return;
    }
    min_value_ = min_value;
    max_value_ = max_value;
    set_value(value_);
}

void SpinBox::set_step(float step) {
    if (!std::isfinite(step) || step <= 0.0f) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid step");
        return;
    }
    step_ = step;
}

void SpinBox::set_decimals(int decimals) {
    if (decimals < 0 || decimals > 9) {
        tc_log_error("[termin-gui-native] SpinBox rejected decimals outside [0, 9]");
        return;
    }
    decimals_ = decimals;
    if (editing_) {
        edit_text_ = formatted_value();
        caret_ = edit_text_.size();
    }
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
}

std::string SpinBox::formatted_value() const {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals_) << value_;
    return stream.str();
}

void SpinBox::begin_edit() {
    if (editing_) return;
    editing_ = true;
    edit_text_ = formatted_value();
    caret_ = edit_text_.size();
    selection_anchor_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void SpinBox::commit_edit() {
    if (!editing_) return;
    editing_ = false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(edit_text_.c_str(), &end);
    if (errno == ERANGE || end == edit_text_.c_str() || !end || *end != '\0' || !std::isfinite(parsed)) {
        tc_log_error("[termin-gui-native] SpinBox rejected invalid numeric edit '%s'", edit_text_.c_str());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return;
    }
    set_value(parsed);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void SpinBox::cancel_edit() {
    editing_ = false;
    edit_text_.clear();
    caret_ = 0;
    selection_anchor_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_rect SpinBox::up_button_rect() const {
    return tc_ui_rect {
        bounds().x + bounds().width - button_width_,
        bounds().y,
        button_width_,
        bounds().height * 0.5f
    };
}

tc_ui_rect SpinBox::down_button_rect() const {
    tc_ui_rect result = up_button_rect();
    result.y += result.height;
    return result;
}

tc_ui_rect SpinBox::text_clip_rect(tc_ui_document_handle document) const {
    const tc_ui_style style = computed_style(document);
    return tc_ui_rect {
        bounds().x + style.padding_left,
        bounds().y + style.padding_top,
        std::max(0.0f, bounds().width - button_width_ - style.padding_left - 2.0f),
        std::max(0.0f, bounds().height - style.padding_top - style.padding_bottom)
    };
}

float SpinBox::prefix_width(tc_ui_document_handle document, size_t offset, float font_size) const {
    tc_ui_text_metrics metrics {};
    measure_text(document, std::string_view(edit_text_).substr(0, std::min(offset, edit_text_.size())), font_size, metrics);
    return metrics.width;
}

size_t SpinBox::caret_from_content_x(tc_ui_document_handle document, float content_x) const {
    const float font_size = computed_style(document).font_size;
    float current_width = 0.0f;
    for (size_t current = 0; current < edit_text_.size(); ++current) {
        const float next_width = prefix_width(document, current + 1, font_size);
        if (content_x < (current_width + next_width) * 0.5f) return current;
        current_width = next_width;
    }
    return edit_text_.size();
}

void SpinBox::move_caret(size_t next, bool extend_selection) {
    next = std::min(next, edit_text_.size());
    if (extend_selection) {
        if (selection_anchor_ == SIZE_MAX) selection_anchor_ = caret_;
    } else {
        selection_anchor_ = SIZE_MAX;
    }
    caret_ = next;
    if (selection_anchor_ == caret_) selection_anchor_ = SIZE_MAX;
}

bool SpinBox::delete_selection() {
    if (!has_selection()) return false;
    const size_t start = selection_start();
    edit_text_.erase(start, selection_end() - start);
    caret_ = start;
    selection_anchor_ = SIZE_MAX;
    return true;
}

void SpinBox::replace_selection(std::string_view text) {
    delete_selection();
    edit_text_.insert(caret_, text);
    caret_ += text.size();
    selection_anchor_ = SIZE_MAX;
}

tc_ui_size SpinBox::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    tc_ui_size result = preferred_size();
    if (measure_text(document, formatted_value(), style.font_size, metrics)) {
        result.width = std::max(result.width, metrics.width + button_width_ + 20.0f);
        result.height = std::max(result.height, metrics.line_height + 8.0f);
    }
    return clamp_size(result, constraints);
}

void SpinBox::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const std::string display = editing_ ? edit_text_ : formatted_value();
    tc_ui_text_metrics metrics {};
    measure_text(document, display, style.font_size, metrics);
    const float ascent = metrics.ascent > 0.0f ? metrics.ascent : style.font_size;
    const float line_height_value = metrics.line_height > 0.0f ? metrics.line_height : style.font_size;
    tc_ui_painter_fill_rounded_rect(context, bounds(), style.corner_radius, style.background);
    if (style.border_width > 0.0f && color_visible(style.border)) {
        tc_ui_painter_stroke_rounded_rect(
            context, bounds(), style.corner_radius, style.border, style.border_width);
    }
    tc_ui_painter_fill_rect(context, up_button_rect(), style.border);
    tc_ui_painter_fill_rect(context, down_button_rect(), style.border);
    tc_ui_painter_draw_text(
        context, "+",
        tc_ui_point {up_button_rect().x + 5.0f, up_button_rect().y + up_button_rect().height - 3.0f},
        style.font_size, style.foreground
    );
    tc_ui_painter_draw_text(
        context, "-",
        tc_ui_point {down_button_rect().x + 6.0f, down_button_rect().y + down_button_rect().height - 3.0f},
        style.font_size, style.foreground
    );
    const tc_ui_rect clip = text_clip_rect(document);
    const float baseline = clip.y + std::max(0.0f, (clip.height - line_height_value) * 0.5f) + ascent;
    tc_ui_painter_push_clip(context, clip);
    if (editing_ && has_selection()) {
        tc_ui_color selection_color = style.accent;
        selection_color.a *= 0.45f;
        const float start_x = prefix_width(document, selection_start(), style.font_size);
        const float end_x = prefix_width(document, selection_end(), style.font_size);
        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {clip.x + start_x, clip.y, end_x - start_x, clip.height},
            selection_color
        );
    }
    tc_ui_painter_draw_text(context, display.c_str(), tc_ui_point {clip.x, baseline}, style.font_size, style.foreground);
    if (editing_ && tc_widget_handle_eq(tc_ui_document_focused_widget(document), handle())) {
        const float x = clip.x + prefix_width(document, caret_, style.font_size);
        tc_ui_painter_draw_line(context, tc_ui_point {x, clip.y + 3.0f}, tc_ui_point {x, clip.y + clip.height - 3.0f}, style.accent, 1.0f);
    }
    tc_ui_painter_pop_clip(context);
}

tc_ui_event_result SpinBox::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_DOWN && rect_contains(bounds(), event->x, event->y)) {
        tc_ui_document_set_focus(document, handle());
        if (rect_contains(up_button_rect(), event->x, event->y)) {
            set_value(value_ + step_);
        } else if (rect_contains(down_button_rect(), event->x, event->y)) {
            set_value(value_ - step_);
        } else {
            begin_edit();
            const tc_ui_rect clip = text_clip_rect(document);
            const size_t next = caret_from_content_x(document, std::max(0.0f, event->x - clip.x));
            if ((event->modifiers & TC_UI_MOD_SHIFT) != 0) move_caret(next, true);
            else {
                caret_ = next;
                selection_anchor_ = caret_;
            }
            selecting_ = true;
            tc_ui_document_set_pointer_capture(document, handle());
        }
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && selecting_) {
        const tc_ui_rect clip = text_clip_rect(document);
        caret_ = caret_from_content_x(document, std::max(0.0f, event->x - clip.x));
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && selecting_) {
        selecting_ = false;
        tc_ui_document_release_pointer_capture(document, handle());
        if (selection_anchor_ == caret_) selection_anchor_ = SIZE_MAX;
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result SpinBox::key_event(tc_ui_document_handle document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN) return TC_UI_EVENT_IGNORED;
    if (event->key == TC_UI_KEY_UP_ARROW) {
        set_value(value_ + step_);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->key == TC_UI_KEY_DOWN_ARROW) {
        set_value(value_ - step_);
        return TC_UI_EVENT_HANDLED;
    }
    if (!editing_) return TC_UI_EVENT_IGNORED;
    const bool extend = (event->modifiers & TC_UI_MOD_SHIFT) != 0;
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'a')) {
        selection_anchor_ = 0;
        caret_ = edit_text_.size();
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
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
            delete_selection();
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'v')) {
        const char* clipboard = tc_ui_document_clipboard_text(document);
        if (clipboard) {
            const std::string filtered = numeric_edit_characters(clipboard);
            if (!filtered.empty()) {
                replace_selection(filtered);
                mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    switch (event->key) {
    case TC_UI_KEY_LEFT:
        if (!extend && has_selection()) move_caret(selection_start(), false);
        else move_caret(caret_ > 0 ? caret_ - 1 : 0, extend);
        break;
    case TC_UI_KEY_RIGHT:
        if (!extend && has_selection()) move_caret(selection_end(), false);
        else move_caret(std::min(caret_ + 1, edit_text_.size()), extend);
        break;
    case TC_UI_KEY_HOME: move_caret(0, extend); break;
    case TC_UI_KEY_END: move_caret(edit_text_.size(), extend); break;
    case TC_UI_KEY_BACKSPACE:
        if (!delete_selection() && caret_ > 0) edit_text_.erase(--caret_, 1);
        break;
    case TC_UI_KEY_DELETE:
        if (!delete_selection() && caret_ < edit_text_.size()) edit_text_.erase(caret_, 1);
        break;
    case TC_UI_KEY_ENTER: commit_edit(); break;
    case TC_UI_KEY_ESCAPE: cancel_edit(); break;
    default: return TC_UI_EVENT_IGNORED;
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result SpinBox::text_event(tc_ui_document_handle, const tc_ui_text_event* event) {
    if (!event || !event->text || event->text[0] == '\0') return TC_UI_EVENT_IGNORED;
    if (!editing_) begin_edit();
    const std::string filtered = numeric_edit_characters(event->text);
    if (!filtered.empty()) {
        replace_selection(filtered);
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }
    return TC_UI_EVENT_HANDLED;
}

void SpinBox::focus_event(tc_ui_document_handle, bool focused) {
    if (!focused) commit_edit();
}


} // namespace termin::gui_native
