#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

enum class Orientation {
    Horizontal,
    Vertical,
};

struct EdgeInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    tc_ui_color c_color() const {
        return tc_ui_color {r, g, b, a};
    }
};

class NativeWidget : public Widget {
public:
    explicit NativeWidget(const char* debug_name = nullptr);
    ~NativeWidget() override = default;

    tc_ui_rect bounds() const { return bounds_; }
    void set_min_size(tc_ui_size size) { min_size_ = size; }
    tc_ui_size min_size() const { return min_size_; }

    virtual tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints);
    virtual void layout(tc_ui_document* document, tc_ui_rect rect);
    virtual void paint(tc_ui_document* document, tc_ui_paint_context* context);
    virtual tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event);
    virtual void visit_recursive_destroy_targets(
        tc_ui_document* document,
        void* user_data,
        tc_widget_visit_fn visit
    );
    virtual void on_destroy(tc_ui_document* document);

protected:
    tc_ui_rect bounds_ {0.0f, 0.0f, 0.0f, 0.0f};
    tc_ui_size min_size_ {0.0f, 0.0f};

private:
    static tc_ui_size dispatch_measure(
        tc_widget* widget,
        tc_ui_document* document,
        tc_ui_constraints constraints
    );
    static void dispatch_layout(tc_widget* widget, tc_ui_document* document, tc_ui_rect rect);
    static void dispatch_paint(tc_widget* widget, tc_ui_document* document, tc_ui_paint_context* context);
    static tc_ui_event_result dispatch_pointer_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_pointer_event* event
    );
    static void dispatch_visit_recursive_destroy_targets(
        tc_widget* widget,
        tc_ui_document* document,
        void* user_data,
        tc_widget_visit_fn visit
    );
    static void dispatch_on_destroy(tc_widget* widget, tc_ui_document* document);

    static const tc_widget_vtable VTABLE;
};

class DocumentBuilder {
public:
    explicit DocumentBuilder(Document& document) : document_(document) {}

    template<typename T, typename... Args>
    T& make(Args&&... args) {
        static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *widget;
        tc_widget_handle handle = document_.adopt(widget.get());
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to adopt native UI widget");
        }
        widget.release();
        return ref;
    }

    template<typename T, typename... Args>
    T& make_root(Args&&... args) {
        T& widget = make<T>(std::forward<Args>(args)...);
        if (!document_.add_root(widget)) {
            throw std::runtime_error("failed to add native UI root widget");
        }
        return widget;
    }

private:
    Document& document_;
};

class BoxLayout : public NativeWidget {
public:
    explicit BoxLayout(Orientation orientation, const char* debug_name = nullptr);

    BoxLayout& set_padding(EdgeInsets padding);
    BoxLayout& set_spacing(float spacing);
    BoxLayout& set_background(Color color);
    BoxLayout& set_border(Color color, float thickness = 1.0f);

    void add_child(tc_widget_handle handle);
    void add_child(const Widget& widget) { add_child(widget.handle()); }

    const std::vector<tc_widget_handle>& children() const { return children_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    void visit_recursive_destroy_targets(
        tc_ui_document* document,
        void* user_data,
        tc_widget_visit_fn visit
    ) override;

private:
    Orientation orientation_;
    EdgeInsets padding_ {};
    float spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<tc_widget_handle> children_;
};

class Panel : public NativeWidget {
public:
    explicit Panel(const char* debug_name = nullptr);

    Panel& set_fill(Color color);
    Panel& set_border(Color color, float thickness = 1.0f);

    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    Color fill_ {0.16f, 0.17f, 0.19f, 1.0f};
    Color border_ {0.32f, 0.34f, 0.38f, 1.0f};
    float border_thickness_ = 1.0f;
};

class Button : public NativeWidget {
public:
    explicit Button(
        std::string text = {},
        Color fill = Color {0.20f, 0.38f, 0.64f, 1.0f}
    );
    explicit Button(Color fill);

    Button& set_accent(Color color);
    Button& set_text(std::string text);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    std::string text_;
    Color fill_;
    Color accent_ {0.80f, 0.88f, 1.0f, 1.0f};
};

class Label : public NativeWidget {
public:
    explicit Label(
        std::string text,
        float font_size = 15.0f,
        Color color = Color {0.90f, 0.92f, 0.96f, 1.0f}
    );

    Label& set_text(std::string text);
    Label& set_color(Color color);
    Label& set_font_size(float font_size);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    void update_min_size();

    std::string text_;
    float font_size_ = 15.0f;
    Color color_;
};

class Checkbox : public NativeWidget {
public:
    explicit Checkbox(bool checked = false);

    bool checked() const { return checked_; }
    void set_checked(bool checked) { checked_ = checked; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    bool checked_ = false;
};

class ProgressBar : public NativeWidget {
public:
    explicit ProgressBar(float value = 0.0f);

    void set_value(float value);
    float value() const { return value_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    float value_ = 0.0f;
};

class Slider : public NativeWidget {
public:
    explicit Slider(float value = 0.0f);

    void set_value(float value);
    float value() const { return value_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    float value_ = 0.0f;
};

class Swatch : public NativeWidget {
public:
    explicit Swatch(Color color);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    Color color_;
};

class Spacer : public NativeWidget {
public:
    explicit Spacer(tc_ui_size size);
};

} // namespace termin::gui_native
