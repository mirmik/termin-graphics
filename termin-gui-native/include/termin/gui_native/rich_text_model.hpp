#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

struct RichTextStyle {
    std::optional<tc_ui_color> color;
    bool bold = false;
    bool italic = false;
};

struct RichTextSegment {
    std::string text;
    RichTextStyle style;
};

using RichTextLine = std::vector<RichTextSegment>;

class RichTextModel {
  public:
    RichTextModel();

    const std::vector<RichTextLine>& lines() const { return lines_; }
    const std::string& text() const { return text_; }
    uint64_t revision() const { return revision_; }

    void set_text(std::string text);
    void set_lines(std::vector<RichTextLine> lines);
    void set_html(std::string_view html);
    void clear();

    Signal<RichTextModel&>& changed() { return changed_; }

  private:
    static void validate_lines(const std::vector<RichTextLine>& lines);
    void replace_lines(std::vector<RichTextLine> lines);

    std::vector<RichTextLine> lines_;
    std::string text_;
    uint64_t revision_ = 1;
    Signal<RichTextModel&> changed_;
};

} // namespace termin::gui_native
