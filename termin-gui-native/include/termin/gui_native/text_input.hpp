#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class TextInput : public NativeWidget {
private:
    std::string text_;
    size_t caret_ = 0;
    size_t selection_anchor_ = SIZE_MAX;
    bool selecting_ = false;
    float scroll_x_ = 0.0f;
    Signal<TextInput&, const std::string&> changed_;
    Signal<TextInput&, const std::string&> submitted_;

public:
    explicit TextInput(std::string text = {});
    const std::string& text() const { return text_; }
    size_t caret() const { return caret_; }
    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    float scroll_x() const { return scroll_x_; }
    void set_text(std::string text);
    void set_caret(size_t caret);
    void select(size_t anchor, size_t caret);
    void select_all();
    void clear_selection();
    Signal<TextInput&, const std::string&>& changed() { return changed_; }
    const Signal<TextInput&, const std::string&>& changed() const { return changed_; }
    Signal<TextInput&, const std::string&>& submitted() { return submitted_; }
    const Signal<TextInput&, const std::string&>& submitted() const { return submitted_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;
private:
    tc_ui_rect text_clip_rect(tc_ui_document* document) const;
    bool measure_prefix(tc_ui_document* document, size_t byte_offset, float font_size, float& width) const;
    void ensure_caret_visible(tc_ui_document* document);
    size_t caret_from_content_x(tc_ui_document* document, float content_x) const;
    void update_unmeasured_size();
    void emit_changed();
    void move_caret(size_t next, bool extend_selection);
    bool delete_selection();
    bool replace_selection(std::string_view inserted);
};
} // namespace termin::gui_native
