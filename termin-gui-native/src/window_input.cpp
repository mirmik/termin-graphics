#include "termin/gui_native/window_input.hpp"

namespace termin::gui_native {

namespace {

int32_t translate_modifiers(uint32_t modifiers) {
    int32_t result = 0;
    if ((modifiers & WindowModifierShift) != 0) result |= TC_UI_MOD_SHIFT;
    if ((modifiers & WindowModifierControl) != 0) result |= TC_UI_MOD_CTRL;
    if ((modifiers & WindowModifierAlt) != 0) result |= TC_UI_MOD_ALT;
    if ((modifiers & WindowModifierSuper) != 0) result |= TC_UI_MOD_SUPER;
    return result;
}

int32_t translate_key(WindowKey key) {
    if (key >= WindowKey::A && key <= WindowKey::Z) {
        return TC_UI_KEY_A + static_cast<int32_t>(key) -
            static_cast<int32_t>(WindowKey::A);
    }
    if (key >= WindowKey::Digit0 && key <= WindowKey::Digit9) {
        return TC_UI_KEY_0 + static_cast<int32_t>(key) -
            static_cast<int32_t>(WindowKey::Digit0);
    }
    if (key >= WindowKey::F1 && key <= WindowKey::F12) {
        return TC_UI_KEY_F1 + static_cast<int32_t>(key) -
            static_cast<int32_t>(WindowKey::F1);
    }
    switch (key) {
        case WindowKey::Tab: return TC_UI_KEY_TAB;
        case WindowKey::Enter: return TC_UI_KEY_ENTER;
        case WindowKey::Space: return TC_UI_KEY_SPACE;
        case WindowKey::Escape: return TC_UI_KEY_ESCAPE;
        case WindowKey::Backspace: return TC_UI_KEY_BACKSPACE;
        case WindowKey::Delete: return TC_UI_KEY_DELETE;
        case WindowKey::Right: return TC_UI_KEY_RIGHT;
        case WindowKey::Left: return TC_UI_KEY_LEFT;
        case WindowKey::Down: return TC_UI_KEY_DOWN_ARROW;
        case WindowKey::Up: return TC_UI_KEY_UP_ARROW;
        case WindowKey::Home: return TC_UI_KEY_HOME;
        case WindowKey::End: return TC_UI_KEY_END;
        default: return TC_UI_KEY_UNKNOWN;
    }
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
        default:
            return std::nullopt;
    }

    return tc_ui_pointer_event{
        type,
        event.pointer.framebuffer_position.x,
        event.pointer.framebuffer_position.y,
        tcbase::mouse_button_value(event.pointer.button),
        event.pointer.clicks,
        translate_modifiers(event.pointer.modifiers),
        event.pointer.wheel_x,
        event.pointer.wheel_y};
}

std::optional<tc_ui_key_event> make_key_event(const WindowEvent& event) {
    if (event.type != WindowEventType::KeyPressed &&
        event.type != WindowEventType::KeyReleased) {
        return std::nullopt;
    }
    return tc_ui_key_event{
        event.type == WindowEventType::KeyPressed ? TC_UI_KEY_DOWN : TC_UI_KEY_UP,
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

tc_ui_event_result dispatch_window_event(
    TcDocument document,
    const WindowEvent& event) {
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
