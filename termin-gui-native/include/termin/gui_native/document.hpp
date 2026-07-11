#pragma once

#include <stdexcept>
#include <utility>

#include <tcbase/tc_trent.hpp>
#include <termin/gui_native/tc_ui_serialization.h>
#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

class Document {
private:
    tc_ui_document* _document = nullptr;

public:
    Document() : _document(tc_ui_document_create()) {}
    ~Document() { tc_ui_document_destroy(_document); }
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
    tc_widget_handle adopt(Widget* widget) { return tc_ui_document_adopt_widget(_document, widget ? widget->c_widget() : nullptr); }
    bool add_root(const Widget& widget) { return tc_ui_document_add_root(_document, widget.handle()); }
    bool remove_root(const Widget& widget) { return tc_ui_document_remove_root(_document, widget.handle()); }
    void layout_roots(tc_ui_rect rect) { tc_ui_document_layout_roots(_document, rect); }
    void paint_roots(tc_ui_paint_context* context) { tc_ui_document_paint_roots(_document, context); }
    void paint(tc_ui_paint_context* context) { tc_ui_document_paint(_document, context); }
    bool show_overlay(const Widget& widget, uint32_t flags = 0) { return tc_ui_document_show_overlay(_document, widget.handle(), flags); }
    bool dismiss_overlay(const Widget& widget, tc_ui_overlay_dismiss_reason reason = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC) { return tc_ui_document_dismiss_overlay(_document, widget.handle(), reason); }
    size_t overlay_count() const { return tc_ui_document_overlay_count(_document); }
    tc_ui_event_result dispatch_pointer_event(const tc_ui_pointer_event& event) { return tc_ui_document_dispatch_pointer_event(_document, &event); }
    tc_widget_handle hit_test(float x, float y) { return tc_ui_document_hit_test(_document, x, y); }
    tc_widget_handle hovered_widget() const { return tc_ui_document_hovered_widget(_document); }
    tc_widget_handle pointer_capture() const { return tc_ui_document_pointer_capture(_document); }
    tc_widget_handle pressed_widget() const { return tc_ui_document_pressed_widget(_document); }
    bool set_pointer_capture(const Widget& widget) { return tc_ui_document_set_pointer_capture(_document, widget.handle()); }
    bool release_pointer_capture(const Widget& widget) { return tc_ui_document_release_pointer_capture(_document, widget.handle()); }
    tc_widget_handle focused_widget() const { return tc_ui_document_focused_widget(_document); }
    bool set_focus(const Widget& widget) { return tc_ui_document_set_focus(_document, widget.handle()); }
    bool clear_focus(const Widget& widget) { return tc_ui_document_clear_focus(_document, widget.handle()); }
    bool focus_next() { return tc_ui_document_focus_next(_document); }
    bool focus_previous() { return tc_ui_document_focus_previous(_document); }
    tc_ui_event_result dispatch_key_event(const tc_ui_key_event& event) { return tc_ui_document_dispatch_key_event(_document, &event); }
    tc_ui_event_result dispatch_text_event(const tc_ui_text_event& event) { return tc_ui_document_dispatch_text_event(_document, &event); }
    void set_text_measurer(tc_ui_text_measure_fn measure, void* user_data) { tc_ui_document_set_text_measurer(_document, measure, user_data); }
    void set_clipboard(tc_ui_clipboard_get_text_fn get_text, tc_ui_clipboard_set_text_fn set_text, void* user_data) { tc_ui_document_set_clipboard(_document, get_text, set_text, user_data); }
    bool measure_text(const char* text_utf8, size_t text_byte_length, float font_size, tc_ui_text_metrics& out_metrics) { return tc_ui_document_measure_text(_document, text_utf8, text_byte_length, font_size, &out_metrics); }
    const tc_ui_theme& theme() const { return *tc_ui_document_theme(_document); }
    bool set_theme(const tc_ui_theme& theme) { return tc_ui_document_set_theme(_document, &theme); }
    uint64_t theme_revision() const { return tc_ui_document_theme_revision(_document); }
    tc::trent serialize() const {
        tc_value value = tc_ui_document_serialize(_document);
        if (value.type != TC_VALUE_DICT) {
            tc_value_free(&value);
            throw std::runtime_error("failed to serialize native UI document");
        }
        return tc::trent::adopt(value);
    }
    void restore(const tc::trent& serialized) {
        if (!tc_ui_document_restore(_document, serialized.raw())) {
            throw std::runtime_error("failed to restore native UI document");
        }
    }
    tc_ui_style resolve_style(const Widget& widget, uint32_t extra_state_flags = 0) const {
        tc_ui_style style {};
        if (!tc_ui_document_resolve_style(_document, widget.c_widget(), extra_state_flags, &style)) throw std::runtime_error("failed to resolve native UI widget style");
        return style;
    }
};

} // namespace termin::gui_native
