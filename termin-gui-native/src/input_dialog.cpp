#include "widgets_internal.hpp"

#include <stdexcept>

namespace termin::gui_native {

InputDialog::InputDialog(std::string title, std::string message, std::string value)
    : Dialog(std::move(title)), message_(std::move(message)), initial_value_(std::move(value)) {
    set_actions({
        DialogAction{"ok", "OK", true, false},
        DialogAction{"cancel", "Cancel", false, true},
    });
    finished_connection_ = finished().connect([this](Dialog&, const DialogResult& result) {
        std::optional<std::string> value;
        if (result.action_id == "ok")
            value = this->value();
        value_finished_.emit(*this, value);
    });
}

bool InputDialog::ensure_content(tc_ui_document* document) {
    if (!tc_widget_handle_is_invalid(input_handle_) &&
        tc_ui_document_is_alive(document, input_handle_)) {
        return true;
    }
    auto column = std::make_unique<VStack>("input-dialog-content");
    column->set_spacing(8.0f);
    auto label = std::make_unique<Label>(message_);
    auto input = std::make_unique<TextInput>(initial_value_);
    const tc_widget_handle column_handle =
        tc_ui_document_adopt_widget(
            document, column->c_widget(), &Widget::delete_owned_widget);
    const tc_widget_handle label_handle = tc_ui_document_adopt_widget(
        document, label->c_widget(), &Widget::delete_owned_widget);
    const tc_widget_handle input_handle = tc_ui_document_adopt_widget(
        document, input->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(column_handle) || tc_widget_handle_is_invalid(label_handle) ||
        tc_widget_handle_is_invalid(input_handle)) {
        tc_log_error("[termin-gui-native] InputDialog failed to adopt content widgets");
        if (!tc_widget_handle_is_invalid(column_handle))
            tc_ui_document_destroy_widget_recursive(document, column_handle);
        if (!tc_widget_handle_is_invalid(label_handle))
            tc_ui_document_destroy_widget_recursive(document, label_handle);
        if (!tc_widget_handle_is_invalid(input_handle))
            tc_ui_document_destroy_widget_recursive(document, input_handle);
        return false;
    }
    VStack* column_body = column.release();
    Label* label_body = label.release();
    TextInput* input_body = input.release();
    if (!message_.empty())
        column_body->add_preferred_child(*label_body);
    else
        tc_ui_document_destroy_widget(document, label_handle);
    column_body->add_preferred_child(*input_body);
    input_handle_ = input_handle;
    input_submit_connection_ = input_body->submitted().connect(
        [this](TextInput&, const std::string&) { activate("ok", this->document()); });
    set_content(*column_body);
    return true;
}

const std::string& InputDialog::value() const {
    if (!document() || !tc_ui_document_is_alive(document(), input_handle_))
        return initial_value_;
    const tc_widget* widget = tc_ui_document_resolve_widget_const(document(), input_handle_);
    const auto* input = dynamic_cast<const TextInput*>(static_cast<const Widget*>(widget->body));
    return input ? input->text() : initial_value_;
}

void InputDialog::set_value(std::string value) {
    if (!detail::valid_utf8(value)) {
        tc_log_error("[termin-gui-native] InputDialog rejected invalid UTF-8 value");
        throw std::invalid_argument("input dialog value must be valid UTF-8");
    }
    initial_value_ = std::move(value);
    if (document() && tc_ui_document_is_alive(document(), input_handle_)) {
        tc_widget* widget = tc_ui_document_resolve_widget(document(), input_handle_);
        auto* input = dynamic_cast<TextInput*>(static_cast<Widget*>(widget->body));
        if (input)
            input->set_text(initial_value_);
    }
}

bool InputDialog::show(tc_ui_document* document, tc_ui_rect viewport) {
    if (!ensure_content(document) || !Dialog::show(document, viewport))
        return false;
    tc_ui_document_set_focus(document, input_handle_);
    return true;
}

} // namespace termin::gui_native
