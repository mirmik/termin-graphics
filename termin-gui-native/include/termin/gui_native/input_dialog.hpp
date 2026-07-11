#pragma once

#include <optional>

#include <termin/gui_native/dialog.hpp>

namespace termin::gui_native {

class InputDialog final : public Dialog {
  private:
    std::string message_;
    std::string initial_value_;
    tc_widget_handle input_handle_ = tc_widget_handle_invalid();
    size_t input_submit_connection_ = 0;
    size_t finished_connection_ = 0;
    Signal<InputDialog&, const std::optional<std::string>&> value_finished_;

  public:
    InputDialog(std::string title, std::string message = {}, std::string value = {});

    const std::string& message() const { return message_; }
    const std::string& value() const;
    void set_value(std::string value);
    bool show(tc_ui_document* document, tc_ui_rect viewport);
    Signal<InputDialog&, const std::optional<std::string>&>& value_finished() {
        return value_finished_;
    }

  private:
    bool ensure_content(tc_ui_document* document);

};

} // namespace termin::gui_native
