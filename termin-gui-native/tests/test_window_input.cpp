#include <cassert>
#include <cstring>

#include <termin/gui_native/window_input.hpp>

int main() {
    termin::WindowEvent pointer;
    pointer.type = termin::WindowEventType::PointerButtonPressed;
    pointer.pointer.logical_position = {12.0f, 20.0f};
    pointer.pointer.framebuffer_position = {18.0f, 30.0f};
    pointer.pointer.button = tcbase::MouseButton::RIGHT;
    pointer.pointer.clicks = 2;
    pointer.pointer.modifiers =
        termin::WindowModifierShift | termin::WindowModifierControl;

    const auto gui_pointer = termin::gui_native::make_pointer_event(pointer);
    assert(gui_pointer.has_value());
    assert(gui_pointer->type == TC_UI_POINTER_DOWN);
    assert(gui_pointer->x == 18.0f);
    assert(gui_pointer->y == 30.0f);
    assert(gui_pointer->button == 1);
    assert(gui_pointer->click_count == 2);
    assert(gui_pointer->modifiers == (TC_UI_MOD_SHIFT | TC_UI_MOD_CTRL));

    termin::WindowEvent wheel;
    wheel.type = termin::WindowEventType::PointerWheel;
    wheel.pointer.framebuffer_position = {40.0f, 50.0f};
    wheel.pointer.wheel_x = -1.0f;
    wheel.pointer.wheel_y = 2.0f;
    const auto gui_wheel = termin::gui_native::make_pointer_event(wheel);
    assert(gui_wheel.has_value());
    assert(gui_wheel->type == TC_UI_POINTER_WHEEL);
    assert(gui_wheel->wheel_x == -1.0f);
    assert(gui_wheel->wheel_y == 2.0f);

    termin::WindowEvent key;
    key.type = termin::WindowEventType::KeyPressed;
    key.key.key = termin::WindowKey::C;
    key.key.native_scancode = 6;
    key.key.modifiers = termin::WindowModifierControl;
    key.key.repeat = true;
    const auto gui_key = termin::gui_native::make_key_event(key);
    assert(gui_key.has_value());
    assert(gui_key->type == TC_UI_KEY_DOWN);
    assert(gui_key->key == TC_UI_KEY_C);
    assert(gui_key->scancode == 6);
    assert(gui_key->modifiers == TC_UI_MOD_CTRL);
    assert(gui_key->repeat);

    termin::WindowEvent text;
    text.type = termin::WindowEventType::TextInput;
    std::strcpy(text.text.utf8.data(), "A\xD0\x96");
    const auto gui_text = termin::gui_native::make_text_event(text);
    assert(gui_text.has_value());
    assert(std::strcmp(gui_text->text, "A\xD0\x96") == 0);

    termin::WindowEvent resize;
    resize.type = termin::WindowEventType::Resized;
    assert(!termin::gui_native::make_pointer_event(resize).has_value());
    assert(!termin::gui_native::make_key_event(resize).has_value());
    assert(!termin::gui_native::make_text_event(resize).has_value());
    return 0;
}
