#pragma once

#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {

    class WrapLayout : public NativeWidget {
    private:
        Orientation orientation_ = Orientation::Horizontal;
        EdgeInsets padding_{};
        float spacing_ = 0.0f;
        float line_spacing_ = 0.0f;
        CrossAxisAlignment line_alignment_ = CrossAxisAlignment::Start;

    public:
        explicit WrapLayout(Orientation orientation = Orientation::Horizontal, const char* debug_name = nullptr);
        WrapLayout& set_orientation(Orientation orientation);
        WrapLayout& set_padding(EdgeInsets padding);
        WrapLayout& set_spacing(float spacing);
        WrapLayout& set_line_spacing(float spacing);
        WrapLayout& set_line_alignment(CrossAxisAlignment alignment);
        void add_child(tc_widget_handle handle);
        void add_child(const Widget& widget) {
            add_child(widget.handle());
        }
        std::vector<tc_widget_handle> children() const;
        Orientation orientation() const {
            return orientation_;
        }
        EdgeInsets padding() const {
            return padding_;
        }
        float spacing() const {
            return spacing_;
        }
        float line_spacing() const {
            return line_spacing_;
        }
        CrossAxisAlignment line_alignment() const {
            return line_alignment_;
        }
        tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
        void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
        tc_ui_event_result pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) override;
        tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
    };

} // namespace termin::gui_native
