#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <termin/gui_native/builtin_widget_registration.hpp>
#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/tc_widget_registry.h>

namespace termin::gui_native {

    class DocumentBuilder {
    private:
        TcDocument document_;

    public:
        explicit DocumentBuilder(TcDocument document)
            : document_(document) {}
        template <typename T, typename... Args> T& make(Args&&... args) {
            static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
            auto widget = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *widget;
            tc_widget_handle handle = tc_widget_handle_invalid();
            if constexpr (NativeWidgetRuntimeType<T>::name) {
                if (!register_builtin_widget_types()) {
                    throw std::runtime_error("failed to register built-in native UI widget types");
                }
                tc_widget_factory_result result{
                    widget.release()->c_widget(), &Widget::delete_owned_widget, TC_WIDGET_OWNED};
                if (!tc_ui_document_adopt_registered_widget(
                        document_.get(), NativeWidgetRuntimeType<T>::name, &result, &handle)) {
                    throw std::runtime_error("failed to adopt registered native UI widget");
                }
            } else {
                handle = document_.adopt(widget.get());
                if (!tc_widget_handle_is_invalid(handle)) {
                    widget.release();
                }
            }
            if (tc_widget_handle_is_invalid(handle)) {
                throw std::runtime_error("failed to adopt native UI widget");
            }
            return ref;
        }
        template <typename T, typename... Args> T& make_root(Args&&... args) {
            T& widget = make<T>(std::forward<Args>(args)...);
            if (!document_.add_root(widget))
                throw std::runtime_error("failed to add native UI root widget");
            return widget;
        }
    };

} // namespace termin::gui_native
