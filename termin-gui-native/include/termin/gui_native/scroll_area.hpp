#pragma once

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {
class ScrollArea : public NativeWidget {
public:
    explicit ScrollArea(const char* debug_name = nullptr);
    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }
    tc_widget_handle content() const { return child_handle_at(0); }
    void set_scroll(float x, float y);
    float scroll_x() const { return scroll_x_; }
    float scroll_y() const { return scroll_y_; }
    tc_ui_size content_size() const { return content_size_; }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;
private:
    void clamp_scroll();
    tc_ui_size content_size_ {0.0f, 0.0f};
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float wheel_step_ = 48.0f;
};
} // namespace termin::gui_native
