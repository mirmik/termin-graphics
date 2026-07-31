#include "widgets_internal.hpp"

#include <cctype>
#include <limits>

namespace termin::gui_native {
using namespace detail;

namespace {

struct LabelLine {
    std::string text;
    float width = 0.0f;
};

struct LabelTextLayout {
    std::vector<LabelLine> lines;
    float width = 0.0f;
    float line_height = 0.0f;
    float ascent = 0.0f;
    bool truncated = false;
};

float text_width(
    tc_ui_document_handle document,
    std::string_view text,
    float font_size
) {
    tc_ui_text_metrics metrics {};
    return measure_text(document, text, font_size, metrics)
        ? metrics.width
        : static_cast<float>(text.size()) * font_size * 0.5f;
}

std::string ellipsize(
    tc_ui_document_handle document,
    std::string text,
    float font_size,
    float width
) {
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    const float ellipsis_width = text_width(document, ellipsis, font_size);
    if (width <= 0.0f || ellipsis_width > width) {
        return {};
    }
    while (!text.empty() &&
           text_width(document, text, font_size) + ellipsis_width > width) {
        text.resize(utf8_previous_boundary(text, text.size()));
    }
    text.append(ellipsis);
    return text;
}

size_t best_character_break(
    tc_ui_document_handle document,
    std::string_view text,
    float font_size,
    float width
) {
    size_t offset = 0;
    size_t best = 0;
    while (offset < text.size()) {
        const size_t next = utf8_next_boundary(text, offset);
        if (text_width(document, text.substr(0, next), font_size) > width) {
            break;
        }
        best = next;
        offset = next;
    }
    return best;
}

size_t next_word_end(std::string_view text) {
    size_t offset = 0;
    while (offset < text.size()) {
        const size_t next = utf8_next_boundary(text, offset);
        const unsigned char byte = static_cast<unsigned char>(text[offset]);
        if (byte < 0x80 && std::isspace(byte)) {
            break;
        }
        offset = next;
    }
    return offset;
}

void trim_leading_space(std::string_view& text) {
    while (!text.empty()) {
        const unsigned char byte = static_cast<unsigned char>(text.front());
        if (byte >= 0x80 || !std::isspace(byte)) {
            return;
        }
        text.remove_prefix(1);
    }
}

void add_line(
    LabelTextLayout& result,
    tc_ui_document_handle document,
    std::string text,
    float font_size,
    float width,
    TextOverflow overflow,
    bool force_overflow = false
) {
    float measured = text_width(document, text, font_size);
    if ((force_overflow || measured > width) &&
        std::isfinite(width) && overflow == TextOverflow::Ellipsis) {
        text = ellipsize(document, std::move(text), font_size, width);
        measured = text_width(document, text, font_size);
        result.truncated = true;
    }
    result.width = std::max(
        result.width,
        std::isfinite(width) ? std::min(measured, width) : measured);
    result.lines.push_back(LabelLine {std::move(text), measured});
}

void wrap_paragraph(
    LabelTextLayout& result,
    tc_ui_document_handle document,
    std::string_view paragraph,
    float font_size,
    float width,
    TextWrapMode wrap,
    TextOverflow overflow
) {
    if (wrap == TextWrapMode::None || !std::isfinite(width)) {
        add_line(
            result, document, std::string(paragraph), font_size, width,
            overflow);
        return;
    }
    if (paragraph.empty()) {
        add_line(result, document, {}, font_size, width, overflow);
        return;
    }

    while (!paragraph.empty()) {
        if (text_width(document, paragraph, font_size) <= width) {
            add_line(
                result, document, std::string(paragraph), font_size, width,
                overflow);
            break;
        }
        size_t split = best_character_break(
            document, paragraph, font_size, width);
        if (wrap == TextWrapMode::Word) {
            size_t whitespace = std::string_view::npos;
            size_t offset = 0;
            while (offset < split) {
                const unsigned char byte =
                    static_cast<unsigned char>(paragraph[offset]);
                const size_t next = utf8_next_boundary(paragraph, offset);
                if (byte < 0x80 && std::isspace(byte)) {
                    whitespace = offset;
                }
                offset = next;
            }
            if (whitespace != std::string_view::npos && whitespace > 0) {
                split = whitespace;
            } else {
                const size_t word_end = next_word_end(paragraph);
                if (word_end > split) {
                    add_line(
                        result, document,
                        std::string(paragraph.substr(0, word_end)), font_size,
                        width, overflow, true);
                    paragraph.remove_prefix(word_end);
                    trim_leading_space(paragraph);
                    continue;
                }
            }
        }
        if (split == 0) {
            split = utf8_next_boundary(paragraph, 0);
        }
        std::string line(paragraph.substr(0, split));
        while (!line.empty() &&
               std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        add_line(
            result, document, std::move(line), font_size, width, overflow);
        paragraph.remove_prefix(split);
        trim_leading_space(paragraph);
    }
}

LabelTextLayout layout_text(
    tc_ui_document_handle document,
    std::string_view text,
    float font_size,
    float width,
    TextWrapMode wrap,
    TextOverflow overflow,
    size_t line_limit
) {
    LabelTextLayout result;
    tc_ui_text_metrics metrics {};
    if (measure_text(document, "Mg", font_size, metrics)) {
        result.line_height =
            metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
        result.ascent =
            metrics.ascent > 0.0f ? metrics.ascent : result.line_height;
    } else {
        result.line_height = font_size;
        result.ascent = font_size;
    }

    size_t start = 0;
    do {
        const size_t newline = text.find('\n', start);
        const size_t end =
            newline == std::string_view::npos ? text.size() : newline;
        wrap_paragraph(
            result, document, text.substr(start, end - start), font_size,
            width, wrap, overflow);
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    } while (start <= text.size());

    if (line_limit > 0 && result.lines.size() > line_limit) {
        result.lines.resize(line_limit);
        result.truncated = true;
        if (overflow == TextOverflow::Ellipsis && !result.lines.empty()) {
            LabelLine& last = result.lines.back();
            last.text = ellipsize(
                document, std::move(last.text), font_size, width);
            last.width = text_width(document, last.text, font_size);
        }
    }
    result.width = 0.0f;
    for (const LabelLine& line : result.lines) {
        result.width = std::max(
            result.width,
            std::isfinite(width) ? std::min(line.width, width) : line.width);
    }
    return result;
}

} // namespace

Label::Label(std::string text)
    : NativeWidget("Label"), text_(std::move(text)) {
    set_style_role(TC_UI_STYLE_LABEL);
    update_unmeasured_size();
}

Label::Label(std::string text, float font_size)
    : Label(std::move(text)) {
    set_font_size(font_size);
}

Label::Label(std::string text, float font_size, Color color)
    : Label(std::move(text), font_size) {
    set_color(color);
}

Label& Label::set_text(std::string text) {
    text_ = std::move(text);
    update_unmeasured_size();
    mark_dirty(
        TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_LAYOUT |
        TC_WIDGET_DIRTY_PAINT);
    return *this;
}

Label& Label::set_color(Color color) {
    set_style_color(*this, TC_UI_STYLE_FOREGROUND, color.c_color());
    return *this;
}

Label& Label::set_font_size(float font_size) {
    set_style_metric(
        *this, TC_UI_STYLE_FONT_SIZE, std::max(1.0f, font_size));
    update_unmeasured_size();
    return *this;
}

Label& Label::set_wrap_mode(TextWrapMode mode) {
    if (wrap_mode_ != mode) {
        wrap_mode_ = mode;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }
    return *this;
}

Label& Label::set_overflow(TextOverflow overflow) {
    if (overflow_ != overflow) {
        overflow_ = overflow;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }
    return *this;
}

Label& Label::set_max_lines(size_t max_lines) {
    if (max_lines_ != max_lines) {
        max_lines_ = max_lines;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }
    return *this;
}

tc_ui_size Label::measure(
    tc_ui_document_handle document,
    tc_ui_constraints constraints
) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics probe {};
    if (!measure_text(document, text_, style.font_size, probe)) {
        tc_ui_size measured = preferred_size();
        measured.width = std::max(measured.width, min_size().width);
        measured.height = std::max(measured.height, min_size().height);
        return clamp_size(measured, constraints);
    }
    const bool width_definite =
        constraints.max_size.width > 0.0f &&
        constraints.max_size.width < kHuge;
    const float width = width_definite
        ? constraints.max_size.width
        : std::numeric_limits<float>::infinity();
    const LabelTextLayout text_layout = layout_text(
        document, text_, style.font_size, width, wrap_mode_, overflow_,
        max_lines_);
    tc_ui_size measured {
        text_layout.width,
        text_layout.line_height *
            static_cast<float>(text_layout.lines.size())};
    measured.width = std::max(measured.width, min_size().width);
    measured.height = std::max(measured.height, min_size().height);
    return clamp_size(measured, constraints);
}

void Label::paint(
    tc_ui_document_handle document,
    tc_ui_paint_context* context
) {
    const tc_ui_style style = computed_style(document);
    tc_ui_text_metrics metrics {};
    float line_height = style.font_size;
    if (measure_text(document, "Mg", style.font_size, metrics)) {
        line_height =
            metrics.line_height > 0.0f ? metrics.line_height : metrics.height;
    }
    const size_t height_lines = line_height > 0.0f
        ? static_cast<size_t>(std::floor(bounds().height / line_height))
        : 0;
    if (height_lines == 0) {
        return;
    }
    const size_t line_limit = max_lines_ == 0
        ? height_lines
        : std::min(max_lines_, height_lines);
    const LabelTextLayout text_layout = layout_text(
        document, text_, style.font_size, bounds().width, wrap_mode_,
        overflow_, line_limit);
    tc_ui_painter_push_clip(context, bounds());
    for (size_t index = 0; index < text_layout.lines.size(); ++index) {
        const float baseline =
            bounds().y + static_cast<float>(index) * text_layout.line_height +
            text_layout.ascent;
        tc_ui_painter_draw_text(
            context, text_layout.lines[index].text.c_str(),
            tc_ui_point {bounds().x, baseline}, style.font_size,
            style.foreground);
    }
    tc_ui_painter_pop_clip(context);
}

void Label::update_unmeasured_size() {
    const tc_ui_style_override style_override = this->style_override();
    const float font_size =
        (style_override.fields & TC_UI_STYLE_FONT_SIZE) != 0
        ? style_override.value.font_size
        : 15.0f;
    set_preferred_size(tc_ui_size {0.0f, font_size});
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
}

} // namespace termin::gui_native
