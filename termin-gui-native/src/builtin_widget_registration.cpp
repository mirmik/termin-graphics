#include <termin/gui_native/builtin_widget_registration.hpp>

#include <string>

#include <tcbase/tc_log.h>
#include <termin/gui_native/tc_widget_registry.h>
#include <termin/gui_native/text_area.hpp>
#include <termin/gui_native/text_input.hpp>

#include "widgets_internal.hpp"

namespace termin::gui_native {
namespace {

constexpr const char* kBuiltinOwner = "termin-gui-native";
constexpr const char* kWidgetParent = "termin.gui.Widget";

void delete_native_widget(tc_widget* widget) {
    Widget::delete_owned_widget(widget);
}

template <typename T>
bool create_text_widget(tc_ui_document_handle, void*, tc_widget_factory_result* result) {
    if (!result) {
        return false;
    }
    auto* widget = new T();
    *result = tc_widget_factory_result{
        widget->c_widget(), &delete_native_widget, TC_WIDGET_OWNED};
    return true;
}

bool read_text_state(const tc_value* state, std::string& text, std::string& placeholder) {
    tc_value* text_value;
    tc_value* placeholder_value;
    if (!state || state->type != TC_VALUE_DICT || tc_value_dict_size(state) != 2) {
        tc_log_error("[termin-gui-native] text widget state must contain exactly text and placeholder");
        return false;
    }
    text_value = tc_value_dict_get(const_cast<tc_value*>(state), "text");
    placeholder_value = tc_value_dict_get(const_cast<tc_value*>(state), "placeholder");
    if (!text_value || !placeholder_value || text_value->type != TC_VALUE_STRING ||
        placeholder_value->type != TC_VALUE_STRING || !text_value->data.s ||
        !placeholder_value->data.s || !detail::valid_utf8(text_value->data.s) ||
        !detail::valid_utf8(placeholder_value->data.s)) {
        tc_log_error("[termin-gui-native] text widget state has invalid UTF-8 text or placeholder");
        return false;
    }
    text = text_value->data.s;
    placeholder = placeholder_value->data.s;
    return true;
}

template <typename T>
bool serialize_text_widget(const tc_widget* widget, void*, tc_value* state) {
    const auto* text_widget = static_cast<const T*>(widget->body);
    tc_value_dict_set(state, "text", tc_value_string(text_widget->text().c_str()));
    tc_value_dict_set(state, "placeholder", tc_value_string(text_widget->placeholder().c_str()));
    return true;
}

template <typename T>
bool deserialize_text_widget(tc_widget* widget, const tc_value* state, void*) {
    std::string text;
    std::string placeholder;
    if (!read_text_state(state, text, placeholder)) {
        return false;
    }
    auto* text_widget = static_cast<T*>(widget->body);
    text_widget->set_text(std::move(text));
    text_widget->set_placeholder(std::move(placeholder));
    return true;
}

template <typename T>
bool register_text_widget(const char* type_name) {
    if (tc_widget_registry_has(type_name)) {
        return true;
    }
    const tc_widget_factory_descriptor descriptor{
        TC_WIDGET_FACTORY_ABI_VERSION,
        TC_LANGUAGE_CXX,
        &create_text_widget<T>,
        nullptr,
        nullptr,
        nullptr,
        &serialize_text_widget<T>,
        &deserialize_text_widget<T>,
    };
    return tc_widget_registry_register(type_name, kBuiltinOwner, kWidgetParent, &descriptor);
}

} // namespace

bool register_builtin_widget_types() {
    return tc_widget_registry_initialize() &&
        register_text_widget<TextInput>(NativeWidgetRuntimeType<TextInput>::name) &&
        register_text_widget<TextArea>(NativeWidgetRuntimeType<TextArea>::name);
}

} // namespace termin::gui_native
