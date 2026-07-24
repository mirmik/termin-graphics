#pragma once

#include <termin/gui_native/export.h>

namespace termin::gui_native {

class TextArea;
class TextInput;

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

} // namespace termin::gui_native
