#pragma once

#include <stdexcept>
#include <string>

#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

class Widget {
private:
    tc_widget _widget {};

public:
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    tc_widget* c_widget() { return &_widget; }
    const tc_widget* c_widget() const { return &_widget; }
    static void delete_owned_widget(tc_widget* widget) {
        delete static_cast<Widget*>(widget->body);
    }
    tc_ui_document_handle document() { return _widget.document; }
    tc_ui_document_handle document() const { return _widget.document; }
    tc_widget_handle handle() const { return _widget.handle; }
    const char* stable_id() const { return tc_widget_stable_id(&_widget); }
    const char* name() const { return tc_widget_name(&_widget); }
    const char* debug_name() const { return tc_widget_debug_name(&_widget); }
    Widget& set_stable_id(std::string stable_id) {
        if (!tc_widget_set_stable_id(&_widget, stable_id.c_str())) {
            throw std::runtime_error("failed to set widget stable id");
        }
        return *this;
    }
    Widget& set_name(std::string name) {
        if (!tc_widget_set_name(&_widget, name.c_str())) {
            throw std::runtime_error("failed to set widget name");
        }
        return *this;
    }
    Widget& set_debug_name(std::string debug_name) {
        if (!tc_widget_set_debug_name(&_widget, debug_name.c_str())) {
            throw std::runtime_error("failed to set widget debug name");
        }
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
    bool set_cursor_intent(tc_ui_cursor_intent cursor) {
        return tc_widget_set_cursor_intent(&_widget, cursor);
    }
    tc_ui_cursor_intent cursor_intent() const { return tc_widget_cursor_intent(&_widget); }
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
    void set_style_role(tc_ui_style_role role) { tc_widget_set_style_role(&_widget, role); }
    tc_ui_style_role style_role() const { return tc_widget_style_role(&_widget); }
    bool set_style_override(const tc_ui_style_override& style_override) {
        return tc_widget_set_style_override(&_widget, &style_override);
    }
    void clear_style_override() { tc_widget_clear_style_override(&_widget); }
    tc_ui_style_override style_override() const { return tc_widget_style_override(&_widget); }

protected:
    explicit Widget(const tc_widget_vtable* vtable, const char* debug_name = nullptr) {
        tc_widget_init_unowned(&_widget, vtable, TC_LANGUAGE_CXX, this);
        set_debug_name(debug_name ? debug_name : std::string {});
    }

    virtual ~Widget() = default;

};

} // namespace termin::gui_native

// Kept for source compatibility with existing consumers of widget.hpp.
#include <termin/gui_native/document.hpp>
