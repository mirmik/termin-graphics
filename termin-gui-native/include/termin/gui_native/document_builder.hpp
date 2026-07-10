#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <termin/gui_native/document.hpp>

namespace termin::gui_native {

class DocumentBuilder {
public:
    explicit DocumentBuilder(Document& document) : document_(document) {}
    template<typename T, typename... Args> T& make(Args&&... args) {
        static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *widget;
        if (tc_widget_handle_is_invalid(document_.adopt(widget.get()))) throw std::runtime_error("failed to adopt native UI widget");
        widget.release();
        return ref;
    }
    template<typename T, typename... Args> T& make_root(Args&&... args) {
        T& widget = make<T>(std::forward<Args>(args)...);
        if (!document_.add_root(widget)) throw std::runtime_error("failed to add native UI root widget");
        return widget;
    }
private:
    Document& document_;
};

} // namespace termin::gui_native
