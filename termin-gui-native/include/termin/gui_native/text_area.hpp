#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class TextArea : public NativeWidget {
private:
    struct Line { size_t start; size_t end; };
    std::string text_;
    size_t caret_ = 0;
    size_t selection_anchor_ = SIZE_MAX;
    bool selecting_ = false;
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float desired_x_ = -1.0f;
    Signal<TextArea&, const std::string&> changed_;

public:
    explicit TextArea(std::string text = {});
    const std::string& text() const { return text_; }
    size_t caret() const { return caret_; }
    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    float scroll_x() const { return scroll_x_; }
    float scroll_y() const { return scroll_y_; }
    void set_text(std::string text);
    void set_caret(size_t caret);
    void select(size_t anchor, size_t caret);
    void select_all();
    void clear_selection();
    Signal<TextArea&, const std::string&>& changed() { return changed_; }
    const Signal<TextArea&, const std::string&>& changed() const { return changed_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;
private:
    std::vector<Line> lines() const;
    tc_ui_rect text_clip_rect(tc_ui_document* document) const;
    float line_height(tc_ui_document* document) const;
    bool measure_range(tc_ui_document* document, size_t start, size_t end, float font_size, float& width) const;
    size_t line_index_for_offset(const std::vector<Line>& lines, size_t offset) const;
    size_t caret_from_point(tc_ui_document* document, float x, float y) const;
    size_t caret_from_line_x(tc_ui_document* document, const Line& line, float content_x) const;
    void ensure_caret_visible(tc_ui_document* document);
    void move_caret(size_t next, bool extend_selection, bool preserve_column = false);
    void move_vertical(tc_ui_document* document, int direction, bool extend_selection);
    bool delete_selection();
    bool replace_selection(std::string_view inserted);
    void emit_changed();
};
} // namespace termin::gui_native
