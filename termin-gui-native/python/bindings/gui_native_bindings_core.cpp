#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_core(nb::module_& m) {

    nb::class_<WidgetHandle>(m, "WidgetHandle")
        .def_prop_ro("index", [](const WidgetHandle& handle) { return handle.handle.index; })
        .def_prop_ro("generation", [](const WidgetHandle& handle) { return handle.handle.generation; })
        .def_prop_ro("valid", [](const WidgetHandle& handle) {
            return tc_widget_handle_valid_value(handle.handle);
        })
        .def("__bool__", [](const WidgetHandle& handle) {
            return tc_widget_handle_valid_value(handle.handle);
        })
        .def("__eq__", [](const WidgetHandle& lhs, const WidgetHandle& rhs) {
            return tc_widget_handle_eq(lhs.handle, rhs.handle);
        });

    m.def("invalid_widget_handle", []() {
        return WidgetHandle {tc_widget_handle_invalid_value()};
    });

    nb::class_<tc_ui_size>(m, "Size")
        .def(nb::init<float, float>(), nb::arg("width") = 0.0f, nb::arg("height") = 0.0f)
        .def_rw("width", &tc_ui_size::width)
        .def_rw("height", &tc_ui_size::height);

    nb::class_<tc_ui_rect>(m, "Rect")
        .def(nb::init<float, float, float, float>(),
             nb::arg("x") = 0.0f, nb::arg("y") = 0.0f,
             nb::arg("width") = 0.0f, nb::arg("height") = 0.0f)
        .def_rw("x", &tc_ui_rect::x)
        .def_rw("y", &tc_ui_rect::y)
        .def_rw("width", &tc_ui_rect::width)
        .def_rw("height", &tc_ui_rect::height);

    nb::class_<tc_ui_point>(m, "Point")
        .def(nb::init<float, float>(), nb::arg("x") = 0.0f, nb::arg("y") = 0.0f)
        .def_rw("x", &tc_ui_point::x)
        .def_rw("y", &tc_ui_point::y);

    nb::class_<tc_ui_color>(m, "Color")
        .def(nb::init<float, float, float, float>(), nb::arg("r") = 0.0f, nb::arg("g") = 0.0f,
             nb::arg("b") = 0.0f, nb::arg("a") = 1.0f)
        .def_rw("r", &tc_ui_color::r)
        .def_rw("g", &tc_ui_color::g)
        .def_rw("b", &tc_ui_color::b)
        .def_rw("a", &tc_ui_color::a);

    nb::class_<termin::gui_native::EdgeInsets>(m, "EdgeInsets")
        .def(nb::init<float, float, float, float>(), nb::arg("left") = 0.0f, nb::arg("top") = 0.0f,
             nb::arg("right") = 0.0f, nb::arg("bottom") = 0.0f)
        .def_rw("left", &termin::gui_native::EdgeInsets::left)
        .def_rw("top", &termin::gui_native::EdgeInsets::top)
        .def_rw("right", &termin::gui_native::EdgeInsets::right)
        .def_rw("bottom", &termin::gui_native::EdgeInsets::bottom);

    nb::enum_<termin::gui_native::LayoutPolicy>(m, "LayoutPolicy")
        .value("Fixed", termin::gui_native::LayoutPolicy::Fixed)
        .value("Preferred", termin::gui_native::LayoutPolicy::Preferred)
        .value("Flex", termin::gui_native::LayoutPolicy::Flex)
        .value("Stretch", termin::gui_native::LayoutPolicy::Stretch);

    nb::enum_<tc_ui_style_role>(m, "StyleRole")
        .value("Generic", TC_UI_STYLE_GENERIC)
        .value("Panel", TC_UI_STYLE_PANEL)
        .value("Label", TC_UI_STYLE_LABEL)
        .value("Button", TC_UI_STYLE_BUTTON)
        .value("TextInput", TC_UI_STYLE_TEXT_INPUT)
        .value("GroupBox", TC_UI_STYLE_GROUP_BOX)
        .value("Tab", TC_UI_STYLE_TAB)
        .value("Checkbox", TC_UI_STYLE_CHECKBOX)
        .value("Progress", TC_UI_STYLE_PROGRESS)
        .value("Slider", TC_UI_STYLE_SLIDER)
        .value("Separator", TC_UI_STYLE_SEPARATOR);

    nb::enum_<tc_ui_font_role>(m, "FontRole")
        .value("Body", TC_UI_FONT_BODY)
        .value("Small", TC_UI_FONT_SMALL)
        .value("Title", TC_UI_FONT_TITLE)
        .value("Monospace", TC_UI_FONT_MONOSPACE);

    nb::enum_<tc_ui_style_state_flag>(m, "StyleState", nb::is_arithmetic())
        .value("Hovered", TC_UI_STYLE_STATE_HOVERED)
        .value("Pressed", TC_UI_STYLE_STATE_PRESSED)
        .value("Focused", TC_UI_STYLE_STATE_FOCUSED)
        .value("Disabled", TC_UI_STYLE_STATE_DISABLED)
        .value("Checked", TC_UI_STYLE_STATE_CHECKED);

    nb::enum_<StyleField>(m, "StyleField", nb::is_arithmetic())
        .value("Background", StyleField::Background)
        .value("Foreground", StyleField::Foreground)
        .value("Border", StyleField::Border)
        .value("Accent", StyleField::Accent)
        .value("PaddingLeft", StyleField::PaddingLeft)
        .value("PaddingTop", StyleField::PaddingTop)
        .value("PaddingRight", StyleField::PaddingRight)
        .value("PaddingBottom", StyleField::PaddingBottom)
        .value("Spacing", StyleField::Spacing)
        .value("BorderWidth", StyleField::BorderWidth)
        .value("FontSize", StyleField::FontSize)
        .value("MinWidth", StyleField::MinWidth)
        .value("MinHeight", StyleField::MinHeight)
        .value("FontRole", StyleField::FontRole)
        .value("CornerRadius", StyleField::CornerRadius)
        .value("All", StyleField::All);

    nb::enum_<tc_ui_style_override_flag>(m, "StyleOverrideFlag", nb::is_arithmetic())
        .value("Inherit", TC_UI_STYLE_OVERRIDE_INHERIT);

    nb::class_<tc_ui_style>(m, "Style")
        .def(nb::init<>())
        .def_rw("background", &tc_ui_style::background)
        .def_rw("foreground", &tc_ui_style::foreground)
        .def_rw("border", &tc_ui_style::border)
        .def_rw("accent", &tc_ui_style::accent)
        .def_rw("padding_left", &tc_ui_style::padding_left)
        .def_rw("padding_top", &tc_ui_style::padding_top)
        .def_rw("padding_right", &tc_ui_style::padding_right)
        .def_rw("padding_bottom", &tc_ui_style::padding_bottom)
        .def_rw("spacing", &tc_ui_style::spacing)
        .def_rw("border_width", &tc_ui_style::border_width)
        .def_rw("font_size", &tc_ui_style::font_size)
        .def_rw("min_width", &tc_ui_style::min_width)
        .def_rw("min_height", &tc_ui_style::min_height)
        .def_rw("corner_radius", &tc_ui_style::corner_radius)
        .def_rw("font_role", &tc_ui_style::font_role);

    nb::class_<tc_ui_style_override>(m, "StyleOverride")
        .def(nb::init<>())
        .def_rw("value", &tc_ui_style_override::value)
        .def_prop_rw("fields",
            [](const tc_ui_style_override& self) {
                return static_cast<StyleField>(self.fields);
            },
            [](tc_ui_style_override& self, uint64_t fields) {
                self.fields = fields;
            })
        .def_rw("flags", &tc_ui_style_override::flags);

    nb::class_<tc_ui_role_style>(m, "RoleStyle")
        .def(nb::init<>())
        .def_rw("base", &tc_ui_role_style::base)
        .def_rw("hovered", &tc_ui_role_style::hovered)
        .def_rw("pressed", &tc_ui_role_style::pressed)
        .def_rw("focused", &tc_ui_role_style::focused)
        .def_rw("disabled", &tc_ui_role_style::disabled)
        .def_rw("checked", &tc_ui_role_style::checked);

    nb::class_<Theme>(m, "Theme")
        .def(nb::init<>())
        .def("role", &Theme::role, nb::arg("role"), nb::rv_policy::reference_internal)
        .def("set_role", [](Theme& self, tc_ui_style_role role, const tc_ui_role_style& value) {
            self.role(role) = value;
        }, nb::arg("role"), nb::arg("value"));

    nb::class_<tc_ui_constraints>(m, "Constraints")
        .def("__init__", [](tc_ui_constraints* self,
                             std::optional<tc_ui_size> min_size,
                             std::optional<tc_ui_size> max_size) {
            new (self) tc_ui_constraints{
                min_size.value_or(tc_ui_size{0.0f, 0.0f}),
                max_size.value_or(tc_ui_size{0.0f, 0.0f})};
        }, nb::arg("min_size").none() = nb::none(),
           nb::arg("max_size").none() = nb::none())
        .def_rw("min_size", &tc_ui_constraints::min_size)
        .def_rw("max_size", &tc_ui_constraints::max_size);

    nb::class_<tc_ui_text_metrics>(m, "TextMetrics")
        .def_ro("width", &tc_ui_text_metrics::width)
        .def_ro("height", &tc_ui_text_metrics::height)
        .def_ro("ascent", &tc_ui_text_metrics::ascent)
        .def_ro("descent", &tc_ui_text_metrics::descent)
        .def_ro("line_height", &tc_ui_text_metrics::line_height);

    nb::enum_<tc_ui_event_result>(m, "EventResult")
        .value("Ignored", TC_UI_EVENT_IGNORED)
        .value("Handled", TC_UI_EVENT_HANDLED);

    nb::enum_<tc_ui_pointer_event_type>(m, "PointerEventType")
        .value("Move", TC_UI_POINTER_MOVE)
        .value("Down", TC_UI_POINTER_DOWN)
        .value("Up", TC_UI_POINTER_UP)
        .value("Wheel", TC_UI_POINTER_WHEEL)
        .value("Enter", TC_UI_POINTER_ENTER)
        .value("Leave", TC_UI_POINTER_LEAVE);

    nb::enum_<tc_ui_cursor_intent>(m, "CursorIntent")
        .value("Inherit", TC_UI_CURSOR_INHERIT)
        .value("Default", TC_UI_CURSOR_DEFAULT)
        .value("Text", TC_UI_CURSOR_TEXT)
        .value("Hand", TC_UI_CURSOR_HAND)
        .value("Crosshair", TC_UI_CURSOR_CROSSHAIR)
        .value("Move", TC_UI_CURSOR_MOVE)
        .value("ResizeHorizontal", TC_UI_CURSOR_RESIZE_HORIZONTAL)
        .value("ResizeVertical", TC_UI_CURSOR_RESIZE_VERTICAL)
        .value("ResizeNwse", TC_UI_CURSOR_RESIZE_NWSE)
        .value("ResizeNesw", TC_UI_CURSOR_RESIZE_NESW);

    nb::enum_<tc_ui_modifier_flag>(m, "ModifierFlag", nb::is_arithmetic())
        .value("Shift", TC_UI_MOD_SHIFT)
        .value("Ctrl", TC_UI_MOD_CTRL)
        .value("Alt", TC_UI_MOD_ALT)
        .value("Super", TC_UI_MOD_SUPER);

    nb::enum_<tc_ui_overlay_flag>(m, "OverlayFlag", nb::is_arithmetic())
        .value("Modal", TC_UI_OVERLAY_MODAL)
        .value("DismissOnOutside", TC_UI_OVERLAY_DISMISS_ON_OUTSIDE)
        .value("PointerTransparent", TC_UI_OVERLAY_POINTER_TRANSPARENT)
        .value("Tooltip", TC_UI_OVERLAY_TOOLTIP)
        .value("AllowRootHit", TC_UI_OVERLAY_ALLOW_ROOT_HIT);

    nb::enum_<tc_ui_overlay_dismiss_reason>(m, "OverlayDismissReason")
        .value("Programmatic", TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
        .value("Outside", TC_UI_OVERLAY_DISMISS_OUTSIDE)
        .value("Escape", TC_UI_OVERLAY_DISMISS_ESCAPE);

    nb::class_<tc_ui_pointer_event>(m, "PointerEvent")
        .def(nb::init<>())
        .def_rw("type", &tc_ui_pointer_event::type)
        .def_rw("x", &tc_ui_pointer_event::x)
        .def_rw("y", &tc_ui_pointer_event::y)
        .def_rw("button", &tc_ui_pointer_event::button)
        .def_rw("click_count", &tc_ui_pointer_event::click_count)
        .def_rw("modifiers", &tc_ui_pointer_event::modifiers)
        .def_rw("wheel_x", &tc_ui_pointer_event::wheel_x)
        .def_rw("wheel_y", &tc_ui_pointer_event::wheel_y);

    nb::enum_<tc_ui_key_event_type>(m, "KeyEventType")
        .value("Down", TC_UI_KEY_DOWN)
        .value("Up", TC_UI_KEY_UP);

    nb::enum_<tc_ui_key_code>(m, "KeyCode")
        .value("Unknown", TC_UI_KEY_UNKNOWN)
        .value("Backspace", TC_UI_KEY_BACKSPACE)
        .value("Tab", TC_UI_KEY_TAB)
        .value("Enter", TC_UI_KEY_ENTER)
        .value("Space", TC_UI_KEY_SPACE)
        .value("Key0", TC_UI_KEY_0)
        .value("Key1", TC_UI_KEY_1)
        .value("Key2", TC_UI_KEY_2)
        .value("Key3", TC_UI_KEY_3)
        .value("Key4", TC_UI_KEY_4)
        .value("Key5", TC_UI_KEY_5)
        .value("Key6", TC_UI_KEY_6)
        .value("Key7", TC_UI_KEY_7)
        .value("Key8", TC_UI_KEY_8)
        .value("Key9", TC_UI_KEY_9)
        .value("Escape", TC_UI_KEY_ESCAPE)
        .value("A", TC_UI_KEY_A)
        .value("B", TC_UI_KEY_B)
        .value("C", TC_UI_KEY_C)
        .value("D", TC_UI_KEY_D)
        .value("E", TC_UI_KEY_E)
        .value("F", TC_UI_KEY_F)
        .value("G", TC_UI_KEY_G)
        .value("H", TC_UI_KEY_H)
        .value("I", TC_UI_KEY_I)
        .value("J", TC_UI_KEY_J)
        .value("K", TC_UI_KEY_K)
        .value("L", TC_UI_KEY_L)
        .value("M", TC_UI_KEY_M)
        .value("N", TC_UI_KEY_N)
        .value("O", TC_UI_KEY_O)
        .value("P", TC_UI_KEY_P)
        .value("Q", TC_UI_KEY_Q)
        .value("R", TC_UI_KEY_R)
        .value("S", TC_UI_KEY_S)
        .value("T", TC_UI_KEY_T)
        .value("U", TC_UI_KEY_U)
        .value("V", TC_UI_KEY_V)
        .value("W", TC_UI_KEY_W)
        .value("X", TC_UI_KEY_X)
        .value("Y", TC_UI_KEY_Y)
        .value("Z", TC_UI_KEY_Z)
        .value("Delete", TC_UI_KEY_DELETE)
        .value("Left", TC_UI_KEY_LEFT)
        .value("Right", TC_UI_KEY_RIGHT)
        .value("Home", TC_UI_KEY_HOME)
        .value("End", TC_UI_KEY_END)
        .value("Up", TC_UI_KEY_UP_ARROW)
        .value("Down", TC_UI_KEY_DOWN_ARROW)
        .value("F1", TC_UI_KEY_F1)
        .value("F2", TC_UI_KEY_F2)
        .value("F3", TC_UI_KEY_F3)
        .value("F4", TC_UI_KEY_F4)
        .value("F5", TC_UI_KEY_F5)
        .value("F6", TC_UI_KEY_F6)
        .value("F7", TC_UI_KEY_F7)
        .value("F8", TC_UI_KEY_F8)
        .value("F9", TC_UI_KEY_F9)
        .value("F10", TC_UI_KEY_F10)
        .value("F11", TC_UI_KEY_F11)
        .value("F12", TC_UI_KEY_F12);

    nb::class_<tc_ui_key_event>(m, "KeyEvent")
        .def(nb::init<>())
        .def_rw("type", &tc_ui_key_event::type)
        .def_prop_rw("key",
            [](const tc_ui_key_event& event) {
                return static_cast<tc_ui_key_code>(event.key);
            },
            [](tc_ui_key_event& event, tc_ui_key_code key) {
                event.key = static_cast<int32_t>(key);
            })
        .def_rw("scancode", &tc_ui_key_event::scancode)
        .def_rw("modifiers", &tc_ui_key_event::modifiers)
        .def_rw("repeat", &tc_ui_key_event::repeat);

    m.def("tooltip_rect", [](tc_ui_rect viewport, tc_ui_point anchor,
                              tc_ui_size preferred_size,
                              std::optional<tc_ui_point> offset, float margin) {
        return tc_ui_tooltip_rect(
            viewport, anchor, preferred_size,
            offset.value_or(tc_ui_point{12.0f, 18.0f}), margin);
    }, nb::arg("viewport"), nb::arg("anchor"), nb::arg("preferred_size"),
       nb::arg("offset").none() = nb::none(), nb::arg("margin") = 4.0f);

    nb::enum_<tc_widget_flag>(m, "WidgetFlag", nb::is_arithmetic())
        .value("Focusable", TC_WIDGET_FOCUSABLE)
        .value("DirtyLayout", TC_WIDGET_DIRTY_LAYOUT)
        .value("DirtyPaint", TC_WIDGET_DIRTY_PAINT)
        .value("DirtyState", TC_WIDGET_DIRTY_STATE)
        .value("Visible", TC_WIDGET_VISIBLE)
        .value("Enabled", TC_WIDGET_ENABLED)
        .value("MouseTransparent", TC_WIDGET_MOUSE_TRANSPARENT);

    nb::enum_<tc_language>(m, "WidgetLanguage")
        .value("C", TC_LANGUAGE_C)
        .value("Cpp", TC_LANGUAGE_CXX)
        .value("Python", TC_LANGUAGE_PYTHON)
        .value("Rust", TC_LANGUAGE_RUST)
        .value("CSharp", TC_LANGUAGE_CSHARP);

    nb::enum_<tc_widget_ownership_policy>(m, "WidgetOwnership")
        .value("Owned", TC_WIDGET_OWNED)
        .value("Borrowed", TC_WIDGET_BORROWED);

    nb::enum_<tc_widget_owner_reload_policy>(m, "WidgetOwnerReloadPolicy")
        .value("Invalidate", TC_WIDGET_OWNER_RELOAD_INVALIDATE);

    m.def(
        "register_widget_type",
        [](const std::string& type_name, nb::object factory, const std::string& owner,
           const std::string& parent_type, const std::string& debug_name,
           nb::object serialize_state, nb::object deserialize_state) {
            if (!PyCallable_Check(factory.ptr()))
                throw std::invalid_argument("widget factory must be callable");
            const bool has_serializer = !serialize_state.is_none();
            const bool has_deserializer = !deserialize_state.is_none();
            if (has_serializer != has_deserializer)
                throw std::invalid_argument(
                    "serialize_state and deserialize_state must be provided together");
            if ((has_serializer && !PyCallable_Check(serialize_state.ptr())) ||
                (has_deserializer && !PyCallable_Check(deserialize_state.ptr())))
                throw std::invalid_argument("widget state hooks must be callable");
            auto* payload = new PythonWidgetFactory{
                std::move(factory),
                debug_name.empty() ? type_name : debug_name,
                std::move(serialize_state),
                std::move(deserialize_state),
            };
            const tc_widget_factory_descriptor descriptor{
                TC_WIDGET_FACTORY_ABI_VERSION,
                TC_LANGUAGE_PYTHON,
                &create_python_registered_widget,
                &bind_python_registered_widget,
                &destroy_python_widget_factory,
                payload,
                &serialize_python_registered_widget,
                &deserialize_python_registered_widget,
            };
            if (!tc_widget_registry_register(
                    type_name.c_str(),
                    owner.empty() ? nullptr : owner.c_str(),
                    parent_type.empty() ? nullptr : parent_type.c_str(),
                    &descriptor)) {
                destroy_python_widget_factory(payload);
                throw std::runtime_error("failed to register widget type '" + type_name + "'");
            }
        },
        nb::arg("type_name"), nb::arg("factory"), nb::arg("owner") = "python",
        nb::arg("parent_type") = "termin.gui.Widget", nb::arg("debug_name") = "",
        nb::arg("serialize_state") = nb::none(), nb::arg("deserialize_state") = nb::none());
    m.def(
        "unregister_widget_type",
        [](const std::string& type_name) {
            return tc_widget_registry_unregister(type_name.c_str());
        },
        nb::arg("type_name"));
    m.def(
        "unregister_widget_owner",
        [](const std::string& owner, tc_widget_owner_reload_policy policy) {
            return tc_widget_registry_unregister_owner(owner.c_str(), policy);
        },
        nb::arg("owner"),
        nb::arg("policy") = TC_WIDGET_OWNER_RELOAD_INVALIDATE);
    m.def("has_widget_type",
          [](const std::string& type_name) {
              return tc_widget_registry_has(type_name.c_str());
          },
          nb::arg("type_name"));
    m.def("registered_widget_types", []() {
        std::vector<std::string> result;
        const size_t count = tc_widget_registry_type_count();
        result.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            const char* type_name = tc_widget_registry_type_at(index);
            if (type_name)
                result.emplace_back(type_name);
        }
        return result;
    });

}
