#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class SpinBox : public NativeWidget {
public:
    explicit SpinBox(float value = 0.0f);
    float value() const { return value_; }
    float min_value() const { return min_value_; }
    float max_value() const { return max_value_; }
    float step() const { return step_; }
    int decimals() const { return decimals_; }
    bool editing() const { return editing_; }
    const std::string& edit_text() const { return edit_text_; }
    size_t caret() const { return caret_; }
    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    void set_value(float value);
    void set_range(float min_value, float max_value);
    void set_step(float step);
    void set_decimals(int decimals);
    Signal<SpinBox&, float>& changed() { return changed_; }
    const Signal<SpinBox&, float>& changed() const { return changed_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;
    void focus_event(tc_ui_document* document, bool focused) override;
private:
    std::string formatted_value() const;
    void begin_edit();
    void commit_edit();
    void cancel_edit();
    tc_ui_rect up_button_rect() const;
    tc_ui_rect down_button_rect() const;
    tc_ui_rect text_clip_rect(tc_ui_document* document) const;
    size_t caret_from_content_x(tc_ui_document* document, float content_x) const;
    float prefix_width(tc_ui_document* document, size_t offset, float font_size) const;
    void move_caret(size_t next, bool extend_selection);
    bool delete_selection();
    void replace_selection(std::string_view text);
    float value_ = 0.0f;
    float min_value_ = -1000000000.0f;
    float max_value_ = 1000000000.0f;
    float step_ = 1.0f;
    int decimals_ = 2;
    bool editing_ = false;
    std::string edit_text_;
    size_t caret_ = 0;
    size_t selection_anchor_ = SIZE_MAX;
    bool selecting_ = false;
    float button_width_ = 18.0f;
    Signal<SpinBox&, float> changed_;
};
} // namespace termin::gui_native
