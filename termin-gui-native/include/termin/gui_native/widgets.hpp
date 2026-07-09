#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

template<typename... Args>
class Signal {
public:
    using Callback = std::function<void(Args...)>;

    size_t connect(Callback callback) {
        if (!callback) {
            return 0;
        }
        const size_t id = next_id_++;
        slots_.push_back(Slot {id, std::move(callback)});
        return id;
    }

    bool disconnect(size_t id) {
        if (id == 0) {
            return false;
        }
        const auto before = slots_.size();
        slots_.erase(
            std::remove_if(
                slots_.begin(),
                slots_.end(),
                [id](const Slot& slot) { return slot.id == id; }
            ),
            slots_.end()
        );
        return slots_.size() != before;
    }

    void emit(Args... args) const {
        for (const Slot& slot : slots_) {
            slot.callback(args...);
        }
    }

    size_t size() const { return slots_.size(); }

private:
    struct Slot {
        size_t id = 0;
        Callback callback;
    };

    size_t next_id_ = 1;
    std::vector<Slot> slots_;
};

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

enum class LayoutPolicy {
    Fixed,
    Preferred,
    Flex,
    Stretch,
};

struct LayoutItem {
    tc_widget_handle handle {};
    LayoutPolicy policy = LayoutPolicy::Stretch;
    float fixed_extent = 0.0f;
    float flex = 1.0f;
    float grow = 1.0f;
    float shrink = 1.0f;
    float min_extent = 0.0f;
    float max_extent = 0.0f;
};

struct GridTrack {
    LayoutPolicy policy = LayoutPolicy::Stretch;
    float value = 0.0f;
    float grow = 1.0f;
    float shrink = 1.0f;
    float min_extent = 0.0f;
    float max_extent = 0.0f;
};

struct GridItem {
    tc_widget_handle handle {};
    size_t row = 0;
    size_t column = 0;
    size_t row_span = 1;
    size_t column_span = 1;
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

    tc_ui_rect bounds() const { return tc_widget_bounds(c_widget()); }
    void set_min_size(tc_ui_size size) {
        tc_widget_set_min_size(c_widget(), size);
    }
    tc_ui_size min_size() const { return tc_widget_min_size(c_widget()); }
    void set_preferred_size(tc_ui_size size) {
        tc_widget_set_preferred_size(c_widget(), size);
    }
    tc_ui_size preferred_size() const { return tc_widget_preferred_size(c_widget()); }
    void set_max_size(tc_ui_size size) {
        tc_widget_set_max_size(c_widget(), size);
    }
    tc_ui_size max_size() const { return tc_widget_max_size(c_widget()); }

    virtual tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints);
    virtual void layout(tc_ui_document* document, tc_ui_rect rect);
    virtual void paint(tc_ui_document* document, tc_ui_paint_context* context);
    virtual tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event);
    virtual tc_widget_handle hit_test(tc_ui_document* document, float x, float y);
    virtual tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event);
    virtual tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event);
    virtual void on_destroy(tc_ui_document* document);

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
    static tc_widget_handle dispatch_hit_test(
        tc_widget* widget,
        tc_ui_document* document,
        float x,
        float y
    );
    static tc_ui_event_result dispatch_key_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_key_event* event
    );
    static tc_ui_event_result dispatch_text_event(
        tc_widget* widget,
        tc_ui_document* document,
        const tc_ui_text_event* event
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
    void add_child(tc_widget_handle handle, LayoutPolicy policy, float value = 0.0f);
    void add_child(const Widget& widget) { add_child(widget.handle()); }
    void add_child(const Widget& widget, LayoutPolicy policy, float value = 0.0f) {
        add_child(widget.handle(), policy, value);
    }
    void add_fixed_child(tc_widget_handle handle, float extent);
    void add_fixed_child(const Widget& widget, float extent) { add_fixed_child(widget.handle(), extent); }
    void add_preferred_child(tc_widget_handle handle);
    void add_preferred_child(const Widget& widget) { add_preferred_child(widget.handle()); }
    void add_flex_child(tc_widget_handle handle, float flex = 1.0f);
    void add_flex_child(const Widget& widget, float flex = 1.0f) { add_flex_child(widget.handle(), flex); }
    void add_stretch_child(tc_widget_handle handle);
    void add_stretch_child(const Widget& widget) { add_stretch_child(widget.handle()); }
    bool set_child_extent_limits(tc_widget_handle handle, float min_extent, float max_extent);
    bool set_child_extent_limits(const Widget& widget, float min_extent, float max_extent) {
        return set_child_extent_limits(widget.handle(), min_extent, max_extent);
    }

    const std::vector<LayoutItem>& items() const { return items_; }
    std::vector<tc_widget_handle> children() const;

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    Orientation orientation_;
    EdgeInsets padding_ {};
    float spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<LayoutItem> items_;
};

class HStack : public BoxLayout {
public:
    explicit HStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Horizontal, debug_name) {}
};

class VStack : public BoxLayout {
public:
    explicit VStack(const char* debug_name = nullptr) : BoxLayout(Orientation::Vertical, debug_name) {}
};

class GridLayout : public NativeWidget {
public:
    explicit GridLayout(const char* debug_name = nullptr);

    GridLayout& set_padding(EdgeInsets padding);
    GridLayout& set_spacing(float column_spacing, float row_spacing);
    GridLayout& set_background(Color color);
    GridLayout& set_border(Color color, float thickness = 1.0f);

    size_t add_column(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    size_t add_row(LayoutPolicy policy = LayoutPolicy::Stretch, float value = 0.0f);
    void add_child(tc_widget_handle handle, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1);
    void add_child(const Widget& widget, size_t row, size_t column, size_t row_span = 1, size_t column_span = 1) {
        add_child(widget.handle(), row, column, row_span, column_span);
    }
    bool set_column_extent_limits(size_t column, float min_extent, float max_extent);
    bool set_row_extent_limits(size_t row, float min_extent, float max_extent);

    const std::vector<GridTrack>& columns() const { return columns_; }
    const std::vector<GridTrack>& rows() const { return rows_; }
    const std::vector<GridItem>& items() const { return items_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    EdgeInsets padding_ {};
    float column_spacing_ = 0.0f;
    float row_spacing_ = 0.0f;
    Color background_ {0.0f, 0.0f, 0.0f, 0.0f};
    Color border_ {0.0f, 0.0f, 0.0f, 0.0f};
    float border_thickness_ = 0.0f;
    std::vector<GridTrack> columns_;
    std::vector<GridTrack> rows_;
    std::vector<GridItem> items_;
};

class GroupBox : public NativeWidget {
public:
    explicit GroupBox(std::string title = {}, const char* debug_name = nullptr);

    GroupBox& set_title(std::string title);
    GroupBox& set_padding(EdgeInsets padding);
    GroupBox& set_background(Color color);
    GroupBox& set_border(Color color, float thickness = 1.0f);
    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }

    const std::string& title() const { return title_; }
    tc_widget_handle content() const { return child_handle_at(0); }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    tc_ui_rect content_rect() const;

    std::string title_;
    EdgeInsets padding_ {10.0f, 8.0f, 10.0f, 10.0f};
    float header_height_ = 30.0f;
    Color background_ {0.11f, 0.12f, 0.14f, 1.0f};
    Color border_ {0.32f, 0.34f, 0.38f, 1.0f};
    float border_thickness_ = 1.0f;
};

class Splitter : public NativeWidget {
public:
    explicit Splitter(Orientation orientation = Orientation::Horizontal, const char* debug_name = nullptr);

    void set_first(tc_widget_handle handle);
    void set_first(const Widget& widget) { set_first(widget.handle()); }
    void set_second(tc_widget_handle handle);
    void set_second(const Widget& widget) { set_second(widget.handle()); }
    Splitter& set_split_fraction(float fraction);
    Splitter& set_min_extents(float first_min, float second_min);
    Splitter& set_divider_thickness(float thickness);

    tc_widget_handle first() const { return child_handle_at(0); }
    tc_widget_handle second() const { return child_handle_at(1); }
    float split_fraction() const { return split_fraction_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    tc_ui_rect divider_rect() const;
    void layout_children(tc_ui_document* document);
    float split_axis_extent() const;

    Orientation orientation_ = Orientation::Horizontal;
    float split_fraction_ = 0.5f;
    float first_min_extent_ = 32.0f;
    float second_min_extent_ = 32.0f;
    float divider_thickness_ = 6.0f;
    Color divider_color_ {0.30f, 0.33f, 0.38f, 1.0f};
};

class ScrollArea : public NativeWidget {
public:
    explicit ScrollArea(const char* debug_name = nullptr);

    void set_content(tc_widget_handle handle);
    void set_content(const Widget& widget) { set_content(widget.handle()); }
    tc_widget_handle content() const { return child_handle_at(0); }

    void set_scroll(float x, float y);
    float scroll_x() const { return scroll_x_; }
    float scroll_y() const { return scroll_y_; }
    tc_ui_size content_size() const { return content_size_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    void clamp_scroll();

    tc_ui_size content_size_ {0.0f, 0.0f};
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float wheel_step_ = 48.0f;
};

struct TabPage {
    std::string title;
    tc_widget_handle handle = tc_widget_handle_invalid();
};

class TabView : public NativeWidget {
public:
    explicit TabView(const char* debug_name = nullptr);

    void add_page(std::string title, tc_widget_handle handle);
    void add_page(std::string title, const Widget& widget) { add_page(std::move(title), widget.handle()); }
    size_t page_count() const { return child_count(); }
    size_t selected_index() const { return selected_index_; }
    void set_selected_index(size_t index);

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_widget_handle hit_test(tc_ui_document* document, float x, float y) override;

private:
    tc_ui_rect page_rect() const;

    std::vector<TabPage> pages_;
    size_t selected_index_ = 0;
    float header_height_ = 32.0f;
    float min_tab_width_ = 92.0f;
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
    Signal<Button&>& clicked() { return clicked_; }
    const Signal<Button&>& clicked() const { return clicked_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    std::string text_;
    Color fill_;
    Color accent_ {0.80f, 0.88f, 1.0f, 1.0f};
    bool pressed_ = false;
    Signal<Button&> clicked_;
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
    void set_checked(bool checked);
    Signal<Checkbox&, bool>& changed() { return changed_; }
    const Signal<Checkbox&, bool>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    bool checked_ = false;
    bool pressed_ = false;
    Signal<Checkbox&, bool> changed_;
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

class Separator : public NativeWidget {
public:
    explicit Separator(Orientation orientation = Orientation::Horizontal);

    Separator& set_color(Color color);
    Separator& set_thickness(float thickness);
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

private:
    Orientation orientation_ = Orientation::Horizontal;
    Color color_ {0.36f, 0.38f, 0.42f, 1.0f};
    float thickness_ = 1.0f;
};

class TextInput : public NativeWidget {
public:
    explicit TextInput(std::string text = {});

    const std::string& text() const { return text_; }
    size_t caret() const { return caret_; }
    void set_text(std::string text);
    void set_caret(size_t caret);
    Signal<TextInput&, const std::string&>& changed() { return changed_; }
    const Signal<TextInput&, const std::string&>& changed() const { return changed_; }
    Signal<TextInput&, const std::string&>& submitted() { return submitted_; }
    const Signal<TextInput&, const std::string&>& submitted() const { return submitted_; }

    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;

private:
    void update_preferred_size();
    void emit_changed();

    std::string text_;
    size_t caret_ = 0;
    float font_size_ = 14.0f;
    Color text_color_ {0.94f, 0.96f, 0.98f, 1.0f};
    Signal<TextInput&, const std::string&> changed_;
    Signal<TextInput&, const std::string&> submitted_;
};

class Slider : public NativeWidget {
public:
    explicit Slider(float value = 0.0f);

    void set_value(float value);
    float value() const { return value_; }
    Signal<Slider&, float>& changed() { return changed_; }
    const Signal<Slider&, float>& changed() const { return changed_; }
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;

private:
    float value_ = 0.0f;
    bool dragging_ = false;
    Signal<Slider&, float> changed_;
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
