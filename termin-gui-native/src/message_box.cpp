#include "widgets_internal.hpp"

namespace termin::gui_native {

MessageBox::MessageBox(std::string title, std::string message, MessageBoxKind kind)
    : Dialog(std::move(title)), message_(std::move(message)), kind_(kind) {
    if (kind_ == MessageBoxKind::Question) {
        set_actions({
            DialogAction{"yes", "Yes", true, false},
            DialogAction{"no", "No", false, true},
        });
    } else {
        set_actions({DialogAction{"ok", "OK", true, false}});
    }
}

bool MessageBox::ensure_content(tc_ui_document* document) {
    if (!tc_widget_handle_is_invalid(message_content_handle_) &&
        tc_ui_document_is_alive(document, message_content_handle_)) {
        return true;
    }
    const char* icon = "ℹ";
    Color color{0.30f, 0.60f, 0.90f, 1.0f};
    if (kind_ == MessageBoxKind::Warning) {
        icon = "⚠";
        color = Color{0.90f, 0.70f, 0.20f, 1.0f};
    } else if (kind_ == MessageBoxKind::Error) {
        icon = "✖";
        color = Color{0.90f, 0.30f, 0.30f, 1.0f};
    } else if (kind_ == MessageBoxKind::Question) {
        icon = "?";
        color = Color{0.30f, 0.80f, 0.50f, 1.0f};
    }
    auto row = std::make_unique<HStack>("message-box-content");
    row->set_spacing(14.0f);
    auto icon_label = std::make_unique<Label>(icon, 28.0f, color);
    auto message_label = std::make_unique<Label>(message_);
    const tc_widget_handle row_handle = tc_ui_document_adopt_widget(document, row->c_widget());
    const tc_widget_handle icon_handle =
        tc_ui_document_adopt_widget(document, icon_label->c_widget());
    const tc_widget_handle message_handle =
        tc_ui_document_adopt_widget(document, message_label->c_widget());
    if (tc_widget_handle_is_invalid(row_handle) || tc_widget_handle_is_invalid(icon_handle) ||
        tc_widget_handle_is_invalid(message_handle)) {
        tc_log_error("[termin-gui-native] MessageBox failed to adopt content widgets");
        if (!tc_widget_handle_is_invalid(row_handle))
            tc_ui_document_destroy_widget_recursive(document, row_handle);
        if (!tc_widget_handle_is_invalid(icon_handle))
            tc_ui_document_destroy_widget_recursive(document, icon_handle);
        if (!tc_widget_handle_is_invalid(message_handle))
            tc_ui_document_destroy_widget_recursive(document, message_handle);
        return false;
    }
    HStack* row_body = row.release();
    Label* icon_body = icon_label.release();
    Label* message_body = message_label.release();
    row_body->add_preferred_child(*icon_body);
    row_body->add_child(*message_body);
    message_content_handle_ = row_handle;
    set_content(*row_body);
    return true;
}

bool MessageBox::show(tc_ui_document* document, tc_ui_rect viewport) {
    return ensure_content(document) && Dialog::show(document, viewport);
}

} // namespace termin::gui_native
