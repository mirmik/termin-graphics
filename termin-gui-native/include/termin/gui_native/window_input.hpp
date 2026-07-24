#pragma once

#include <optional>

#include <termin/gui_native/tc_document.hpp>
#include <termin/window/event.hpp>

namespace termin::gui_native {

std::optional<tc_ui_pointer_event> make_pointer_event(const WindowEvent& event);
std::optional<tc_ui_key_event> make_key_event(const WindowEvent& event);
std::optional<tc_ui_text_event> make_text_event(const WindowEvent& event);

// Routes input events and ignores lifecycle events such as resize and close.
tc_ui_event_result dispatch_window_event(
    TcDocument document,
    const WindowEvent& event);

} // namespace termin::gui_native
