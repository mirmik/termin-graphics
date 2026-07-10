#pragma once

#include <termin/gui_native/dialog.hpp>

namespace termin::gui_native {

enum class MessageBoxKind { Information, Warning, Error, Question };

class MessageBox final : public Dialog {
  public:
    MessageBox(std::string title, std::string message,
               MessageBoxKind kind = MessageBoxKind::Information);

    const std::string& message() const { return message_; }
    MessageBoxKind kind() const { return kind_; }
    bool show(tc_ui_document* document, tc_ui_rect viewport);

  private:
    bool ensure_content(tc_ui_document* document);

    std::string message_;
    MessageBoxKind kind_ = MessageBoxKind::Information;
    tc_widget_handle message_content_handle_ = tc_widget_handle_invalid();
};

} // namespace termin::gui_native
