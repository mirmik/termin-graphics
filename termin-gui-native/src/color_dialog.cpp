#include "widgets_internal.hpp"

namespace termin::gui_native {

ColorDialog::ColorDialog(Color initial, bool show_alpha, std::string title)
    : Dialog(std::move(title)), model_(std::make_shared<ColorPickerModel>(initial, show_alpha)) {
    set_actions({
        DialogAction{"ok", "OK", true, false},
        DialogAction{"cancel", "Cancel", false, true},
    });
    finished_connection_ = finished().connect([this](Dialog&, const DialogResult& result) {
        const std::optional<Color> color =
            result.action_id == "ok" ? accepted_color_ : std::nullopt;
        color_finished_.emit(*this, color);
    });
}

bool ColorDialog::ensure_content(tc_ui_document* document) {
    if (!tc_widget_handle_is_invalid(picker_handle_) &&
        tc_ui_document_is_alive(document, picker_handle_))
        return true;
    auto picker = std::make_unique<ColorPicker>(model_);
    picker_handle_ = tc_ui_document_adopt_widget(
        document, picker->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(picker_handle_)) {
        tc_log_error("[termin-gui-native] ColorDialog failed to adopt picker content");
        return false;
    }
    ColorPicker* body = picker.release();
    set_content(*body);
    return true;
}

bool ColorDialog::before_action(const DialogAction& action) {
    if (action.stable_id == "ok")
        accepted_color_ = model_->color();
    else
        accepted_color_.reset();
    return true;
}

bool ColorDialog::show(tc_ui_document* document, tc_ui_rect viewport) {
    accepted_color_.reset();
    return ensure_content(document) && Dialog::show(document, viewport);
}

} // namespace termin::gui_native
