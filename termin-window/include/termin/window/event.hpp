#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <tcbase/input_enums.hpp>

namespace termin {

enum class WindowEventType : uint8_t {
    None,
    CloseRequested,
    Resized,
    PointerMoved,
    PointerButtonPressed,
    PointerButtonReleased,
    PointerWheel,
    KeyPressed,
    KeyReleased,
    TextInput,
};

enum class WindowKey : uint16_t {
    Unknown,
    Tab,
    Enter,
    Space,
    Escape,
    Backspace,
    Delete,
    Right,
    Left,
    Down,
    Up,
    Home,
    End,
    Insert,
    PageUp,
    PageDown,
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
};

enum WindowModifier : uint32_t {
    WindowModifierNone = 0,
    WindowModifierShift = 1u << 0,
    WindowModifierControl = 1u << 1,
    WindowModifierAlt = 1u << 2,
    WindowModifierSuper = 1u << 3,
};

struct WindowPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct WindowPointerEvent {
    // Logical coordinates use the native window coordinate system. Framebuffer
    // coordinates are ready for pixel-addressed rendering and hit testing.
    WindowPoint logical_position;
    WindowPoint framebuffer_position;
    tcbase::MouseButton button = tcbase::MouseButton::NONE;
    uint32_t clicks = 0;
    uint32_t modifiers = WindowModifierNone;
    float wheel_x = 0.0f;
    float wheel_y = 0.0f;
};

struct WindowKeyEvent {
    WindowKey key = WindowKey::Unknown;
    // Native values are an escape hatch for input systems that retain their
    // own key map. Portable consumers should use `key`.
    int32_t native_key = 0;
    int32_t native_scancode = 0;
    uint32_t modifiers = WindowModifierNone;
    bool repeat = false;
};

struct WindowTextEvent {
    static constexpr size_t Capacity = 32;
    std::array<char, Capacity> utf8{};
};

struct WindowResizeEvent {
    int width = 0;
    int height = 0;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
};

struct WindowEvent {
    WindowEventType type = WindowEventType::None;
    WindowPointerEvent pointer;
    WindowKeyEvent key;
    WindowTextEvent text;
    WindowResizeEvent resize;
};

} // namespace termin
