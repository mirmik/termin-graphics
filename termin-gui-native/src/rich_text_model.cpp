#include <termin/gui_native/rich_text_model.hpp>

#include "widgets_internal.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace termin::gui_native {
namespace {

using detail::valid_utf8;

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

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    for (char& character : result) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<tc_ui_color> parse_css_color(std::string_view value) {
    value = trim(value);
    if (value.size() != 4 && value.size() != 7) {
        return std::nullopt;
    }
    if (value.front() != '#') {
        return std::nullopt;
    }
    std::string digits;
    if (value.size() == 4) {
        digits.reserve(6);
        for (size_t index = 1; index < value.size(); ++index) {
            digits.push_back(value[index]);
            digits.push_back(value[index]);
        }
    } else {
        digits.assign(value.substr(1));
    }
    unsigned int packed = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), packed, 16);
    if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size()) {
        tc_log_error("[termin-gui-native] RichTextModel rejected invalid CSS color '%.*s'",
                     static_cast<int>(value.size()), value.data());
        return std::nullopt;
    }
    return tc_ui_color{static_cast<float>((packed >> 16) & 0xffu) / 255.0f,
                       static_cast<float>((packed >> 8) & 0xffu) / 255.0f,
                       static_cast<float>(packed & 0xffu) / 255.0f, 1.0f};
}

RichTextStyle parse_style_attribute(std::string_view declaration, RichTextStyle style) {
    size_t offset = 0;
    while (offset <= declaration.size()) {
        const size_t separator = declaration.find(';', offset);
        std::string_view part = declaration.substr(offset, separator == std::string_view::npos
                                                               ? declaration.size() - offset
                                                               : separator - offset);
        const size_t colon = part.find(':');
        if (colon != std::string_view::npos) {
            const std::string key = lower_ascii(trim(part.substr(0, colon)));
            const std::string value = lower_ascii(trim(part.substr(colon + 1)));
            if (key == "color") {
                if (auto color = parse_css_color(value)) {
                    style.color = color;
                }
            } else if (key == "font-weight") {
                style.bold = value == "bold" || value == "600" || value == "700" ||
                             value == "800" || value == "900";
            } else if (key == "font-style") {
                style.italic = value == "italic";
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        offset = separator + 1;
    }
    return style;
}

std::optional<std::string_view> find_style_attribute(std::string_view tag) {
    const std::string lower = lower_ascii(tag);
    size_t found = lower.find("style");
    while (found != std::string::npos) {
        size_t cursor = found + 5;
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
            ++cursor;
        }
        if (cursor < tag.size() && tag[cursor] == '=') {
            ++cursor;
            while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
                ++cursor;
            }
            if (cursor >= tag.size()) {
                return std::nullopt;
            }
            const char quote = tag[cursor];
            if (quote == '\'' || quote == '"') {
                const size_t end = tag.find(quote, cursor + 1);
                if (end != std::string_view::npos) {
                    return tag.substr(cursor + 1, end - cursor - 1);
                }
                return std::nullopt;
            }
            const size_t end = tag.find_first_of(" \t\r\n", cursor);
            return tag.substr(cursor,
                              end == std::string_view::npos ? tag.size() - cursor : end - cursor);
        }
        found = lower.find("style", found + 5);
    }
    return std::nullopt;
}

void append_utf8_codepoint(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7fu) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

std::string decode_html_entities(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        if (text[offset] != '&') {
            output.push_back(text[offset++]);
            continue;
        }
        const size_t semicolon = text.find(';', offset + 1);
        if (semicolon == std::string_view::npos) {
            output.push_back(text[offset++]);
            continue;
        }
        const std::string_view entity = text.substr(offset + 1, semicolon - offset - 1);
        if (entity == "amp")
            output.push_back('&');
        else if (entity == "lt")
            output.push_back('<');
        else if (entity == "gt")
            output.push_back('>');
        else if (entity == "quot")
            output.push_back('"');
        else if (entity == "apos" || entity == "#39")
            output.push_back('\'');
        else if (!entity.empty() && entity.front() == '#') {
            uint32_t codepoint = 0;
            const bool hexadecimal = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string_view digits = entity.substr(hexadecimal ? 2 : 1);
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(),
                                                codepoint, hexadecimal ? 16 : 10);
            if (digits.empty() || parsed.ec != std::errc() ||
                parsed.ptr != digits.data() + digits.size() || codepoint > 0x10ffffu ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
                output.append(text.substr(offset, semicolon - offset + 1));
            } else {
                append_utf8_codepoint(output, codepoint);
            }
        } else {
            output.append(text.substr(offset, semicolon - offset + 1));
        }
        offset = semicolon + 1;
    }
    return output;
}

class RichHtmlParser {
  private:
    std::vector<RichTextLine> lines_{RichTextLine{}};
    std::vector<RichTextStyle> styles_{RichTextStyle{}};

  public:
    std::vector<RichTextLine> parse(std::string_view html) {
        size_t offset = 0;
        while (offset < html.size()) {
            const size_t open = html.find('<', offset);
            if (open == std::string_view::npos) {
                append_text(html.substr(offset));
                break;
            }
            append_text(html.substr(offset, open - offset));
            const size_t close = html.find('>', open + 1);
            if (close == std::string_view::npos) {
                append_text(html.substr(open));
                break;
            }
            handle_tag(html.substr(open + 1, close - open - 1));
            offset = close + 1;
        }
        return std::move(lines_);
    }

  private:
    void append_text(std::string_view encoded) {
        std::string text = decode_html_entities(encoded);
        if (text.empty())
            return;
        size_t offset = 0;
        while (offset <= text.size()) {
            const size_t newline_position = text.find('\n', offset);
            size_t end = newline_position == std::string::npos ? text.size() : newline_position;
            if (end > offset && text[end - 1] == '\r')
                --end;
            if (end > offset) {
                std::string part = text.substr(offset, end - offset);
                RichTextLine& line = lines_.back();
                if (!line.empty() && style_equal(line.back().style, styles_.back())) {
                    line.back().text += part;
                } else {
                    line.push_back(RichTextSegment{std::move(part), styles_.back()});
                }
            }
            if (newline_position == std::string::npos)
                break;
            newline();
            offset = newline_position + 1;
        }
    }

    void newline() { lines_.emplace_back(); }

    void handle_tag(std::string_view raw_tag) {
        raw_tag = trim(raw_tag);
        if (raw_tag.empty() || (raw_tag.size() >= 3 && raw_tag.substr(0, 3) == "!--"))
            return;
        const bool closing = raw_tag.front() == '/';
        if (closing)
            raw_tag = trim(raw_tag.substr(1));
        const size_t name_end = raw_tag.find_first_of(" \t\r\n/");
        const std::string name = lower_ascii(raw_tag.substr(0, name_end));
        if (closing) {
            if (name != "br" && styles_.size() > 1)
                styles_.pop_back();
            if (name == "p")
                newline();
            return;
        }
        if (name == "br") {
            newline();
            return;
        }
        RichTextStyle style = styles_.back();
        if (name == "b" || name == "strong")
            style.bold = true;
        else if (name == "i" || name == "em")
            style.italic = true;
        else if (name == "span") {
            if (auto declaration = find_style_attribute(raw_tag)) {
                style = parse_style_attribute(*declaration, style);
            }
        }
        styles_.push_back(style);
    }

};

} // namespace

RichTextModel::RichTextModel() : lines_{RichTextLine{}} {}

void RichTextModel::validate_lines(const std::vector<RichTextLine>& lines) {
    for (const RichTextLine& line : lines) {
        for (const RichTextSegment& segment : line) {
            if (!valid_utf8(segment.text) || segment.text.find('\n') != std::string::npos ||
                segment.text.find('\r') != std::string::npos) {
                tc_log_error("[termin-gui-native] RichTextModel rejected invalid segment text");
                throw std::invalid_argument(
                    "rich text segments must be valid UTF-8 without newlines");
            }
            if (segment.style.color) {
                const tc_ui_color color = *segment.style.color;
                if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                    !std::isfinite(color.a)) {
                    tc_log_error("[termin-gui-native] RichTextModel rejected non-finite color");
                    throw std::invalid_argument("rich text colors must be finite");
                }
            }
        }
    }
}

void RichTextModel::replace_lines(std::vector<RichTextLine> lines) {
    if (lines.empty())
        lines.emplace_back();
    validate_lines(lines);
    std::string text;
    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        if (line_index > 0)
            text.push_back('\n');
        for (const RichTextSegment& segment : lines[line_index])
            text += segment.text;
    }
    lines_ = std::move(lines);
    text_ = std::move(text);
    ++revision_;
    changed_.emit(*this);
}

void RichTextModel::set_text(std::string text) {
    if (!valid_utf8(text)) {
        tc_log_error("[termin-gui-native] RichTextModel rejected invalid UTF-8 text");
        throw std::invalid_argument("rich text must be valid UTF-8");
    }
    std::vector<RichTextLine> lines;
    size_t offset = 0;
    while (offset <= text.size()) {
        const size_t newline = text.find('\n', offset);
        const size_t end = newline == std::string::npos ? text.size() : newline;
        RichTextLine line;
        if (end > offset)
            line.push_back(RichTextSegment{text.substr(offset, end - offset), {}});
        lines.push_back(std::move(line));
        if (newline == std::string::npos)
            break;
        offset = newline + 1;
    }
    replace_lines(std::move(lines));
}

void RichTextModel::set_lines(std::vector<RichTextLine> lines) { replace_lines(std::move(lines)); }

void RichTextModel::set_html(std::string_view html) {
    RichHtmlParser parser;
    replace_lines(parser.parse(html));
}

void RichTextModel::clear() { replace_lines({RichTextLine{}}); }

} // namespace termin::gui_native
