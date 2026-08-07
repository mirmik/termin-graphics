#include "termin/gui_native/window_input.hpp"

namespace termin::gui_native {

    namespace {

        int32_t translate_modifiers(uint32_t modifiers) {
            int32_t result = 0;
            if ((modifiers & WindowModifierShift) != 0)
                result |= TC_UI_MOD_SHIFT;
            if ((modifiers & WindowModifierControl) != 0)
                result |= TC_UI_MOD_CTRL;
            if ((modifiers & WindowModifierAlt) != 0)
                result |= TC_UI_MOD_ALT;
            if ((modifiers & WindowModifierSuper) != 0)
                result |= TC_UI_MOD_SUPER;
            return result;
        }

        int32_t translate_key(WindowKey key) {
            return window_key_code(key);
        }

    } // namespace

    std::optional<tc_ui_pointer_event> make_pointer_event(const WindowEvent& event) {
        tc_ui_pointer_event_type type;
        switch (event.type) {
        case WindowEventType::PointerMoved:
            type = TC_UI_POINTER_MOVE;
            break;
        case WindowEventType::PointerButtonPressed:
            type = TC_UI_POINTER_DOWN;
            break;
        case WindowEventType::PointerButtonReleased:
            type = TC_UI_POINTER_UP;
            break;
        case WindowEventType::PointerWheel:
            type = TC_UI_POINTER_WHEEL;
            break;
        case WindowEventType::PointerCaptureLost:
        case WindowEventType::FocusLost:
            type = TC_UI_POINTER_CANCEL;
            break;
        default:
            return std::nullopt;
        }

        return tc_ui_pointer_event{type,
                                   event.pointer.framebuffer_position.x,
                                   event.pointer.framebuffer_position.y,
                                   tcbase::mouse_button_value(event.pointer.button),
                                   event.pointer.clicks,
                                   translate_modifiers(event.pointer.modifiers),
                                   event.pointer.wheel_x,
                                   event.pointer.wheel_y,
                                   event.type == WindowEventType::FocusLost ? TC_UI_POINTER_CANCEL_WINDOW_FOCUS_LOST
                                   : event.type == WindowEventType::PointerCaptureLost
                                       ? TC_UI_POINTER_CANCEL_HOST_CAPTURE_LOST
                                       : TC_UI_POINTER_CANCEL_EXPLICIT};
    }

    std::optional<tc_ui_key_event> make_key_event(const WindowEvent& event) {
        if (event.type != WindowEventType::KeyPressed && event.type != WindowEventType::KeyReleased) {
            return std::nullopt;
        }
        return tc_ui_key_event{event.type == WindowEventType::KeyPressed ? TC_UI_KEY_DOWN : TC_UI_KEY_UP,
                               translate_key(event.key.key),
                               event.key.native_scancode,
                               translate_modifiers(event.key.modifiers),
                               event.key.repeat};
    }

    std::optional<tc_ui_text_event> make_text_event(const WindowEvent& event) {
        if (event.type != WindowEventType::TextInput) {
            return std::nullopt;
        }
        return tc_ui_text_event{event.text.utf8.data()};
    }

    tc_ui_event_result dispatch_window_event(TcDocument document, const WindowEvent& event) {
        if (const auto pointer = make_pointer_event(event)) {
            return document.dispatch_pointer_event(*pointer);
        }
        if (const auto key = make_key_event(event)) {
            return document.dispatch_key_event(*key);
        }
        if (const auto text = make_text_event(event)) {
            return document.dispatch_text_event(*text);
        }
        return TC_UI_EVENT_IGNORED;
    }

} // namespace termin::gui_native
