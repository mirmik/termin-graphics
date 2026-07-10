#include <termin/gui_native/rich_text_view.hpp>

#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {
namespace {

using detail::clamp_float;
using detail::clamp_size;
using detail::command_modifier;
using detail::key_matches_ascii;
using detail::rect_contains;
using detail::utf8_floor_boundary;
using detail::utf8_next_boundary;

bool color_equal(const tc_ui_color& lhs, const tc_ui_color& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool style_equal(const RichTextStyle& lhs, const RichTextStyle& rhs) {
    if (lhs.bold != rhs.bold || lhs.italic != rhs.italic ||
        lhs.color.has_value() != rhs.color.has_value()) {
        return false;
    }
    return !lhs.color || color_equal(*lhs.color, *rhs.color);
}

bool is_wrap_space(std::string_view codepoint) { return codepoint == " " || codepoint == "\t"; }

struct TextUnit {
    std::string text;
    RichTextStyle style;
    size_t source_start = 0;
    size_t source_end = 0;
    float width = 0.0f;
    bool wrap_space = false;
};

} // namespace

RichTextView::RichTextView(std::shared_ptr<RichTextModel> model)
    : NativeWidget("RichTextView"),
      model_(model ? std::move(model) : std::make_shared<RichTextModel>()) {
    set_style_role(TC_UI_STYLE_TEXT_INPUT);
    set_focusable(true);
    set_preferred_size(tc_ui_size{300.0f, 150.0f});
    connect_model();
}

RichTextView::~RichTextView() { disconnect_model(); }

void RichTextView::connect_model() {
    if (!model_ || model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect([this](RichTextModel&) { on_model_changed(); });
}

void RichTextView::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void RichTextView::on_model_changed() {
    selection_anchor_ = SIZE_MAX;
    selection_cursor_ = 0;
    scroll_y_ = 0.0f;
    invalidate_visual_rows();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::invalidate_visual_rows() {
    cached_revision_ = 0;
    cached_width_ = -1.0f;
    cached_font_size_ = -1.0f;
    rows_.clear();
}

void RichTextView::set_model(std::shared_ptr<RichTextModel> model) {
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<RichTextModel>();
    model_connection_ = 0;
    connect_model();
    on_model_changed();
}

void RichTextView::set_placeholder(std::string placeholder) {
    if (!detail::valid_utf8(placeholder)) {
        tc_log_error("[termin-gui-native] RichTextView rejected invalid UTF-8 placeholder");
        throw std::invalid_argument("placeholder must be valid UTF-8");
    }
    if (placeholder_ == placeholder)
        return;
    placeholder_ = std::move(placeholder);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::set_word_wrap(bool enabled) {
    if (word_wrap_ == enabled)
        return;
    word_wrap_ = enabled;
    scroll_y_ = 0.0f;
    clear_selection();
    invalidate_visual_rows();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::set_show_scrollbar(bool enabled) {
    if (show_scrollbar_ == enabled)
        return;
    show_scrollbar_ = enabled;
    invalidate_visual_rows();
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::set_line_height(float height) {
    if (!std::isfinite(height) || height < 0.0f) {
        tc_log_error("[termin-gui-native] RichTextView rejected invalid line height");
        throw std::invalid_argument("line height must be finite and non-negative");
    }
    if (line_height_ == height)
        return;
    line_height_ = height;
    cached_line_height_ = 0.0f;
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::set_scroll_y(float offset) {
    if (!std::isfinite(offset)) {
        tc_log_error("[termin-gui-native] RichTextView rejected non-finite scroll offset");
        throw std::invalid_argument("scroll offset must be finite");
    }
    const float before = scroll_y_;
    scroll_y_ = std::max(0.0f, offset);
    if (scroll_y_ != before)
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

bool RichTextView::has_selection() const {
    return selection_anchor_ != SIZE_MAX && selection_anchor_ != selection_cursor_;
}

size_t RichTextView::selection_start() const {
    return has_selection() ? std::min(selection_anchor_, selection_cursor_) : selection_cursor_;
}

size_t RichTextView::selection_end() const {
    return has_selection() ? std::max(selection_anchor_, selection_cursor_) : selection_cursor_;
}

std::string RichTextView::selected_text() const {
    if (!has_selection() || !model_)
        return {};
    return model_->text().substr(selection_start(), selection_end() - selection_start());
}

void RichTextView::select(size_t anchor, size_t cursor) {
    const std::string& text = model_->text();
    selection_anchor_ = utf8_floor_boundary(text, std::min(anchor, text.size()));
    selection_cursor_ = utf8_floor_boundary(text, std::min(cursor, text.size()));
    if (selection_anchor_ == selection_cursor_)
        selection_anchor_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void RichTextView::select_all() {
    if (!model_ || model_->text().empty()) {
        clear_selection();
        return;
    }
    select(0, model_->text().size());
}

void RichTextView::clear_selection() {
    if (selection_anchor_ == SIZE_MAX)
        return;
    selection_anchor_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_rect RichTextView::content_rect(tc_ui_document* document) const {
    const tc_ui_style style = computed_style(document);
    return tc_ui_rect{bounds().x + style.padding_left, bounds().y + style.padding_top,
                      std::max(0.0f, bounds().width - style.padding_left - style.padding_right),
                      std::max(0.0f, bounds().height - style.padding_top - style.padding_bottom)};
}

float RichTextView::effective_line_height(tc_ui_document* document) const {
    if (line_height_ > 0.0f)
        return line_height_;
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics{};
    if (detail::measure_text(document, "Mg", style.font_size, metrics)) {
        return metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
    }
    return style.font_size * 1.4f;
}

float RichTextView::measure_width(tc_ui_document* document, std::string_view text,
                                  float font_size) const {
    if (text.empty())
        return 0.0f;
    tc_ui_text_metrics metrics{};
    if (detail::measure_text(document, text, font_size, metrics))
        return metrics.width;
    size_t count = 0;
    for (size_t offset = 0; offset < text.size(); offset = utf8_next_boundary(text, offset))
        ++count;
    return static_cast<float>(count) * font_size * 0.6f;
}

void RichTextView::append_run(VisualRow& row, std::string text, const RichTextStyle& style,
                              size_t source_start, size_t source_end, float width) {
    if (text.empty())
        return;
    if (row.runs.empty())
        row.source_start = source_start;
    if (!row.runs.empty() && row.runs.back().source_end == source_start &&
        style_equal(row.runs.back().style, style)) {
        VisualRun& run = row.runs.back();
        run.text += text;
        run.source_end = source_end;
        run.width += width;
    } else {
        row.runs.push_back(VisualRun{std::move(text), style, source_start, source_end, width});
    }
    row.source_end = source_end;
    row.width += width;
}

void RichTextView::ensure_visual_rows(tc_ui_document* document, float width, float font_size) {
    width = std::max(0.0f, width);
    if (cached_revision_ == model_->revision() && cached_width_ == width &&
        cached_font_size_ == font_size) {
        return;
    }
    rows_.clear();
    size_t global_offset = 0;
    for (size_t line_index = 0; line_index < model_->lines().size(); ++line_index) {
        const RichTextLine& line = model_->lines()[line_index];
        std::vector<TextUnit> units;
        for (const RichTextSegment& segment : line) {
            size_t local_offset = 0;
            while (local_offset < segment.text.size()) {
                const size_t next = utf8_next_boundary(segment.text, local_offset);
                const std::string codepoint =
                    segment.text.substr(local_offset, next - local_offset);
                units.push_back(TextUnit{
                    codepoint, segment.style, global_offset + local_offset, global_offset + next,
                    measure_width(document, codepoint, font_size), is_wrap_space(codepoint)});
                local_offset = next;
            }
            global_offset += segment.text.size();
        }

        VisualRow current;
        current.source_start = global_offset;
        current.source_end = global_offset;
        auto flush = [&]() {
            rows_.push_back(std::move(current));
            current = VisualRow{};
        };
        if (units.empty()) {
            rows_.push_back(current);
        } else if (!word_wrap_ || width <= 0.0f) {
            for (const TextUnit& unit : units) {
                append_run(current, unit.text, unit.style, unit.source_start, unit.source_end,
                           unit.width);
            }
            flush();
        } else {
            size_t token_start = 0;
            while (token_start < units.size()) {
                size_t token_end = token_start;
                float token_width = 0.0f;
                do {
                    token_width += units[token_end].width;
                    ++token_end;
                } while (token_end < units.size() && !units[token_end - 1].wrap_space);

                if (!current.runs.empty() && current.width + token_width > width)
                    flush();
                if (token_width <= width) {
                    for (size_t index = token_start; index < token_end; ++index) {
                        const TextUnit& unit = units[index];
                        append_run(current, unit.text, unit.style, unit.source_start,
                                   unit.source_end, unit.width);
                    }
                } else {
                    for (size_t index = token_start; index < token_end; ++index) {
                        const TextUnit& unit = units[index];
                        if (!current.runs.empty() && current.width + unit.width > width)
                            flush();
                        append_run(current, unit.text, unit.style, unit.source_start,
                                   unit.source_end, unit.width);
                    }
                }
                token_start = token_end;
            }
            flush();
        }
        if (line_index + 1 < model_->lines().size())
            ++global_offset;
    }
    if (rows_.empty())
        rows_.push_back(VisualRow{});
    cached_revision_ = model_->revision();
    cached_width_ = width;
    cached_font_size_ = font_size;
}

void RichTextView::clamp_scroll(float viewport_height) {
    scroll_y_ = clamp_float(scroll_y_, 0.0f, std::max(0.0f, content_height() - viewport_height));
}

tc_ui_rect RichTextView::prepare_layout(tc_ui_document* document, const tc_ui_style& style) {
    tc_ui_rect content = content_rect(document);
    cached_line_height_ = effective_line_height(document);
    ensure_visual_rows(document, content.width, style.font_size);
    if (show_scrollbar_ && content_height() > content.height) {
        ensure_visual_rows(document, std::max(0.0f, content.width - scrollbar_width_),
                           style.font_size);
        content.width = std::max(0.0f, content.width - scrollbar_width_);
    }
    clamp_scroll(content.height);
    return content;
}

RichTextView::ScrollbarGeometry RichTextView::scrollbar_geometry(tc_ui_rect content) const {
    ScrollbarGeometry result;
    result.max_scroll = std::max(0.0f, content_height() - content.height);
    result.visible = show_scrollbar_ && result.max_scroll > 0.0f && content.height > 0.0f;
    if (!result.visible)
        return result;
    result.track =
        tc_ui_rect{content.x + content.width, content.y, scrollbar_width_, content.height};
    const float ratio = std::min(1.0f, content.height / content_height());
    const float thumb_height =
        std::min(content.height, std::max(minimum_thumb_height_, content.height * ratio));
    const float travel = content.height - thumb_height;
    result.thumb = tc_ui_rect{
        result.track.x,
        content.y + (result.max_scroll > 0.0f ? travel * scroll_y_ / result.max_scroll : 0.0f),
        scrollbar_width_, thumb_height};
    return result;
}

float RichTextView::row_x_for_offset(tc_ui_document* document, const VisualRow& row, size_t offset,
                                     float font_size) const {
    float x = 0.0f;
    for (const VisualRun& run : row.runs) {
        if (offset >= run.source_end) {
            x += run.width;
            continue;
        }
        if (offset <= run.source_start)
            break;
        const size_t local = utf8_floor_boundary(run.text, offset - run.source_start);
        x += measure_width(document, std::string_view(run.text).substr(0, local), font_size);
        break;
    }
    return x;
}

size_t RichTextView::source_offset_from_point(tc_ui_document* document, tc_ui_rect content, float x,
                                              float y) const {
    if (rows_.empty())
        return 0;
    const float visual_y = y - content.y + scroll_y_;
    const size_t row_index =
        std::min(rows_.size() - 1,
                 static_cast<size_t>(std::max(0.0f, std::floor(visual_y / cached_line_height_))));
    const VisualRow& row = rows_[row_index];
    float relative_x = x - content.x;
    float cursor_x = 0.0f;
    for (const VisualRun& run : row.runs) {
        size_t local = 0;
        while (local < run.text.size()) {
            const size_t next = utf8_next_boundary(run.text, local);
            const float width =
                measure_width(document, std::string_view(run.text).substr(local, next - local),
                              cached_font_size_);
            if (relative_x < cursor_x + width * 0.5f)
                return run.source_start + local;
            cursor_x += width;
            local = next;
        }
    }
    return row.source_end;
}

tc_ui_size RichTextView::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return clamp_size(preferred_size(), constraints);
}

void RichTextView::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    const tc_ui_style style = computed_style(document);
    prepare_layout(document, style);
}

void RichTextView::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rounded_rect(context, bounds(), 3.0f, style.background);
    if (style.border_width > 0.0f) {
        tc_ui_painter_stroke_rounded_rect(context, bounds(), 3.0f, style.border,
                                          style.border_width);
    }
    const tc_ui_rect content = prepare_layout(document, style);
    tc_ui_painter_push_clip(context, content);
    if (model_->text().empty() && !placeholder_.empty()) {
        tc_ui_color placeholder_color = style.foreground;
        placeholder_color.a *= 0.55f;
        tc_ui_painter_draw_text(context, placeholder_.c_str(),
                                tc_ui_point{content.x, content.y + style.font_size},
                                style.font_size, placeholder_color);
    } else {
        const size_t first = std::min(
            rows_.size(),
            static_cast<size_t>(std::max(0.0f, std::floor(scroll_y_ / cached_line_height_))));
        const size_t last = std::min(
            rows_.size(),
            static_cast<size_t>(std::ceil((scroll_y_ + content.height) / cached_line_height_)) + 1);
        for (size_t row_index = first; row_index < last; ++row_index) {
            const VisualRow& row = rows_[row_index];
            const float row_top =
                content.y + static_cast<float>(row_index) * cached_line_height_ - scroll_y_;
            if (has_selection()) {
                const size_t start = std::max(selection_start(), row.source_start);
                const size_t end = std::min(selection_end(), row.source_end);
                if (end > start) {
                    const float x0 = row_x_for_offset(document, row, start, style.font_size);
                    const float x1 = row_x_for_offset(document, row, end, style.font_size);
                    tc_ui_color selection = style.accent;
                    selection.a = std::min(selection.a, 0.55f);
                    tc_ui_painter_fill_rect(
                        context, tc_ui_rect{content.x + x0, row_top, x1 - x0, cached_line_height_},
                        selection);
                }
            }
            float draw_x = content.x;
            for (const VisualRun& run : row.runs) {
                const tc_ui_color color = run.style.color.value_or(style.foreground);
                const float italic_offset = run.style.italic ? style.font_size * 0.12f : 0.0f;
                tc_ui_painter_draw_text(
                    context, run.text.c_str(),
                    tc_ui_point{draw_x + italic_offset, row_top + style.font_size}, style.font_size,
                    color);
                if (run.style.bold) {
                    tc_ui_painter_draw_text(
                        context, run.text.c_str(),
                        tc_ui_point{draw_x + italic_offset + 0.7f, row_top + style.font_size},
                        style.font_size, color);
                }
                draw_x += run.width;
            }
        }
    }
    tc_ui_painter_pop_clip(context);

    const ScrollbarGeometry scrollbar = scrollbar_geometry(content);
    if (scrollbar.visible) {
        tc_ui_color color = style.border;
        color.a = std::max(color.a, 0.75f);
        tc_ui_painter_fill_rounded_rect(context, scrollbar.thumb, scrollbar_width_ * 0.5f, color);
    }
}

tc_ui_event_result RichTextView::pointer_event(tc_ui_document* document,
                                               const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect content = prepare_layout(document, style);
    const ScrollbarGeometry scrollbar = scrollbar_geometry(content);
    if (event->type == TC_UI_POINTER_WHEEL && rect_contains(bounds(), event->x, event->y)) {
        if (scrollbar.max_scroll <= 0.0f)
            return TC_UI_EVENT_IGNORED;
        set_scroll_y(scroll_y_ - event->wheel_y * cached_line_height_ * 3.0f);
        clamp_scroll(content.height);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN && event->button == 0 &&
        rect_contains(bounds(), event->x, event->y)) {
        if (scrollbar.visible && rect_contains(scrollbar.track, event->x, event->y)) {
            dragging_scrollbar_ = true;
            drag_start_y_ = event->y;
            drag_start_scroll_ = scroll_y_;
        } else {
            const size_t offset = source_offset_from_point(document, content, event->x, event->y);
            if ((event->modifiers & TC_UI_MOD_SHIFT) != 0) {
                if (selection_anchor_ == SIZE_MAX)
                    selection_anchor_ = selection_cursor_;
                selection_cursor_ = offset;
            } else {
                selection_anchor_ = offset;
                selection_cursor_ = offset;
            }
            selecting_ = true;
        }
        tc_ui_document_set_pointer_capture(document, handle());
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && dragging_scrollbar_) {
        const float travel = scrollbar.track.height - scrollbar.thumb.height;
        if (travel > 0.0f) {
            set_scroll_y(drag_start_scroll_ +
                         (event->y - drag_start_y_) * scrollbar.max_scroll / travel);
            clamp_scroll(content.height);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && selecting_) {
        selection_cursor_ = source_offset_from_point(document, content, event->x, event->y);
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && (selecting_ || dragging_scrollbar_)) {
        selecting_ = false;
        dragging_scrollbar_ = false;
        tc_ui_document_release_pointer_capture(document, handle());
        if (selection_anchor_ == selection_cursor_)
            clear_selection();
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result RichTextView::key_event(tc_ui_document* document, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN)
        return TC_UI_EVENT_IGNORED;
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'a')) {
        select_all();
        return TC_UI_EVENT_HANDLED;
    }
    if (command_modifier(event->modifiers) && key_matches_ascii(event->key, 'c')) {
        if (has_selection()) {
            const std::string selected = selected_text();
            tc_ui_document_set_clipboard_text(document, selected.data(), selected.size());
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

} // namespace termin::gui_native
