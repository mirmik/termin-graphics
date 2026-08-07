#pragma once

#include <string>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
    class Label : public NativeWidget {
    private:
        std::string text_;
        TextWrapMode wrap_mode_ = TextWrapMode::None;
        TextOverflow overflow_ = TextOverflow::Clip;
        size_t max_lines_ = 0;

    public:
        explicit Label(std::string text);
        Label(std::string text, float font_size);
        Label(std::string text, float font_size, Color color);
        const std::string& text() const {
            return text_;
        }
        TextWrapMode wrap_mode() const {
            return wrap_mode_;
        }
        TextOverflow overflow() const {
            return overflow_;
        }
        size_t max_lines() const {
            return max_lines_;
        }
        Label& set_text(std::string text);
        Label& set_color(Color color);
        Label& set_font_size(float font_size);
        Label& set_wrap_mode(TextWrapMode mode);
        Label& set_overflow(TextOverflow overflow);
        Label& set_max_lines(size_t max_lines);
        tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
        void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;

    private:
        void update_unmeasured_size();
    };
} // namespace termin::gui_native
