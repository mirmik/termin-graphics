#pragma once

#include <string>
#include <utility>

#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

class Widget {
public:
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    tc_widget* c_widget() { return &_widget; }
    const tc_widget* c_widget() const { return &_widget; }
    tc_ui_document* document() { return _widget.document; }
    const tc_ui_document* document() const { return _widget.document; }
    tc_widget_handle handle() const { return _widget.handle; }
    const char* stable_id() const { return tc_widget_stable_id(&_widget); }
    const char* name() const { return tc_widget_name(&_widget); }
    const char* debug_name() const { return tc_widget_debug_name(&_widget); }
    Widget& set_stable_id(std::string stable_id) {
        stable_id_ = std::move(stable_id);
        _widget.stable_id = stable_id_.empty() ? nullptr : stable_id_.c_str();
        return *this;
    }
    Widget& set_name(std::string name) {
        name_ = std::move(name);
        _widget.name = name_.empty() ? nullptr : name_.c_str();
        return *this;
    }
    Widget& set_debug_name(std::string debug_name) {
        debug_name_ = std::move(debug_name);
        _widget.debug_name = debug_name_.empty() ? nullptr : debug_name_.c_str();
        return *this;
    }
    void set_focusable(bool focusable) { tc_widget_set_focusable(&_widget, focusable); }
    bool focusable() const { return tc_widget_is_focusable(&_widget); }
    void set_visible(bool visible) { tc_widget_set_visible(&_widget, visible); }
    bool visible() const { return tc_widget_is_visible(&_widget); }
    void set_enabled(bool enabled) { tc_widget_set_enabled(&_widget, enabled); }
    bool enabled() const { return tc_widget_is_enabled(&_widget); }
    void set_mouse_transparent(bool mouse_transparent) {
        tc_widget_set_mouse_transparent(&_widget, mouse_transparent);
    }
    bool mouse_transparent() const { return tc_widget_is_mouse_transparent(&_widget); }
    tc_widget* parent_widget() { return tc_widget_parent(&_widget); }
    const tc_widget* parent_widget() const { return tc_widget_parent_const(&_widget); }
    size_t child_count() const { return tc_widget_child_count(&_widget); }
    tc_widget* child_at(size_t index) { return tc_widget_child_at(&_widget, index); }
    const tc_widget* child_at(size_t index) const { return tc_widget_child_at_const(&_widget, index); }
    tc_widget_handle child_handle_at(size_t index) const {
        const tc_widget* child = child_at(index);
        return child ? child->handle : tc_widget_handle_invalid();
    }
    bool append_child(Widget& child) { return tc_widget_append_child(&_widget, child.c_widget()); }
    bool insert_child(size_t index, Widget& child) {
        return tc_widget_insert_child(&_widget, index, child.c_widget());
    }
    bool remove_child(Widget& child) { return tc_widget_remove_child(&_widget, child.c_widget()); }
    bool detach() { return tc_widget_detach(&_widget); }
    void mark_dirty(uint32_t dirty_flags) { tc_widget_mark_dirty(&_widget, dirty_flags); }
    void clear_dirty(uint32_t dirty_flags) { tc_widget_clear_dirty(&_widget, dirty_flags); }
    uint32_t dirty_flags() const { return tc_widget_dirty_flags(&_widget); }
    bool has_dirty_flags(uint32_t dirty_flags) const {
        return tc_widget_has_dirty_flags(&_widget, dirty_flags);
    }

protected:
    explicit Widget(const tc_widget_vtable* vtable, const char* debug_name = nullptr) {
        tc_widget_init(&_widget, vtable, &Widget::delete_widget, TC_LANGUAGE_CXX, this);
        set_debug_name(debug_name ? debug_name : std::string {});
    }

    virtual ~Widget() = default;

private:
    static void delete_widget(tc_widget* widget) {
        delete static_cast<Widget*>(widget->body);
    }

    std::string stable_id_;
    std::string name_;
    std::string debug_name_;
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

    bool add_root(const Widget& widget) {
        return tc_ui_document_add_root(_document, widget.handle());
    }

    bool remove_root(const Widget& widget) {
        return tc_ui_document_remove_root(_document, widget.handle());
    }

    void layout_roots(tc_ui_rect rect) {
        tc_ui_document_layout_roots(_document, rect);
    }

    void paint_roots(tc_ui_paint_context* context) {
        tc_ui_document_paint_roots(_document, context);
    }

    tc_ui_event_result dispatch_pointer_event(const tc_ui_pointer_event& event) {
        return tc_ui_document_dispatch_pointer_event(_document, &event);
    }

    tc_widget_handle hit_test(float x, float y) {
        return tc_ui_document_hit_test(_document, x, y);
    }

    tc_widget_handle hovered_widget() const {
        return tc_ui_document_hovered_widget(_document);
    }

    tc_widget_handle pointer_capture() const {
        return tc_ui_document_pointer_capture(_document);
    }

    bool set_pointer_capture(const Widget& widget) {
        return tc_ui_document_set_pointer_capture(_document, widget.handle());
    }

    bool release_pointer_capture(const Widget& widget) {
        return tc_ui_document_release_pointer_capture(_document, widget.handle());
    }

    tc_widget_handle focused_widget() const {
        return tc_ui_document_focused_widget(_document);
    }

    bool set_focus(const Widget& widget) {
        return tc_ui_document_set_focus(_document, widget.handle());
    }

    bool clear_focus(const Widget& widget) {
        return tc_ui_document_clear_focus(_document, widget.handle());
    }

    tc_ui_event_result dispatch_key_event(const tc_ui_key_event& event) {
        return tc_ui_document_dispatch_key_event(_document, &event);
    }

    tc_ui_event_result dispatch_text_event(const tc_ui_text_event& event) {
        return tc_ui_document_dispatch_text_event(_document, &event);
    }

private:
    tc_ui_document* _document = nullptr;
};

} // namespace termin::gui_native
