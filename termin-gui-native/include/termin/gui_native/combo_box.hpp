#pragma once

#include <string>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
class ComboBoxPopup;
class ComboBox : public NativeWidget {
private:
    std::vector<std::string> items_;
    int selected_index_ = -1;
    bool open_ = false;
    tc_widget_handle popup_handle_ = tc_widget_handle_invalid();
    float item_height_ = 24.0f;
    size_t max_visible_items_ = 8;
    Signal<ComboBox&, int, const std::string&> changed_;

public:
    ComboBox();
    size_t item_count() const { return items_.size(); }
    const std::string& item_text(size_t index) const;
    int selected_index() const { return selected_index_; }
    std::string selected_text() const;
    bool open() const { return open_; }
    void add_item(std::string item);
    void clear_items();
    void set_selected_index(int index);
    Signal<ComboBox&, int, const std::string&>& changed() { return changed_; }
    const Signal<ComboBox&, int, const std::string&>& changed() const { return changed_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    void on_destroy(tc_ui_document* document) override;
private:
    friend class ComboBoxPopup;
    bool show_popup(tc_ui_document* document);
    void hide_popup(tc_ui_document* document);
    void popup_dismissed();
};
} // namespace termin::gui_native
