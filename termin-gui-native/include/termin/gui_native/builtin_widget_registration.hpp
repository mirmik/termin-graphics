#pragma once

#include <termin/gui_native/export.h>

namespace termin::gui_native {

class TextArea;
class TextInput;
class OverlayLayout;
class Panel;
class BoxLayout;
class GridLayout;
class WrapLayout;
class ScrollArea;
class HStack;
class VStack;
class IconButton;
class Label;

TERMIN_GUI_NATIVE_API bool register_builtin_widget_types();

template <typename T>
struct NativeWidgetRuntimeType {
    static constexpr const char* name = nullptr;
};

template <>
struct NativeWidgetRuntimeType<TextInput> {
    static constexpr const char* name = "termin.gui.TextInput";
};

template <>
struct NativeWidgetRuntimeType<TextArea> {
    static constexpr const char* name = "termin.gui.TextArea";
};

template <>
struct NativeWidgetRuntimeType<OverlayLayout> {
    static constexpr const char* name = "termin.gui.OverlayLayout";
};

template <>
struct NativeWidgetRuntimeType<Panel> {
    static constexpr const char* name = "termin.gui.Panel";
};

template <>
struct NativeWidgetRuntimeType<BoxLayout> {
    static constexpr const char* name = "termin.gui.BoxLayout";
};

template <>
struct NativeWidgetRuntimeType<GridLayout> {
    static constexpr const char* name = "termin.gui.GridLayout";
};

template <>
struct NativeWidgetRuntimeType<WrapLayout> {
    static constexpr const char* name = "termin.gui.WrapLayout";
};

template <>
struct NativeWidgetRuntimeType<ScrollArea> {
    static constexpr const char* name = "termin.gui.ScrollArea";
};

template <>
struct NativeWidgetRuntimeType<HStack> {
    static constexpr const char* name = "termin.gui.HStack";
};

template <>
struct NativeWidgetRuntimeType<VStack> {
    static constexpr const char* name = "termin.gui.VStack";
};

template <>
struct NativeWidgetRuntimeType<IconButton> {
    static constexpr const char* name = "termin.gui.IconButton";
};

template <>
struct NativeWidgetRuntimeType<Label> {
    static constexpr const char* name = "termin.gui.Label";
};

} // namespace termin::gui_native
