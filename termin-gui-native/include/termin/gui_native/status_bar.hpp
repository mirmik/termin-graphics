#pragma once

#include <string>

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

class StatusBar final : public NativeWidget {
  private:
    std::string text_;
    std::string message_;
    float padding_x_ = 8.0f;
    float padding_y_ = 4.0f;

  public:
    explicit StatusBar(std::string text = "Ready");

    const std::string& text() const { return text_; }
    void set_text(std::string text);
    const std::string& message() const { return message_; }
    void show_message(std::string message);
    void clear_message();
    bool has_message() const { return !message_.empty(); }
    const std::string& displayed_text() const { return has_message() ? message_ : text_; }

    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;

};

} // namespace termin::gui_native
