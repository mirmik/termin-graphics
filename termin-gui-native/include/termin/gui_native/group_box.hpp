#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class GroupBox : public NativeWidget {
private:
    std::string title_;
    float header_height_ = 30.0f;

public:
    explicit GroupBox(std::string title = {}, const char* debug_name = nullptr);
    GroupBox& set_title(std::string title);
    GroupBox& set_padding(EdgeInsets padding);
    GroupBox& set_background(Color color);
    GroupBox& set_border(Color color, float thickness = 1.0f);
    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }
    const std::string& title() const { return title_; }
    tc_widget_handle content() const { return child_handle_at(0); }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;
private:
    tc_ui_rect content_rect(tc_ui_document* document) const;
};
} // namespace termin::gui_native
