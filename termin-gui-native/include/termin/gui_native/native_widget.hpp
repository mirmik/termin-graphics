#pragma once

#include <cstdint>

#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

class NativeWidget : public Widget {
private:
    static const tc_widget_vtable VTABLE;

public:
    explicit NativeWidget(const char* debug_name = nullptr);
    ~NativeWidget() override = default;
    tc_ui_rect bounds() const { return tc_widget_bounds(c_widget()); }
    void set_min_size(tc_ui_size size) { tc_widget_set_min_size(c_widget(), size); }
    tc_ui_size min_size() const { return tc_widget_min_size(c_widget()); }
    void set_preferred_size(tc_ui_size size) { tc_widget_set_preferred_size(c_widget(), size); }
    tc_ui_size preferred_size() const { return tc_widget_preferred_size(c_widget()); }
    void set_max_size(tc_ui_size size) { tc_widget_set_max_size(c_widget(), size); }
    tc_ui_size max_size() const { return tc_widget_max_size(c_widget()); }
    tc_ui_style computed_style(tc_ui_document_handle document, uint32_t extra_state_flags = 0) const;
    virtual tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints);
    virtual void layout(tc_ui_document_handle document, tc_ui_rect rect);
    virtual void paint(tc_ui_document_handle document, tc_ui_paint_context* context);
    virtual tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event);
    virtual tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y);
    virtual tc_ui_event_result key_event(tc_ui_document_handle document, const tc_ui_key_event* event);
    virtual tc_ui_event_result text_event(tc_ui_document_handle document, const tc_ui_text_event* event);
    virtual void focus_event(tc_ui_document_handle document, bool focused);
    virtual void overlay_dismissed(tc_ui_document_handle document, tc_ui_overlay_dismiss_reason reason);
    virtual void on_destroy(tc_ui_document_handle document);
private:
    static tc_ui_size dispatch_measure(tc_widget* widget, tc_ui_document_handle document, tc_ui_constraints constraints);
    static void dispatch_layout(tc_widget* widget, tc_ui_document_handle document, tc_ui_rect rect);
    static void dispatch_paint(tc_widget* widget, tc_ui_document_handle document, tc_ui_paint_context* context);
    static tc_ui_event_result dispatch_pointer_event(tc_widget* widget, tc_ui_document_handle document, const tc_ui_pointer_event* event);
    static tc_widget_handle dispatch_hit_test(tc_widget* widget, tc_ui_document_handle document, float x, float y);
    static tc_ui_event_result dispatch_key_event(tc_widget* widget, tc_ui_document_handle document, const tc_ui_key_event* event);
    static tc_ui_event_result dispatch_text_event(tc_widget* widget, tc_ui_document_handle document, const tc_ui_text_event* event);
    static void dispatch_focus_event(tc_widget* widget, tc_ui_document_handle document, bool focused);
    static void dispatch_overlay_dismissed(tc_widget* widget, tc_ui_document_handle document, tc_ui_overlay_dismiss_reason reason);
    static void dispatch_on_destroy(tc_widget* widget, tc_ui_document_handle document);
};

} // namespace termin::gui_native
