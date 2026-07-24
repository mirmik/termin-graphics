#include <termin/gui_native/tc_document.hpp>

#include <stdexcept>

namespace termin::gui_native {

tc_widget_handle TcDocument::adopt(Widget* widget) const {
    return tc_ui_document_adopt_widget(
        handle_, widget ? widget->c_widget() : nullptr, &Widget::delete_owned_widget);
}

tc::trent TcDocument::serialize() const {
    tc_value value = tc_ui_document_serialize(handle_);
    if (value.type != TC_VALUE_DICT) {
        tc_value_free(&value);
        throw std::runtime_error("failed to serialize native UI document");
    }
    return tc::trent::adopt(value);
}

void TcDocument::restore(const tc::trent& serialized) const {
    if (!tc_ui_document_restore(handle_, serialized.raw())) {
        throw std::runtime_error("failed to restore native UI document");
    }
}

tc_ui_style TcDocument::resolve_style(
    const Widget& widget, uint32_t extra_state_flags) const {
    tc_ui_style style{};
    if (!tc_ui_document_resolve_style(
            handle_, widget.c_widget(), extra_state_flags, &style)) {
        throw std::runtime_error("failed to resolve native UI widget style");
    }
    return style;
}

} // namespace termin::gui_native
