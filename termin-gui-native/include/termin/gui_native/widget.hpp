#pragma once

#include <utility>

#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

class Widget {
public:
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    tc_widget* c_widget() { return &_widget; }
    const tc_widget* c_widget() const { return &_widget; }
    tc_widget_handle handle() const { return _widget.handle; }

protected:
    explicit Widget(const tc_widget_vtable* vtable, const char* debug_name = nullptr) {
        tc_widget_init(&_widget, vtable, &Widget::delete_widget, TC_LANGUAGE_CXX, this);
        _widget.debug_name = debug_name;
    }

    virtual ~Widget() = default;

private:
    static void delete_widget(tc_widget* widget) {
        delete static_cast<Widget*>(widget->body);
    }

    tc_widget _widget {};
};

class Document {
public:
    Document() : _document(tc_ui_document_create()) {}

    ~Document() {
        tc_ui_document_destroy(_document);
    }

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Document(Document&& other) noexcept : _document(std::exchange(other._document, nullptr)) {}

    Document& operator=(Document&& other) noexcept {
        if (this != &other) {
            tc_ui_document_destroy(_document);
            _document = std::exchange(other._document, nullptr);
        }
        return *this;
    }

    tc_ui_document* get() { return _document; }
    const tc_ui_document* get() const { return _document; }

    tc_widget_handle adopt(Widget* widget) {
        return tc_ui_document_adopt_widget(_document, widget ? widget->c_widget() : nullptr);
    }

private:
    tc_ui_document* _document = nullptr;
};

} // namespace termin::gui_native
