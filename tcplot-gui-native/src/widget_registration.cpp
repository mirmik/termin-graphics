#include <tcplot/gui_native/widget_registration.hpp>

#include <exception>
#include <iterator>
#include <string>
#include <string_view>

#include <tcbase/tc_log.h>
#include <tcplot/gui_native/plot2d.hpp>
#include <termin/gui_native/builtin_widget_registration.hpp>
#include <termin/gui_native/tc_widget_registry.h>

extern "C" {
#include <tc_value.h>
}

namespace tcplot::gui_native {
    namespace {

        constexpr const char* widget_type = "termin.gui.Plot2D";
        constexpr const char* module_owner = "tcplot-gui-native";
        constexpr const char* widget_parent = "termin.gui.Widget";

        void delete_widget(tc_widget* widget) {
            termin::gui_native::Widget::delete_owned_widget(widget);
        }

        bool create_widget(tc_ui_document_handle, void*, tc_widget_factory_result* result) {
            if (result == nullptr) {
                return false;
            }
            try {
                auto* widget = new Plot2D();
                *result = tc_widget_factory_result{
                    widget->c_widget(),
                    &delete_widget,
                    TC_WIDGET_OWNED,
                };
                return true;
            } catch (const std::exception& error) {
                tc_log_error("[tcplot-gui-native] failed to create Plot2D: %s", error.what());
                return false;
            } catch (...) {
                tc_log_error("[tcplot-gui-native] failed to create Plot2D with an "
                             "unknown exception");
                return false;
            }
        }

        const tc_value* property(const tc_value* properties, const char* name) {
            return properties != nullptr && properties->type == TC_VALUE_DICT
                       ? tc_value_dict_get(const_cast<tc_value*>(properties), name)
                       : nullptr;
        }

        std::string string_property(const tc_value* properties, const char* name) {
            const tc_value* value = property(properties, name);
            return value != nullptr && value->type == TC_VALUE_STRING && value->data.s != nullptr ? value->data.s
                                                                                                  : std::string{};
        }

        bool validate_property(const char* name, const tc_value* value) {
            if (name == nullptr || value == nullptr) {
                return false;
            }
            const std::string_view property_name{name};
            if (property_name == "title" || property_name == "x_label" || property_name == "y_label") {
                return value->type == TC_VALUE_STRING;
            }
            if (property_name == "auto_fit") {
                return value->type == TC_VALUE_BOOL;
            }
            return false;
        }

        bool apply_properties(tc_widget* widget, const tc_value* properties) {
            if (widget == nullptr || widget->body == nullptr) {
                return false;
            }
            auto* plot = static_cast<Plot2D*>(widget->body);
            if (property(properties, "title") != nullptr) {
                plot->set_title(string_property(properties, "title"));
            }
            if (property(properties, "x_label") != nullptr) {
                plot->set_x_label(string_property(properties, "x_label"));
            }
            if (property(properties, "y_label") != nullptr) {
                plot->set_y_label(string_property(properties, "y_label"));
            }
            if (const tc_value* auto_fit = property(properties, "auto_fit")) {
                if (auto_fit->type != TC_VALUE_BOOL) {
                    tc_log_error("[tcplot-gui-native] Plot2D auto_fit must be bool");
                    return false;
                }
                plot->set_auto_fit(auto_fit->data.b);
            }
            return true;
        }

        bool reject_persistence(const tc_widget*, void*, tc_value*) {
            tc_log_error("[tcplot-gui-native] Plot2D is declarative-only and has no "
                         "durable widget state codec");
            return false;
        }

        bool reject_restore(tc_widget*, const tc_value*, void*) {
            tc_log_error("[tcplot-gui-native] Plot2D cannot be restored through "
                         "document persistence");
            return false;
        }

        constexpr const char* properties[] = {
            "title",
            "x_label",
            "y_label",
            "auto_fit",
        };

        const tc_uiscript_type_descriptor uiscript_descriptor{
            TC_UISCRIPT_TYPE_ABI_VERSION,
            properties,
            std::size(properties),
            &validate_property,
            &apply_properties,
            nullptr,
            nullptr,
            0,
            0,
        };

    } // namespace

    bool register_plot_widget_types() {
        if (!termin::gui_native::register_builtin_widget_types()) {
            tc_log_error("[tcplot-gui-native] built-in widget registration failed");
            return false;
        }
        if (tc_widget_registry_has(widget_type)) {
            return true;
        }
        const tc_widget_factory_descriptor descriptor{
            TC_WIDGET_FACTORY_ABI_VERSION,
            TC_LANGUAGE_CXX,
            &create_widget,
            nullptr,
            nullptr,
            nullptr,
            &reject_persistence,
            &reject_restore,
            &uiscript_descriptor,
        };
        if (!tc_widget_registry_register(widget_type, module_owner, widget_parent, &descriptor)) {
            tc_log_error("[tcplot-gui-native] failed to register termin.gui.Plot2D");
            return false;
        }
        return true;
    }

} // namespace tcplot::gui_native
