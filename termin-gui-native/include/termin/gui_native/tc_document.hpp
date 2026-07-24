#pragma once

#include <cstddef>

#include <tcbase/tc_trent.hpp>
#include <termin/gui_native/export.h>
#include <termin/gui_native/tc_ui_serialization.h>
#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

// Copyable, non-owning facade over a tc_ui_document registry handle.
//
// Lifetime is deliberately explicit: constructing, copying, moving or
// destroying TcDocument never creates or destroys the underlying document.
// Composition roots must use tc_ui_document_create/tc_ui_document_destroy.
class TcDocument {
  public:
    tc_ui_document_handle handle_ = tc_ui_document_handle_invalid();

    TcDocument() = default;
    explicit TcDocument(tc_ui_document_handle handle) : handle_(handle) {}

    tc_ui_document_handle handle() const { return handle_; }
    tc_ui_document_handle get() const { return handle_; }
    bool valid() const { return tc_ui_document_is_valid(handle_); }
    friend bool operator==(TcDocument lhs, TcDocument rhs) {
        return tc_ui_document_handle_eq(lhs.handle_, rhs.handle_);
    }

    TERMIN_GUI_NATIVE_API tc_widget_handle adopt(Widget* widget) const;
    bool add_root(const Widget& widget) const {
        return tc_ui_document_add_root(handle_, widget.handle());
    }
    bool remove_root(const Widget& widget) const {
        return tc_ui_document_remove_root(handle_, widget.handle());
    }
    void layout_roots(tc_ui_rect rect) const { tc_ui_document_layout_roots(handle_, rect); }
    void paint_roots(tc_ui_paint_context* context) const {
        tc_ui_document_paint_roots(handle_, context);
    }
    void paint(tc_ui_paint_context* context) const { tc_ui_document_paint(handle_, context); }
    bool show_overlay(const Widget& widget, uint32_t flags = 0) const {
        return tc_ui_document_show_overlay(handle_, widget.handle(), flags);
    }
    bool dismiss_overlay(
        const Widget& widget,
        tc_ui_overlay_dismiss_reason reason = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC) const {
        return tc_ui_document_dismiss_overlay(handle_, widget.handle(), reason);
    }
    size_t overlay_count() const { return tc_ui_document_overlay_count(handle_); }
    tc_ui_event_result dispatch_pointer_event(const tc_ui_pointer_event& event) const {
        return tc_ui_document_dispatch_pointer_event(handle_, &event);
    }
    tc_widget_handle hit_test(float x, float y) const {
        return tc_ui_document_hit_test(handle_, x, y);
    }
    tc_widget_handle hovered_widget() const {
        return tc_ui_document_hovered_widget(handle_);
    }
    tc_ui_cursor_intent cursor_intent() const {
        return tc_ui_document_cursor_intent(handle_);
    }
    void set_cursor_changed_callback(tc_ui_cursor_changed_fn callback, void* user_data) const {
        tc_ui_document_set_cursor_changed_callback(handle_, callback, user_data);
    }
    tc_widget_handle pointer_capture() const {
        return tc_ui_document_pointer_capture(handle_);
    }
    tc_widget_handle pressed_widget() const {
        return tc_ui_document_pressed_widget(handle_);
    }
    bool set_pointer_capture(const Widget& widget) const {
        return tc_ui_document_set_pointer_capture(handle_, widget.handle());
    }
    bool release_pointer_capture(const Widget& widget) const {
        return tc_ui_document_release_pointer_capture(handle_, widget.handle());
    }
    tc_widget_handle focused_widget() const {
        return tc_ui_document_focused_widget(handle_);
    }
    bool set_focus(const Widget& widget) const {
        return tc_ui_document_set_focus(handle_, widget.handle());
    }
    bool clear_focus(const Widget& widget) const {
        return tc_ui_document_clear_focus(handle_, widget.handle());
    }
    bool focus_next() const { return tc_ui_document_focus_next(handle_); }
    bool focus_previous() const { return tc_ui_document_focus_previous(handle_); }
    tc_ui_event_result dispatch_key_event(const tc_ui_key_event& event) const {
        return tc_ui_document_dispatch_key_event(handle_, &event);
    }
    tc_ui_event_result dispatch_text_event(const tc_ui_text_event& event) const {
        return tc_ui_document_dispatch_text_event(handle_, &event);
    }
    void set_text_measurer(tc_ui_text_measure_fn measure, void* user_data) const {
        tc_ui_document_set_text_measurer(handle_, measure, user_data);
    }
    void set_clipboard(tc_ui_clipboard_get_text_fn get_text,
                       tc_ui_clipboard_set_text_fn set_text, void* user_data) const {
        tc_ui_document_set_clipboard(handle_, get_text, set_text, user_data);
    }
    bool measure_text(const char* text_utf8, size_t text_byte_length, float font_size,
                      tc_ui_text_metrics& out_metrics) const {
        return tc_ui_document_measure_text(
            handle_, text_utf8, text_byte_length, font_size, &out_metrics);
    }
    const tc_ui_theme& theme() const { return *tc_ui_document_theme(handle_); }
    bool set_theme(const tc_ui_theme& theme) const {
        return tc_ui_document_set_theme(handle_, &theme);
    }
    uint64_t theme_revision() const {
        return tc_ui_document_theme_revision(handle_);
    }
    TERMIN_GUI_NATIVE_API tc::trent serialize() const;
    TERMIN_GUI_NATIVE_API void restore(const tc::trent& serialized) const;
    TERMIN_GUI_NATIVE_API tc_ui_style
    resolve_style(const Widget& widget, uint32_t extra_state_flags = 0) const;
};

} // namespace termin::gui_native
