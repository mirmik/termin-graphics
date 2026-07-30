#include <termin/gui_native/builtin_widget_registration.hpp>

#include <algorithm>
#include <iterator>
#include <string>

#include <tcbase/tc_log.h>
#include <termin/gui_native/h_stack.hpp>
#include <termin/gui_native/icon_button.hpp>
#include <termin/gui_native/label.hpp>
#include <termin/gui_native/overlay_layout.hpp>
#include <termin/gui_native/panel.hpp>
#include <termin/gui_native/tc_widget_registry.h>
#include <termin/gui_native/text_area.hpp>
#include <termin/gui_native/text_input.hpp>
#include <termin/gui_native/v_stack.hpp>

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

template <typename T>
bool create_default_widget(tc_ui_document_handle, void*, tc_widget_factory_result* result) {
    if (!result) {
        return false;
    }
    auto* widget = new T();
    *result = tc_widget_factory_result{
        widget->c_widget(), &delete_native_widget, TC_WIDGET_OWNED};
    return true;
}

const tc_value* property(const tc_value* properties, const char* name) {
    return properties && properties->type == TC_VALUE_DICT
        ? tc_value_dict_get(const_cast<tc_value*>(properties), name)
        : nullptr;
}

bool color_property(const tc_value* properties, const char* name, Color& out) {
    const tc_value* value = property(properties, name);
    if (!value) {
        return false;
    }
    if (value->type != TC_VALUE_LIST ||
        (value->data.list.count != 3 && value->data.list.count != 4)) {
        return false;
    }
    auto channel = [&](size_t index, float fallback) {
        if (index >= value->data.list.count) {
            return fallback;
        }
        const tc_value* item = &value->data.list.items[index];
        if (item->type == TC_VALUE_INT) {
            return static_cast<float>(item->data.i);
        }
        if (item->type == TC_VALUE_FLOAT) {
            return item->data.f;
        }
        return item->type == TC_VALUE_DOUBLE
            ? static_cast<float>(item->data.d)
            : fallback;
    };
    out = Color{channel(0, 0.0f), channel(1, 0.0f), channel(2, 0.0f),
                channel(3, 1.0f)};
    return true;
}

float number_property(const tc_value* properties, const char* name, float fallback = 0.0f) {
    const tc_value* value = property(properties, name);
    if (!value) {
        return fallback;
    }
    if (value->type == TC_VALUE_INT) {
        return static_cast<float>(value->data.i);
    }
    if (value->type == TC_VALUE_FLOAT) {
        return value->data.f;
    }
    return value->type == TC_VALUE_DOUBLE
        ? static_cast<float>(value->data.d)
        : fallback;
}

bool apply_panel_properties(tc_widget* widget, const tc_value* properties) {
    Color background;
    if (color_property(properties, "background_color", background)) {
        auto* panel = static_cast<Panel*>(widget->body);
        panel->set_fill(background);
    }
    return true;
}

bool apply_overlay_properties(tc_widget* widget, const tc_value* properties) {
    Color background;
    if (color_property(properties, "background_color", background)) {
        tc_ui_style_override style = tc_widget_style_override(widget);
        style.value.background = background.c_color();
        style.fields |= TC_UI_STYLE_BACKGROUND;
        return tc_widget_set_style_override(widget, &style);
    }
    return true;
}

bool apply_box_properties(tc_widget* widget, const tc_value* properties) {
    auto* box = static_cast<BoxLayout*>(widget->body);
    if (property(properties, "spacing")) {
        box->set_spacing(number_property(properties, "spacing"));
    }
    return true;
}

bool apply_icon_button_properties(tc_widget* widget, const tc_value* properties) {
    auto* button = static_cast<IconButton*>(widget->body);
    if (const tc_value* icon = property(properties, "icon")) {
        button->set_icon(icon->data.s ? icon->data.s : "");
    }
    if (const tc_value* tooltip = property(properties, "tooltip")) {
        button->set_tooltip(tooltip->data.s ? tooltip->data.s : "");
    }
    if (const tc_value* active = property(properties, "active")) {
        button->set_active(active->data.b);
    }
    if (property(properties, "size")) {
        const float size = number_property(properties, "size");
        button->set_preferred_size({size, size});
    }
    if (property(properties, "border_radius")) {
        button->set_corner_radius(number_property(properties, "border_radius"));
    }
    if (property(properties, "font_size")) {
        button->set_font_size(number_property(properties, "font_size"));
    }
    struct ColorSetter {
        const char* name;
        void (IconButton::*setter)(Color);
    };
    static constexpr ColorSetter setters[] = {
        {"background_color", &IconButton::set_background_color},
        {"hover_color", &IconButton::set_hover_color},
        {"pressed_color", &IconButton::set_pressed_color},
        {"active_color", &IconButton::set_active_color},
        {"icon_color", &IconButton::set_icon_color},
    };
    for (const ColorSetter& item : setters) {
        Color color;
        if (color_property(properties, item.name, color)) {
            (button->*item.setter)(color);
        }
    }
    return true;
}

bool create_label(
    tc_ui_document_handle,
    void*,
    tc_widget_factory_result* result
) {
    if (!result) {
        return false;
    }
    auto* widget = new Label("");
    *result = tc_widget_factory_result{
        widget->c_widget(), &delete_native_widget, TC_WIDGET_OWNED};
    return true;
}

bool apply_label_properties(tc_widget* widget, const tc_value* properties) {
    auto* label = static_cast<Label*>(widget->body);
    if (const tc_value* text = property(properties, "text")) {
        label->set_text(text->data.s ? text->data.s : "");
    }
    if (property(properties, "font_size")) {
        label->set_font_size(number_property(properties, "font_size"));
    }
    Color color;
    if (color_property(properties, "color", color)) {
        label->set_color(color);
    }
    return true;
}

bool append_child(
    tc_widget* parent,
    tc_widget* child,
    const tc_value*
) {
    return tc_widget_append_child(parent, child);
}

bool append_box_child(
    tc_widget* parent,
    tc_widget* child,
    const tc_value*
) {
    static_cast<BoxLayout*>(parent->body)->add_child(child->handle);
    return child->parent == parent;
}

bool append_overlay_child(
    tc_widget* parent,
    tc_widget* child,
    const tc_value* properties
) {
    OverlayAnchor anchor = OverlayAnchor::Fill;
    if (const tc_value* value = property(properties, "anchor")) {
        const std::string text = value->data.s ? value->data.s : "";
        if (text == "top-left") anchor = OverlayAnchor::TopLeft;
        else if (text == "top-right") anchor = OverlayAnchor::TopRight;
        else if (text == "bottom-left") anchor = OverlayAnchor::BottomLeft;
        else if (text == "bottom-right") anchor = OverlayAnchor::BottomRight;
    }
    tc_ui_point offset{};
    if (const tc_value* value = property(properties, "offset")) {
        const tc_value* x = value->data.list.count > 0 ? &value->data.list.items[0] : nullptr;
        const tc_value* y = value->data.list.count > 1 ? &value->data.list.items[1] : nullptr;
        auto number = [](const tc_value* item) {
            if (!item) return 0.0f;
            if (item->type == TC_VALUE_INT) return static_cast<float>(item->data.i);
            if (item->type == TC_VALUE_FLOAT) return item->data.f;
            return item->type == TC_VALUE_DOUBLE
                ? static_cast<float>(item->data.d) : 0.0f;
        };
        offset = {number(x), number(y)};
    }
    return static_cast<OverlayLayout*>(parent->body)->add_child(
        child->handle, anchor, offset);
}

bool reject_declarative_persistence(const tc_widget* widget, void*, tc_value*) {
    tc_log_error(
        "[termin-gui-native] widget type '%s' is declarative-only and has no durable state codec",
        tc_widget_type_name(widget));
    return false;
}

bool reject_declarative_restore(tc_widget* widget, const tc_value*, void*) {
    tc_log_error(
        "[termin-gui-native] widget type '%s' cannot be restored through document persistence",
        tc_widget_type_name(widget));
    return false;
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
        nullptr,
    };
    return tc_widget_registry_register(type_name, kBuiltinOwner, kWidgetParent, &descriptor);
}

template <typename T>
bool register_declarative_widget(
    const char* type_name,
    const tc_uiscript_type_descriptor* uiscript
) {
    if (tc_widget_registry_has(type_name)) {
        return true;
    }
    const tc_widget_factory_descriptor descriptor{
        TC_WIDGET_FACTORY_ABI_VERSION,
        TC_LANGUAGE_CXX,
        &create_default_widget<T>,
        nullptr,
        nullptr,
        nullptr,
        &reject_declarative_persistence,
        &reject_declarative_restore,
        uiscript,
    };
    return tc_widget_registry_register(type_name, kBuiltinOwner, kWidgetParent, &descriptor);
}

constexpr const char* kPanelProperties[] = {"background_color"};
constexpr const char* kBoxProperties[] = {"spacing"};
constexpr const char* kIconButtonProperties[] = {
    "icon", "tooltip", "size", "font_size", "background_color",
    "hover_color", "pressed_color", "active_color", "icon_color",
    "border_radius", "active",
};
constexpr const char* kLabelProperties[] = {"text", "font_size", "color"};

const tc_uiscript_type_descriptor kOverlayUiScript{
    TC_UISCRIPT_TYPE_ABI_VERSION,
    kPanelProperties,
    std::size(kPanelProperties),
    &apply_overlay_properties,
    &append_overlay_child,
};
const tc_uiscript_type_descriptor kPanelUiScript{
    TC_UISCRIPT_TYPE_ABI_VERSION,
    kPanelProperties,
    std::size(kPanelProperties),
    &apply_panel_properties,
    &append_child,
};
const tc_uiscript_type_descriptor kBoxUiScript{
    TC_UISCRIPT_TYPE_ABI_VERSION,
    kBoxProperties,
    std::size(kBoxProperties),
    &apply_box_properties,
    &append_box_child,
};
const tc_uiscript_type_descriptor kIconButtonUiScript{
    TC_UISCRIPT_TYPE_ABI_VERSION,
    kIconButtonProperties,
    std::size(kIconButtonProperties),
    &apply_icon_button_properties,
    nullptr,
};
const tc_uiscript_type_descriptor kLabelUiScript{
    TC_UISCRIPT_TYPE_ABI_VERSION,
    kLabelProperties,
    std::size(kLabelProperties),
    &apply_label_properties,
    nullptr,
};

bool register_label_widget() {
    constexpr const char* type_name = NativeWidgetRuntimeType<Label>::name;
    if (tc_widget_registry_has(type_name)) {
        return true;
    }
    const tc_widget_factory_descriptor descriptor{
        TC_WIDGET_FACTORY_ABI_VERSION,
        TC_LANGUAGE_CXX,
        &create_label,
        nullptr,
        nullptr,
        nullptr,
        &reject_declarative_persistence,
        &reject_declarative_restore,
        &kLabelUiScript,
    };
    return tc_widget_registry_register(
        type_name, kBuiltinOwner, kWidgetParent, &descriptor);
}

} // namespace

bool register_builtin_widget_types() {
    return tc_widget_registry_initialize() &&
        register_text_widget<TextInput>(NativeWidgetRuntimeType<TextInput>::name) &&
        register_text_widget<TextArea>(NativeWidgetRuntimeType<TextArea>::name) &&
        register_declarative_widget<OverlayLayout>(
            NativeWidgetRuntimeType<OverlayLayout>::name, &kOverlayUiScript) &&
        register_declarative_widget<Panel>(
            NativeWidgetRuntimeType<Panel>::name, &kPanelUiScript) &&
        register_declarative_widget<HStack>(
            NativeWidgetRuntimeType<HStack>::name, &kBoxUiScript) &&
        register_declarative_widget<VStack>(
            NativeWidgetRuntimeType<VStack>::name, &kBoxUiScript) &&
        register_label_widget() &&
        register_declarative_widget<IconButton>(
            NativeWidgetRuntimeType<IconButton>::name, &kIconButtonUiScript);
}

} // namespace termin::gui_native
