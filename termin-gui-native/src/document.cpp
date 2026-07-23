#include <termin/gui_native/document.hpp>

#include <stdexcept>

#include <tcbase/tc_log.h>

namespace termin::gui_native {

Document::Document() : _document(tc_ui_document_create()) {
    if (tc_ui_document_handle_is_invalid(_document)) {
        throw std::runtime_error("failed to create tc_ui_document");
    }
}

Document::~Document() {
    if (_active_window_hosts != 0) {
        tc_log_error(
            "[gui-native-document] destroyed with %zu live GuiWindowHost binding(s)",
            _active_window_hosts);
    }
    destroy_document();
}

Document::Document(Document&& other) {
    if (other._active_window_hosts != 0) {
        tc_log_error("[gui-native-document] cannot move a Document with a live GuiWindowHost");
        throw std::logic_error("cannot move a Document with a live GuiWindowHost");
    }
    _document = other._document;
    other._document = tc_ui_document_handle_invalid();
}

Document& Document::operator=(Document&& other) {
    if (this == &other) return *this;
    if (_active_window_hosts != 0 || other._active_window_hosts != 0) {
        tc_log_error(
            "[gui-native-document] cannot move-assign a Document with a live GuiWindowHost");
        throw std::logic_error("cannot move-assign a Document with a live GuiWindowHost");
    }
    destroy_document();
    _document = other._document;
    other._document = tc_ui_document_handle_invalid();
    return *this;
}

void Document::attach_window_host() {
    if (!valid()) {
        tc_log_error("[gui-native-document] cannot attach a host after close");
        throw std::logic_error("Document is closed");
    }
    if (_active_window_hosts != 0) {
        tc_log_error(
            "[gui-native-document] a Document supports exactly one active GuiWindowHost");
        throw std::logic_error("Document already has an active GuiWindowHost");
    }
    ++_active_window_hosts;
}

void Document::detach_window_host() {
    if (_active_window_hosts == 0) {
        tc_log_error("[gui-native-document] GuiWindowHost binding count underflow");
        return;
    }
    --_active_window_hosts;
}

void Document::close() {
    if (_active_window_hosts != 0) {
        tc_log_error(
            "[gui-native-document] close rejected with %zu live GuiWindowHost binding(s)",
            _active_window_hosts);
        throw std::logic_error("Document::close requires GuiWindowHost to close first");
    }
    destroy_document();
}

void Document::destroy_document() noexcept {
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
