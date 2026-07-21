#include <termin/gui_native/document.hpp>

#include <stdexcept>
#include <utility>

namespace termin::gui_native {

Document::Document() : _document(tc_ui_document_create()) {
    if (tc_ui_document_handle_is_invalid(_document)) {
        throw std::runtime_error("failed to create tc_ui_document");
    }
}

Document::~Document() {
    close();
}

Document::Document(Document&& other) noexcept : _document(std::exchange(other._document, tc_ui_document_handle_invalid())) {}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        close();
        _document = std::exchange(other._document, tc_ui_document_handle_invalid());
    }
    return *this;
}

void Document::close() {
    if (!tc_ui_document_handle_is_invalid(_document)) {
        tc_ui_document_destroy(_document);
        _document = tc_ui_document_handle_invalid();
    }
}

tc_widget_handle Document::adopt(Widget* widget) {
    return tc_ui_document_adopt_widget(
        _document,
        widget ? widget->c_widget() : nullptr,
        &Widget::delete_owned_widget
    );
}

tc::trent Document::serialize() const {
    tc_value value = tc_ui_document_serialize(_document);
    if (value.type != TC_VALUE_DICT) {
        tc_value_free(&value);
        throw std::runtime_error("failed to serialize native UI document");
    }
    return tc::trent::adopt(value);
}

void Document::restore(const tc::trent& serialized) {
    if (!tc_ui_document_restore(_document, serialized.raw())) {
        throw std::runtime_error("failed to restore native UI document");
    }
}

tc_ui_style Document::resolve_style(const Widget& widget, uint32_t extra_state_flags) const {
    tc_ui_style style {};
    if (!tc_ui_document_resolve_style(_document, widget.c_widget(), extra_state_flags, &style)) {
        throw std::runtime_error("failed to resolve native UI widget style");
    }
    return style;
}

} // namespace termin::gui_native
