#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include <tcbase/tc_log.h>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/tc_ui_document.h>
#include <termin/gui_native/widgets.hpp>
#include <tgfx2/render_context.hpp>

namespace nb = nanobind;

namespace {

constexpr uint32_t PYTHON_WIDGET_MAGIC = 0x54475549u; // "TGUI"

class DrawList {
public:
    DrawList() : draw_list_(tc_ui_draw_list_create()) {
        if (!draw_list_) {
            throw std::runtime_error("failed to create tc_ui_draw_list");
        }
    }

    ~DrawList() {
        tc_ui_draw_list_destroy(draw_list_);
    }

    DrawList(const DrawList&) = delete;
    DrawList& operator=(const DrawList&) = delete;

    tc_ui_draw_list* get() const {
        return draw_list_;
    }

private:
    tc_ui_draw_list* draw_list_ = nullptr;
};

struct DrawCommand {
    tc_ui_draw_command value {};
    std::string text;
    std::vector<tc_ui_point> points;

    explicit DrawCommand(const tc_ui_draw_command& source)
        : value(source),
          text(source.text ? source.text : ""),
          points(source.points && source.point_count > 0
              ? std::vector<tc_ui_point>(source.points, source.points + source.point_count)
              : std::vector<tc_ui_point> {}) {
        refresh_pointers();
    }

    DrawCommand(const DrawCommand& other)
        : value(other.value), text(other.text), points(other.points) {
        refresh_pointers();
    }

    DrawCommand(DrawCommand&& other) noexcept
        : value(other.value), text(std::move(other.text)), points(std::move(other.points)) {
        refresh_pointers();
    }

    DrawCommand& operator=(const DrawCommand& other) {
        if (this != &other) {
            value = other.value;
            text = other.text;
            points = other.points;
            refresh_pointers();
        }
        return *this;
    }

    DrawCommand& operator=(DrawCommand&& other) noexcept {
        if (this != &other) {
            value = other.value;
            text = std::move(other.text);
            points = std::move(other.points);
            refresh_pointers();
        }
        return *this;
    }

private:
    void refresh_pointers() {
        value.text = text.empty() ? nullptr : text.c_str();
        value.points = points.empty() ? nullptr : points.data();
        value.point_count = points.size();
    }
};

class PaintContext {
public:
    explicit PaintContext(DrawList& draw_list)
        : context_(tc_ui_paint_context_create(draw_list.get())),
          owns_(true) {
        if (!context_) {
            throw std::runtime_error("failed to create tc_ui_paint_context");
        }
    }

    PaintContext(tc_ui_paint_context* context, bool owns)
        : context_(context), owns_(owns) {
        if (!context_) {
            throw std::runtime_error("cannot wrap null tc_ui_paint_context");
        }
    }

    ~PaintContext() {
        if (owns_) {
            tc_ui_paint_context_destroy(context_);
        }
    }

    PaintContext(PaintContext&& other) noexcept
        : context_(other.context_), owns_(other.owns_) {
        other.context_ = nullptr;
        other.owns_ = false;
    }

    PaintContext& operator=(PaintContext&& other) noexcept {
        if (this != &other) {
            if (owns_) {
                tc_ui_paint_context_destroy(context_);
            }
            context_ = other.context_;
            owns_ = other.owns_;
            other.context_ = nullptr;
            other.owns_ = false;
        }
        return *this;
    }

    PaintContext(const PaintContext&) = delete;
    PaintContext& operator=(const PaintContext&) = delete;

    tc_ui_paint_context* get() const {
        return context_;
    }

private:
    tc_ui_paint_context* context_ = nullptr;
    bool owns_ = false;
};

class Document;

struct WidgetHandle {
    tc_widget_handle handle = tc_widget_handle_invalid();
};

enum class StyleField : uint64_t {
    Background = TC_UI_STYLE_BACKGROUND,
    Foreground = TC_UI_STYLE_FOREGROUND,
    Border = TC_UI_STYLE_BORDER,
    Accent = TC_UI_STYLE_ACCENT,
    PaddingLeft = TC_UI_STYLE_PADDING_LEFT,
    PaddingTop = TC_UI_STYLE_PADDING_TOP,
    PaddingRight = TC_UI_STYLE_PADDING_RIGHT,
    PaddingBottom = TC_UI_STYLE_PADDING_BOTTOM,
    Spacing = TC_UI_STYLE_SPACING,
    BorderWidth = TC_UI_STYLE_BORDER_WIDTH,
    FontSize = TC_UI_STYLE_FONT_SIZE,
    MinWidth = TC_UI_STYLE_MIN_WIDTH,
    MinHeight = TC_UI_STYLE_MIN_HEIGHT,
    FontRole = TC_UI_STYLE_FONT_ROLE,
    All = TC_UI_STYLE_ALL_FIELDS,
};

struct Theme {
    tc_ui_theme value {};

    Theme() {
        tc_ui_theme_init_default(&value);
    }

    explicit Theme(const tc_ui_theme& source) : value(source) {}

    tc_ui_role_style& role(tc_ui_style_role role) {
        if (role < TC_UI_STYLE_GENERIC || role >= TC_UI_STYLE_ROLE_COUNT) {
            throw std::out_of_range("invalid native UI style role");
        }
        return value.roles[role];
    }
};

struct DocumentState {
    tc_ui_document* document = nullptr;
    std::exception_ptr pending_exception;
};

struct WidgetRef {
    std::shared_ptr<DocumentState> state;
    tc_widget_handle handle = tc_widget_handle_invalid();

    bool alive() const {
        return state && state->document && tc_ui_document_is_alive(state->document, handle);
    }

    tc_widget* resolve() const {
        return state && state->document
            ? tc_ui_document_resolve_widget(state->document, handle)
            : nullptr;
    }

    tc_widget* resolve_checked() const {
        tc_widget* widget = resolve();
        if (!widget) {
            throw std::runtime_error("widget reference is stale");
        }
        return widget;
    }

    void throw_pending_exception() const {
        if (state && state->pending_exception) {
            std::exception_ptr exception = state->pending_exception;
            state->pending_exception = nullptr;
            std::rethrow_exception(exception);
        }
    }
};

template<typename T>
T& native_widget_checked(const WidgetRef& ref, const char* expected_type) {
    tc_widget* widget = ref.resolve_checked();
    auto* base = static_cast<termin::gui_native::Widget*>(widget->body);
    T* typed = dynamic_cast<T*>(base);
    if (!typed) {
        throw std::runtime_error(std::string("widget is not a ") + expected_type);
    }
    return *typed;
}

struct TextInputRef {
    WidgetRef widget;

    termin::gui_native::TextInput& get() const {
        return native_widget_checked<termin::gui_native::TextInput>(widget, "TextInput");
    }
};

struct TextAreaRef {
    WidgetRef widget;

    termin::gui_native::TextArea& get() const {
        return native_widget_checked<termin::gui_native::TextArea>(widget, "TextArea");
    }
};

#define TERMIN_GUI_NATIVE_WIDGET_REF(Name, Type) \
    struct Name { \
        WidgetRef widget; \
        termin::gui_native::Type& get() const { \
            return native_widget_checked<termin::gui_native::Type>(widget, #Type); \
        } \
    }

TERMIN_GUI_NATIVE_WIDGET_REF(SpinBoxRef, SpinBox);
TERMIN_GUI_NATIVE_WIDGET_REF(SliderEditRef, SliderEdit);
TERMIN_GUI_NATIVE_WIDGET_REF(ComboBoxRef, ComboBox);
TERMIN_GUI_NATIVE_WIDGET_REF(IconButtonRef, IconButton);
TERMIN_GUI_NATIVE_WIDGET_REF(ImageWidgetRef, ImageWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(CanvasRef, Canvas);

#undef TERMIN_GUI_NATIVE_WIDGET_REF

struct PythonWidget {
    uint32_t magic = PYTHON_WIDGET_MAGIC;
    tc_widget widget {};
    nb::object object;
    std::string debug_name;
    std::shared_ptr<DocumentState> state;
    bool callbacks_enabled = false;

    explicit PythonWidget(
        nb::object object_,
        std::string debug_name_,
        std::shared_ptr<DocumentState> state_
    )
        : object(std::move(object_)),
          debug_name(std::move(debug_name_)),
          state(std::move(state_)) {
        tc_widget_init(&widget, &VTABLE, &PythonWidget::delete_widget, TC_LANGUAGE_PYTHON, this);
        widget.debug_name = debug_name.empty() ? nullptr : debug_name.c_str();
    }

    static PythonWidget* from_widget(tc_widget* widget) {
        if (!widget || !widget->body) {
            return nullptr;
        }
        auto* self = static_cast<PythonWidget*>(widget->body);
        if (self->magic != PYTHON_WIDGET_MAGIC) {
            return nullptr;
        }
        return self;
    }

    static void delete_widget(tc_widget* widget) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot delete invalid Python widget shim");
            return;
        }
        self->magic = 0;
        nb::gil_scoped_acquire gil;
        delete self;
    }

    void capture_exception(const char* operation) {
        nb::gil_scoped_acquire gil;
        if (state && !state->pending_exception) {
            state->pending_exception = std::current_exception();
        }
        tc_log_error(
            "[termin-gui-native/python] Python widget %s failed for '%s'",
            operation,
            debug_name.empty() ? "<unnamed>" : debug_name.c_str()
        );
    }

    static tc_ui_size measure(
        tc_widget* widget,
        tc_ui_document*,
        tc_ui_constraints constraints
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot measure invalid Python widget shim");
            return constraints.min_size;
        }
        try {
            nb::gil_scoped_acquire gil;
            return nb::cast<tc_ui_size>(self->object.attr("measure")(constraints));
        } catch (...) {
            self->capture_exception("measure");
            return constraints.min_size;
        }
    }

    static void layout(tc_widget* widget, tc_ui_document*, tc_ui_rect rect) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot layout invalid Python widget shim");
            return;
        }
        tc_widget_set_bounds(widget, rect);
        try {
            nb::gil_scoped_acquire gil;
            self->object.attr("layout")(rect);
        } catch (...) {
            self->capture_exception("layout");
        }
    }

    static void paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context* context) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot paint invalid Python widget shim");
            return;
        }

        try {
            nb::gil_scoped_acquire gil;
            PaintContext borrowed(context, false);
            self->object.attr("paint")(std::move(borrowed));
        } catch (...) {
            self->capture_exception("paint");
        }
    }

    static tc_ui_event_result pointer_event(
        tc_widget* widget,
        tc_ui_document*,
        const tc_ui_pointer_event* event
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self || !event) {
            tc_log_error("[termin-gui-native/python] cannot route pointer event to invalid Python widget shim");
            return TC_UI_EVENT_IGNORED;
        }
        try {
            nb::gil_scoped_acquire gil;
            return nb::cast<tc_ui_event_result>(self->object.attr("pointer_event")(*event));
        } catch (...) {
            self->capture_exception("pointer_event");
            return TC_UI_EVENT_IGNORED;
        }
    }

    static tc_widget_handle hit_test(
        tc_widget* widget,
        tc_ui_document* document,
        float x,
        float y
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot hit-test invalid Python widget shim");
            return tc_widget_handle_invalid();
        }
        try {
            nb::gil_scoped_acquire gil;
            nb::object result = self->object.attr("hit_test")(x, y);
            if (result.is_none()) {
                return tc_widget_handle_invalid();
            }
            tc_widget_handle handle = nb::cast<WidgetHandle>(result).handle;
            return tc_ui_document_is_alive(document, handle)
                ? handle
                : tc_widget_handle_invalid();
        } catch (...) {
            self->capture_exception("hit_test");
            return tc_widget_handle_invalid();
        }
    }

    static tc_ui_event_result key_event(
        tc_widget* widget,
        tc_ui_document*,
        const tc_ui_key_event* event
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self || !event) {
            tc_log_error("[termin-gui-native/python] cannot route key event to invalid Python widget shim");
            return TC_UI_EVENT_IGNORED;
        }
        try {
            nb::gil_scoped_acquire gil;
            return nb::cast<tc_ui_event_result>(self->object.attr("key_event")(*event));
        } catch (...) {
            self->capture_exception("key_event");
            return TC_UI_EVENT_IGNORED;
        }
    }

    static tc_ui_event_result text_event(
        tc_widget* widget,
        tc_ui_document*,
        const tc_ui_text_event* event
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self || !event) {
            tc_log_error("[termin-gui-native/python] cannot route text event to invalid Python widget shim");
            return TC_UI_EVENT_IGNORED;
        }
        try {
            nb::gil_scoped_acquire gil;
            return nb::cast<tc_ui_event_result>(
                self->object.attr("text_event")(event->text ? event->text : "")
            );
        } catch (...) {
            self->capture_exception("text_event");
            return TC_UI_EVENT_IGNORED;
        }
    }

    static void focus_event(tc_widget* widget, tc_ui_document*, bool focused) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot route focus event to invalid Python widget shim");
            return;
        }
        try {
            nb::gil_scoped_acquire gil;
            self->object.attr("focus_event")(focused);
        } catch (...) {
            self->capture_exception("focus_event");
        }
    }

    static void overlay_dismissed(
        tc_widget* widget,
        tc_ui_document*,
        tc_ui_overlay_dismiss_reason reason
    ) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot notify invalid dismissed overlay shim");
            return;
        }
        try {
            nb::gil_scoped_acquire gil;
            self->object.attr("overlay_dismissed")(reason);
        } catch (...) {
            self->capture_exception("overlay_dismissed");
        }
    }

    static void on_destroy(tc_widget* widget, tc_ui_document*) {
        PythonWidget* self = from_widget(widget);
        if (!self) {
            tc_log_error("[termin-gui-native/python] cannot destroy invalid Python widget shim");
            return;
        }
        if (!self->callbacks_enabled) {
            return;
        }
        try {
            nb::gil_scoped_acquire gil;
            self->object.attr("on_destroy")();
        } catch (...) {
            self->capture_exception("on_destroy");
        }
    }

    static const tc_widget_vtable VTABLE;
};

const tc_widget_vtable PythonWidget::VTABLE {
    "PythonWidget",
    &PythonWidget::measure,
    &PythonWidget::layout,
    &PythonWidget::paint,
    &PythonWidget::pointer_event,
    &PythonWidget::hit_test,
    &PythonWidget::key_event,
    &PythonWidget::text_event,
    &PythonWidget::focus_event,
    &PythonWidget::overlay_dismissed,
    &PythonWidget::on_destroy,
};

class Document {
public:
    Document()
        : state_(std::make_shared<DocumentState>()),
          clipboard_getter_(nb::none()),
          clipboard_setter_(nb::none()) {
        state_->document = tc_ui_document_create();
        if (!state_->document) {
            throw std::runtime_error("failed to create tc_ui_document");
        }
    }

    ~Document() {
        if (state_ && state_->document) {
            tc_ui_document_destroy(state_->document);
            state_->document = nullptr;
            state_->pending_exception = nullptr;
        }
    }

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    tc_ui_document* get() const {
        return state_->document;
    }

    WidgetHandle adopt(nb::object object, const std::string& debug_name) {
        auto widget = std::make_unique<PythonWidget>(object, debug_name, state_);
        tc_widget_handle handle = tc_ui_document_adopt_widget(get(), &widget->widget);
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to adopt Python widget");
        }
        PythonWidget* adopted_widget = widget.release();
        try {
            object.attr("_bind_native")(WidgetRef {state_, handle});
            adopted_widget->callbacks_enabled = true;
        } catch (...) {
            tc_ui_document_destroy_widget(get(), handle);
            throw;
        }
        return WidgetHandle {handle};
    }

    WidgetRef ref(WidgetHandle handle) const {
        return WidgetRef {state_, handle.handle};
    }

    template<typename T, typename... Args>
    WidgetRef make_native(Args&&... args) {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        tc_widget_handle handle = tc_ui_document_adopt_widget(get(), widget->c_widget());
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to adopt native widget");
        }
        widget.release();
        return WidgetRef {state_, handle};
    }

    void throw_pending_exception() {
        if (state_->pending_exception) {
            std::exception_ptr exception = state_->pending_exception;
            state_->pending_exception = nullptr;
            std::rethrow_exception(exception);
        }
    }

    void set_clipboard_handlers(nb::object getter, nb::object setter) {
        clipboard_getter_ = std::move(getter);
        clipboard_setter_ = std::move(setter);
        tc_ui_document_set_clipboard(
            get(),
            clipboard_getter_.is_none() ? nullptr : &Document::clipboard_get,
            clipboard_setter_.is_none() ? nullptr : &Document::clipboard_set,
            this
        );
    }

private:
    static const char* clipboard_get(void* user_data) {
        auto* self = static_cast<Document*>(user_data);
        try {
            nb::gil_scoped_acquire gil;
            self->clipboard_buffer_ = nb::cast<std::string>(self->clipboard_getter_());
            return self->clipboard_buffer_.c_str();
        } catch (...) {
            if (!self->state_->pending_exception) {
                self->state_->pending_exception = std::current_exception();
            }
            tc_log_error("[termin-gui-native/python] clipboard getter failed");
            return nullptr;
        }
    }

    static bool clipboard_set(void* user_data, const char* text, size_t byte_length) {
        auto* self = static_cast<Document*>(user_data);
        try {
            nb::gil_scoped_acquire gil;
            self->clipboard_setter_(std::string(text ? text : "", byte_length));
            return true;
        } catch (...) {
            if (!self->state_->pending_exception) {
                self->state_->pending_exception = std::current_exception();
            }
            tc_log_error("[termin-gui-native/python] clipboard setter failed");
            return false;
        }
    }

    std::shared_ptr<DocumentState> state_;
    nb::object clipboard_getter_;
    nb::object clipboard_setter_;
    std::string clipboard_buffer_;
};

DrawCommand command_at_checked(const DrawList& draw_list, size_t index) {
    const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list.get(), index);
    if (!command) {
        throw std::out_of_range("draw command index out of range");
    }
    return DrawCommand {*command};
}

} // namespace

NB_MODULE(_gui_native, m) {
    try {
        nb::module_::import_("tgfx._tgfx_native");
    } catch (const std::exception& e) {
        tc_log_error("[termin-gui-native/python] failed to import tgfx._tgfx_native: %s", e.what());
        throw;
    }

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
        .def(nb::init<float, float, float, float>(),
             nb::arg("r") = 0.0f, nb::arg("g") = 0.0f,
             nb::arg("b") = 0.0f, nb::arg("a") = 1.0f)
        .def_rw("r", &tc_ui_color::r)
        .def_rw("g", &tc_ui_color::g)
        .def_rw("b", &tc_ui_color::b)
        .def_rw("a", &tc_ui_color::a);

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
        .def(nb::init<tc_ui_size, tc_ui_size>(),
             nb::arg("min_size") = tc_ui_size {0.0f, 0.0f},
             nb::arg("max_size") = tc_ui_size {0.0f, 0.0f})
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

    nb::enum_<tc_ui_modifier_flag>(m, "ModifierFlag", nb::is_arithmetic())
        .value("Shift", TC_UI_MOD_SHIFT)
        .value("Ctrl", TC_UI_MOD_CTRL)
        .value("Alt", TC_UI_MOD_ALT)
        .value("Super", TC_UI_MOD_SUPER);

    nb::enum_<tc_ui_overlay_flag>(m, "OverlayFlag", nb::is_arithmetic())
        .value("Modal", TC_UI_OVERLAY_MODAL)
        .value("DismissOnOutside", TC_UI_OVERLAY_DISMISS_ON_OUTSIDE)
        .value("PointerTransparent", TC_UI_OVERLAY_POINTER_TRANSPARENT)
        .value("Tooltip", TC_UI_OVERLAY_TOOLTIP);

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
        .value("Escape", TC_UI_KEY_ESCAPE)
        .value("A", TC_UI_KEY_A)
        .value("C", TC_UI_KEY_C)
        .value("V", TC_UI_KEY_V)
        .value("X", TC_UI_KEY_X)
        .value("Delete", TC_UI_KEY_DELETE)
        .value("Left", TC_UI_KEY_LEFT)
        .value("Right", TC_UI_KEY_RIGHT)
        .value("Home", TC_UI_KEY_HOME)
        .value("End", TC_UI_KEY_END)
        .value("Up", TC_UI_KEY_UP_ARROW)
        .value("Down", TC_UI_KEY_DOWN_ARROW);

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

    m.def("tooltip_rect", &tc_ui_tooltip_rect,
          nb::arg("viewport"), nb::arg("anchor"), nb::arg("preferred_size"),
          nb::arg("offset") = tc_ui_point {12.0f, 18.0f}, nb::arg("margin") = 4.0f);

    nb::enum_<tc_widget_flag>(m, "WidgetFlag", nb::is_arithmetic())
        .value("Focusable", TC_WIDGET_FOCUSABLE)
        .value("DirtyLayout", TC_WIDGET_DIRTY_LAYOUT)
        .value("DirtyPaint", TC_WIDGET_DIRTY_PAINT)
        .value("DirtyState", TC_WIDGET_DIRTY_STATE)
        .value("Visible", TC_WIDGET_VISIBLE)
        .value("Enabled", TC_WIDGET_ENABLED)
        .value("MouseTransparent", TC_WIDGET_MOUSE_TRANSPARENT);

    nb::class_<WidgetRef>(m, "WidgetRef")
        .def_prop_ro("handle", [](const WidgetRef& self) { return WidgetHandle {self.handle}; })
        .def_prop_ro("alive", &WidgetRef::alive)
        .def("__bool__", &WidgetRef::alive)
        .def_prop_rw("bounds",
            [](const WidgetRef& self) { return tc_widget_bounds(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_rect value) {
                tc_widget_set_bounds(self.resolve_checked(), value);
            })
        .def_prop_rw("min_size",
            [](const WidgetRef& self) { return tc_widget_min_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_min_size(self.resolve_checked(), value);
            })
        .def_prop_rw("preferred_size",
            [](const WidgetRef& self) { return tc_widget_preferred_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_preferred_size(self.resolve_checked(), value);
            })
        .def_prop_rw("max_size",
            [](const WidgetRef& self) { return tc_widget_max_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_max_size(self.resolve_checked(), value);
            })
        .def_prop_rw("visible",
            [](const WidgetRef& self) { return tc_widget_is_visible(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_visible(self.resolve_checked(), value);
            })
        .def_prop_rw("enabled",
            [](const WidgetRef& self) { return tc_widget_is_enabled(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_enabled(self.resolve_checked(), value);
            })
        .def_prop_rw("mouse_transparent",
            [](const WidgetRef& self) { return tc_widget_is_mouse_transparent(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_mouse_transparent(self.resolve_checked(), value);
            })
        .def_prop_rw("focusable",
            [](const WidgetRef& self) { return tc_widget_is_focusable(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_focusable(self.resolve_checked(), value);
            })
        .def_prop_rw("style_role",
            [](const WidgetRef& self) {
                return tc_widget_style_role(self.resolve_checked());
            },
            [](const WidgetRef& self, tc_ui_style_role role) {
                tc_widget_set_style_role(self.resolve_checked(), role);
            })
        .def_prop_rw("style_override",
            [](const WidgetRef& self) {
                return tc_widget_style_override(self.resolve_checked());
            },
            [](const WidgetRef& self, const tc_ui_style_override& style_override) {
                if (!tc_widget_set_style_override(self.resolve_checked(), &style_override)) {
                    throw std::invalid_argument("invalid native UI style override");
                }
            })
        .def("clear_style_override", [](const WidgetRef& self) {
            tc_widget_clear_style_override(self.resolve_checked());
        })
        .def("style_state", [](const WidgetRef& self) {
            return tc_ui_document_widget_style_state(
                self.state->document,
                self.resolve_checked()
            );
        })
        .def("resolve_style", [](const WidgetRef& self, uint32_t extra_state) {
            tc_ui_style style {};
            if (!tc_ui_document_resolve_style(
                    self.state->document,
                    self.resolve_checked(),
                    extra_state,
                    &style)) {
                throw std::runtime_error("failed to resolve native UI widget style");
            }
            return style;
        }, nb::arg("extra_state") = 0)
        .def_prop_ro("debug_name", [](const WidgetRef& self) {
            const char* name = tc_widget_debug_name(self.resolve_checked());
            return name ? std::string(name) : std::string();
        })
        .def_prop_ro("dirty_flags", [](const WidgetRef& self) {
            return tc_widget_dirty_flags(self.resolve_checked());
        })
        .def("mark_dirty", [](const WidgetRef& self, uint32_t flags) {
            tc_widget_mark_dirty(self.resolve_checked(), flags);
        }, nb::arg("flags"))
        .def("clear_dirty", [](const WidgetRef& self, uint32_t flags) {
            tc_widget_clear_dirty(self.resolve_checked(), flags);
        }, nb::arg("flags"))
        .def_prop_ro("parent", [](const WidgetRef& self) -> nb::object {
            tc_widget* parent = tc_widget_parent(self.resolve_checked());
            return parent
                ? nb::cast(WidgetRef {self.state, parent->handle})
                : nb::none();
        })
        .def_prop_ro("children", [](const WidgetRef& self) {
            nb::list children;
            tc_widget* widget = self.resolve_checked();
            for (size_t index = 0; index < tc_widget_child_count(widget); ++index) {
                tc_widget* child = tc_widget_child_at(widget, index);
                if (child) {
                    children.append(WidgetRef {self.state, child->handle});
                }
            }
            return children;
        })
        .def("append_child", [](const WidgetRef& self, const WidgetRef& child) {
            return tc_widget_append_child(self.resolve_checked(), child.resolve_checked());
        }, nb::arg("child"))
        .def("insert_child", [](const WidgetRef& self, size_t index, const WidgetRef& child) {
            return tc_widget_insert_child(self.resolve_checked(), index, child.resolve_checked());
        }, nb::arg("index"), nb::arg("child"))
        .def("remove_child", [](const WidgetRef& self, const WidgetRef& child) {
            return tc_widget_remove_child(self.resolve_checked(), child.resolve_checked());
        }, nb::arg("child"))
        .def("detach", [](const WidgetRef& self) {
            return tc_widget_detach(self.resolve_checked());
        })
        .def("measure", [](const WidgetRef& self, tc_ui_constraints constraints) {
            tc_widget* widget = self.resolve_checked();
            tc_ui_size result = constraints.min_size;
            if (widget->vtable && widget->vtable->measure) {
                result = widget->vtable->measure(widget, self.state->document, constraints);
            }
            self.throw_pending_exception();
            return result;
        }, nb::arg("constraints"))
        .def("layout", [](const WidgetRef& self, tc_ui_rect rect) {
            tc_widget* widget = self.resolve_checked();
            if (widget->vtable && widget->vtable->layout) {
                widget->vtable->layout(widget, self.state->document, rect);
            }
            self.throw_pending_exception();
        }, nb::arg("rect"))
        .def("paint", [](const WidgetRef& self, PaintContext& context) {
            tc_widget* widget = self.resolve_checked();
            if (widget->vtable && widget->vtable->paint) {
                widget->vtable->paint(widget, self.state->document, context.get());
            }
            self.throw_pending_exception();
        }, nb::arg("context"))
        .def("hit_test", [](const WidgetRef& self, float x, float y) {
            tc_widget* widget = self.resolve_checked();
            tc_widget_handle result = tc_widget_handle_invalid();
            if (widget->vtable && widget->vtable->hit_test) {
                result = widget->vtable->hit_test(widget, self.state->document, x, y);
            }
            self.throw_pending_exception();
            return WidgetHandle {result};
        }, nb::arg("x"), nb::arg("y"))
        .def("dispatch_pointer_event", [](const WidgetRef& self, const tc_ui_pointer_event& event) {
            tc_widget* widget = self.resolve_checked();
            tc_ui_event_result result = TC_UI_EVENT_IGNORED;
            if (widget->vtable && widget->vtable->pointer_event) {
                result = widget->vtable->pointer_event(widget, self.state->document, &event);
            }
            self.throw_pending_exception();
            return result;
        }, nb::arg("event"))
        .def("dispatch_key_event", [](const WidgetRef& self, const tc_ui_key_event& event) {
            tc_widget* widget = self.resolve_checked();
            tc_ui_event_result result = TC_UI_EVENT_IGNORED;
            if (widget->vtable && widget->vtable->key_event) {
                result = widget->vtable->key_event(widget, self.state->document, &event);
            }
            self.throw_pending_exception();
            return result;
        }, nb::arg("event"))
        .def("dispatch_text_event", [](const WidgetRef& self, const std::string& text) {
            tc_widget* widget = self.resolve_checked();
            const tc_ui_text_event event {text.c_str()};
            tc_ui_event_result result = TC_UI_EVENT_IGNORED;
            if (widget->vtable && widget->vtable->text_event) {
                result = widget->vtable->text_event(widget, self.state->document, &event);
            }
            self.throw_pending_exception();
            return result;
        });

    nb::class_<TextInputRef>(m, "TextInput")
        .def_prop_ro("widget", [](const TextInputRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const TextInputRef& self) {
            return WidgetHandle {self.widget.handle};
        })
        .def_prop_rw("text",
            [](const TextInputRef& self) { return self.get().text(); },
            [](const TextInputRef& self, const std::string& value) { self.get().set_text(value); })
        .def_prop_rw("caret",
            [](const TextInputRef& self) { return self.get().caret(); },
            [](const TextInputRef& self, size_t value) { self.get().set_caret(value); })
        .def_prop_ro("has_selection", [](const TextInputRef& self) {
            return self.get().has_selection();
        })
        .def_prop_ro("selection_start", [](const TextInputRef& self) {
            return self.get().selection_start();
        })
        .def_prop_ro("selection_end", [](const TextInputRef& self) {
            return self.get().selection_end();
        })
        .def_prop_ro("selected_text", [](const TextInputRef& self) {
            return self.get().selected_text();
        })
        .def_prop_ro("scroll_x", [](const TextInputRef& self) { return self.get().scroll_x(); })
        .def("select", [](const TextInputRef& self, size_t anchor, size_t caret) {
            self.get().select(anchor, caret);
        }, nb::arg("anchor"), nb::arg("caret"))
        .def("select_all", [](const TextInputRef& self) { self.get().select_all(); })
        .def("clear_selection", [](const TextInputRef& self) { self.get().clear_selection(); });

    nb::class_<TextAreaRef>(m, "TextArea")
        .def_prop_ro("widget", [](const TextAreaRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const TextAreaRef& self) {
            return WidgetHandle {self.widget.handle};
        })
        .def_prop_rw("text",
            [](const TextAreaRef& self) { return self.get().text(); },
            [](const TextAreaRef& self, const std::string& value) { self.get().set_text(value); })
        .def_prop_rw("caret",
            [](const TextAreaRef& self) { return self.get().caret(); },
            [](const TextAreaRef& self, size_t value) { self.get().set_caret(value); })
        .def_prop_ro("has_selection", [](const TextAreaRef& self) {
            return self.get().has_selection();
        })
        .def_prop_ro("selection_start", [](const TextAreaRef& self) {
            return self.get().selection_start();
        })
        .def_prop_ro("selection_end", [](const TextAreaRef& self) {
            return self.get().selection_end();
        })
        .def_prop_ro("selected_text", [](const TextAreaRef& self) {
            return self.get().selected_text();
        })
        .def_prop_ro("scroll_x", [](const TextAreaRef& self) { return self.get().scroll_x(); })
        .def_prop_ro("scroll_y", [](const TextAreaRef& self) { return self.get().scroll_y(); })
        .def("select", [](const TextAreaRef& self, size_t anchor, size_t caret) {
            self.get().select(anchor, caret);
        }, nb::arg("anchor"), nb::arg("caret"))
        .def("select_all", [](const TextAreaRef& self) { self.get().select_all(); })
        .def("clear_selection", [](const TextAreaRef& self) { self.get().clear_selection(); });

    nb::class_<SpinBoxRef>(m, "SpinBox")
        .def_prop_ro("widget", [](const SpinBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SpinBoxRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_rw("value",
            [](const SpinBoxRef& self) { return self.get().value(); },
            [](const SpinBoxRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("min_value", [](const SpinBoxRef& self) { return self.get().min_value(); })
        .def_prop_ro("max_value", [](const SpinBoxRef& self) { return self.get().max_value(); })
        .def_prop_rw("step",
            [](const SpinBoxRef& self) { return self.get().step(); },
            [](const SpinBoxRef& self, float value) { self.get().set_step(value); })
        .def_prop_rw("decimals",
            [](const SpinBoxRef& self) { return self.get().decimals(); },
            [](const SpinBoxRef& self, int value) { self.get().set_decimals(value); })
        .def_prop_ro("editing", [](const SpinBoxRef& self) { return self.get().editing(); })
        .def("set_range", [](const SpinBoxRef& self, float minimum, float maximum) {
            self.get().set_range(minimum, maximum);
            self.widget.throw_pending_exception();
        }, nb::arg("minimum"), nb::arg("maximum"))
        .def("connect_changed", [](const SpinBoxRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::SpinBox&,
                                                   float value) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback(value);
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] SpinBox changed callback failed");
                }
            });
        }, nb::arg("callback"));

    nb::class_<SliderEditRef>(m, "SliderEdit")
        .def_prop_ro("widget", [](const SliderEditRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SliderEditRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_rw("value",
            [](const SliderEditRef& self) { return self.get().value(); },
            [](const SliderEditRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_rw("label",
            [](const SliderEditRef& self) { return self.get().label(); },
            [](const SliderEditRef& self, const std::string& value) { self.get().set_label(value); })
        .def_prop_ro("slider_handle", [](const SliderEditRef& self) {
            return WidgetHandle {self.get().slider_handle()};
        })
        .def_prop_ro("spin_box_handle", [](const SliderEditRef& self) {
            return WidgetHandle {self.get().spin_box_handle()};
        })
        .def("set_range", [](const SliderEditRef& self, float minimum, float maximum) {
            self.get().set_range(minimum, maximum);
            self.widget.throw_pending_exception();
        }, nb::arg("minimum"), nb::arg("maximum"))
        .def("set_step", [](const SliderEditRef& self, float step) { self.get().set_step(step); })
        .def("set_decimals", [](const SliderEditRef& self, int decimals) {
            self.get().set_decimals(decimals);
        })
        .def("connect_changed", [](const SliderEditRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::SliderEdit&,
                                                   float value) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback(value);
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] SliderEdit changed callback failed");
                }
            });
        }, nb::arg("callback"));

    nb::class_<ComboBoxRef>(m, "ComboBox")
        .def_prop_ro("widget", [](const ComboBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ComboBoxRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_ro("item_count", [](const ComboBoxRef& self) { return self.get().item_count(); })
        .def_prop_rw("selected_index",
            [](const ComboBoxRef& self) { return self.get().selected_index(); },
            [](const ComboBoxRef& self, int value) {
                self.get().set_selected_index(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("selected_text", [](const ComboBoxRef& self) { return self.get().selected_text(); })
        .def_prop_ro("open", [](const ComboBoxRef& self) { return self.get().open(); })
        .def("add_item", [](const ComboBoxRef& self, const std::string& item) {
            self.get().add_item(item);
        }, nb::arg("item"))
        .def("item_text", [](const ComboBoxRef& self, size_t index) {
            return self.get().item_text(index);
        }, nb::arg("index"))
        .def("clear", [](const ComboBoxRef& self) { self.get().clear_items(); })
        .def("connect_changed", [](const ComboBoxRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::ComboBox&,
                                                   int index,
                                                   const std::string& text) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback(index, text);
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] ComboBox changed callback failed");
                }
            });
        }, nb::arg("callback"));

    nb::class_<IconButtonRef>(m, "IconButton")
        .def_prop_ro("widget", [](const IconButtonRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const IconButtonRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_rw("active",
            [](const IconButtonRef& self) { return self.get().active(); },
            [](const IconButtonRef& self, bool value) { self.get().set_active(value); })
        .def("set_icon", [](const IconButtonRef& self, const std::string& icon) {
            self.get().set_icon(icon);
        }, nb::arg("icon"))
        .def("set_texture", [](const IconButtonRef& self, tgfx::TextureHandle texture) {
            self.get().set_texture(texture.id);
        }, nb::arg("texture"))
        .def("connect_clicked", [](const IconButtonRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().clicked().connect([state, callback = std::move(callback)](
                                                   termin::gui_native::IconButton&) {
                try {
                    nb::gil_scoped_acquire gil;
                    callback();
                } catch (...) {
                    if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] IconButton click callback failed");
                }
            });
        }, nb::arg("callback"));

    nb::class_<ImageWidgetRef>(m, "ImageWidget")
        .def_prop_ro("widget", [](const ImageWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ImageWidgetRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_ro("intrinsic_size", [](const ImageWidgetRef& self) {
            return self.get().intrinsic_size();
        })
        .def("set_texture", [](const ImageWidgetRef& self,
                                tgfx::TextureHandle texture,
                                tc_ui_size intrinsic_size) {
            self.get().set_texture(texture.id, intrinsic_size);
        }, nb::arg("texture"), nb::arg("intrinsic_size") = tc_ui_size {})
        .def("set_tint", [](const ImageWidgetRef& self, tc_ui_color tint) {
            self.get().set_tint(termin::gui_native::Color {tint.r, tint.g, tint.b, tint.a});
        }, nb::arg("tint"))
        .def("set_preserve_aspect", [](const ImageWidgetRef& self, bool preserve) {
            self.get().set_preserve_aspect(preserve);
        }, nb::arg("preserve"));

    nb::class_<CanvasRef>(m, "Canvas")
        .def_prop_ro("widget", [](const CanvasRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const CanvasRef& self) { return WidgetHandle {self.widget.handle}; })
        .def_prop_ro("zoom", [](const CanvasRef& self) { return self.get().zoom(); })
        .def("set_texture", [](const CanvasRef& self,
                                tgfx::TextureHandle texture,
                                tc_ui_size image_size) {
            self.get().set_texture(texture.id, image_size);
        }, nb::arg("texture"), nb::arg("image_size") = tc_ui_size {})
        .def("set_overlay_texture", [](const CanvasRef& self, tgfx::TextureHandle texture) {
            self.get().set_overlay_texture(texture.id);
        }, nb::arg("texture"))
        .def("set_zoom", [](const CanvasRef& self, float zoom, tc_ui_point anchor) {
            self.get().set_zoom(zoom, anchor);
        }, nb::arg("zoom"), nb::arg("anchor"))
        .def("fit_in_view", [](const CanvasRef& self) { self.get().fit_in_view(); })
        .def("widget_to_image", [](const CanvasRef& self, tc_ui_point point) {
            return self.get().widget_to_image(point);
        }, nb::arg("point"))
        .def("image_to_widget", [](const CanvasRef& self, tc_ui_point point) {
            return self.get().image_to_widget(point);
        }, nb::arg("point"))
        .def("set_paint_callback", [](const CanvasRef& self, nb::object callback) {
            if (callback.is_none()) {
                self.get().set_paint_callback({});
                return;
            }
            auto state = self.widget.state;
            self.get().set_paint_callback(
                [state, callback = std::move(callback)](
                    termin::gui_native::Canvas&,
                    tc_ui_paint_context* context) {
                    try {
                        nb::gil_scoped_acquire gil;
                        PaintContext borrowed(context, false);
                        callback(std::move(borrowed));
                    } catch (...) {
                        if (state && !state->pending_exception) state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Canvas paint callback failed");
                    }
                }
            );
        }, nb::arg("callback"));

    nb::enum_<tc_ui_draw_command_type>(m, "DrawCommandType")
        .value("FillRect", TC_UI_DRAW_FILL_RECT)
        .value("StrokeRect", TC_UI_DRAW_STROKE_RECT)
        .value("Line", TC_UI_DRAW_LINE)
        .value("PushClip", TC_UI_DRAW_PUSH_CLIP)
        .value("PopClip", TC_UI_DRAW_POP_CLIP)
        .value("Text", TC_UI_DRAW_TEXT)
        .value("FillRoundedRect", TC_UI_DRAW_FILL_ROUNDED_RECT)
        .value("StrokeRoundedRect", TC_UI_DRAW_STROKE_ROUNDED_RECT)
        .value("FillCircle", TC_UI_DRAW_FILL_CIRCLE)
        .value("StrokeCircle", TC_UI_DRAW_STROKE_CIRCLE)
        .value("Arc", TC_UI_DRAW_ARC)
        .value("Polyline", TC_UI_DRAW_POLYLINE)
        .value("Texture", TC_UI_DRAW_TEXTURE);

    nb::class_<DrawCommand>(m, "DrawCommand")
        .def_prop_ro("type", [](const DrawCommand& command) { return command.value.type; })
        .def_prop_ro("rect", [](const DrawCommand& command) { return command.value.rect; })
        .def_prop_ro("p0", [](const DrawCommand& command) { return command.value.p0; })
        .def_prop_ro("p1", [](const DrawCommand& command) { return command.value.p1; })
        .def_prop_ro("color", [](const DrawCommand& command) { return command.value.color; })
        .def_prop_ro("thickness", [](const DrawCommand& command) { return command.value.thickness; })
        .def_prop_ro("text", [](const DrawCommand& command) {
            return command.text;
        })
        .def_prop_ro("font_size", [](const DrawCommand& command) {
            return command.value.font_size;
        })
        .def_prop_ro("radius", [](const DrawCommand& command) {
            return command.value.radius;
        })
        .def_prop_ro("start_radians", [](const DrawCommand& command) {
            return command.value.start_radians;
        })
        .def_prop_ro("end_radians", [](const DrawCommand& command) {
            return command.value.end_radians;
        })
        .def_prop_ro("segments", [](const DrawCommand& command) {
            return command.value.segments;
        })
        .def_prop_ro("points", [](const DrawCommand& command) {
            return command.points;
        })
        .def_prop_ro("texture_id", [](const DrawCommand& command) {
            return command.value.texture_id;
        })
        .def_prop_ro("flip_v", [](const DrawCommand& command) {
            return command.value.flip_v;
        });

    nb::class_<DrawList>(m, "DrawList")
        .def(nb::init<>())
        .def("clear", [](DrawList& self) {
            tc_ui_draw_list_clear(self.get());
        })
        .def_prop_ro("command_count", [](const DrawList& self) {
            return tc_ui_draw_list_command_count(self.get());
        })
        .def("command_at", &command_at_checked, nb::arg("index"))
        .def_prop_ro("commands", [](const DrawList& self) {
            nb::list commands;
            const size_t count = tc_ui_draw_list_command_count(self.get());
            for (size_t i = 0; i < count; ++i) {
                commands.append(command_at_checked(self, i));
            }
            return commands;
        });

    nb::class_<PaintContext>(m, "PaintContext")
        .def(nb::init<DrawList&>(), nb::arg("draw_list"), nb::keep_alive<1, 2>())
        .def("fill_rect", [](PaintContext& self, tc_ui_rect rect, tc_ui_color color) {
            tc_ui_painter_fill_rect(self.get(), rect, color);
        }, nb::arg("rect"), nb::arg("color"))
        .def("fill_rounded_rect", [](PaintContext& self,
                                      tc_ui_rect rect,
                                      float radius,
                                      tc_ui_color color) {
            tc_ui_painter_fill_rounded_rect(self.get(), rect, radius, color);
        }, nb::arg("rect"), nb::arg("radius"), nb::arg("color"))
        .def("stroke_rect", [](PaintContext& self, tc_ui_rect rect, tc_ui_color color, float thickness) {
            tc_ui_painter_stroke_rect(self.get(), rect, color, thickness);
        }, nb::arg("rect"), nb::arg("color"), nb::arg("thickness"))
        .def("stroke_rounded_rect", [](PaintContext& self,
                                        tc_ui_rect rect,
                                        float radius,
                                        tc_ui_color color,
                                        float thickness) {
            tc_ui_painter_stroke_rounded_rect(self.get(), rect, radius, color, thickness);
        }, nb::arg("rect"), nb::arg("radius"), nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def("fill_circle", [](PaintContext& self,
                                tc_ui_point center,
                                float radius,
                                tc_ui_color color,
                                int32_t segments) {
            tc_ui_painter_fill_circle(self.get(), center, radius, color, segments);
        }, nb::arg("center"), nb::arg("radius"), nb::arg("color"), nb::arg("segments") = 0)
        .def("stroke_circle", [](PaintContext& self,
                                  tc_ui_point center,
                                  float radius,
                                  tc_ui_color color,
                                  float thickness,
                                  int32_t segments) {
            tc_ui_painter_stroke_circle(self.get(), center, radius, color, thickness, segments);
        }, nb::arg("center"), nb::arg("radius"), nb::arg("color"),
           nb::arg("thickness") = 1.0f, nb::arg("segments") = 0)
        .def("draw_arc", [](PaintContext& self,
                             tc_ui_point center,
                             float radius,
                             float start_radians,
                             float end_radians,
                             tc_ui_color color,
                             float thickness,
                             int32_t segments) {
            tc_ui_painter_draw_arc(
                self.get(), center, radius, start_radians, end_radians,
                color, thickness, segments
            );
        }, nb::arg("center"), nb::arg("radius"), nb::arg("start_radians"),
           nb::arg("end_radians"), nb::arg("color"),
           nb::arg("thickness") = 1.0f, nb::arg("segments") = 0)
        .def("draw_line", [](PaintContext& self, tc_ui_point p0, tc_ui_point p1, tc_ui_color color, float thickness) {
            tc_ui_painter_draw_line(self.get(), p0, p1, color, thickness);
        }, nb::arg("p0"), nb::arg("p1"), nb::arg("color"), nb::arg("thickness"))
        .def("draw_polyline", [](PaintContext& self,
                                  const std::vector<tc_ui_point>& points,
                                  tc_ui_color color,
                                  float thickness) {
            tc_ui_painter_draw_polyline(
                self.get(), points.data(), points.size(), color, thickness
            );
        }, nb::arg("points"), nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def("draw_texture", [](PaintContext& self,
                                 tgfx::TextureHandle texture,
                                 tc_ui_rect rect,
                                 tc_ui_color tint,
                                 bool flip_v) {
            tc_ui_painter_draw_texture(self.get(), texture.id, rect, tint, flip_v);
        }, nb::arg("texture"), nb::arg("rect"),
           nb::arg("tint") = tc_ui_color {1.0f, 1.0f, 1.0f, 1.0f},
           nb::arg("flip_v") = false)
        .def("draw_image", [](PaintContext& self,
                               tgfx::TextureHandle texture,
                               tc_ui_rect rect,
                               tc_ui_color tint,
                               bool flip_v) {
            tc_ui_painter_draw_texture(self.get(), texture.id, rect, tint, flip_v);
        }, nb::arg("texture"), nb::arg("rect"),
           nb::arg("tint") = tc_ui_color {1.0f, 1.0f, 1.0f, 1.0f},
           nb::arg("flip_v") = false)
        .def("draw_text", [](PaintContext& self, const std::string& text, tc_ui_point position, float font_size, tc_ui_color color) {
            tc_ui_painter_draw_text(self.get(), text.c_str(), position, font_size, color);
        }, nb::arg("text"), nb::arg("position"), nb::arg("font_size"), nb::arg("color"))
        .def("push_clip", [](PaintContext& self, tc_ui_rect rect) {
            tc_ui_painter_push_clip(self.get(), rect);
        }, nb::arg("rect"))
        .def("pop_clip", [](PaintContext& self) {
            tc_ui_painter_pop_clip(self.get());
        });

    nb::class_<Document>(m, "Document")
        .def(nb::init<>())
        .def_prop_rw("theme",
            [](const Document& self) {
                return Theme {*tc_ui_document_theme(self.get())};
            },
            [](Document& self, const Theme& theme) {
                if (!tc_ui_document_set_theme(self.get(), &theme.value)) {
                    throw std::invalid_argument("invalid native UI theme");
                }
            })
        .def_prop_ro("theme_revision", [](const Document& self) {
            return tc_ui_document_theme_revision(self.get());
        })
        .def("adopt", [](Document& self, nb::object widget, const std::string& debug_name) {
            return self.adopt(std::move(widget), debug_name);
        }, nb::arg("widget"), nb::arg("debug_name") = "")
        .def("add_root", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_add_root(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("adopt_root", [](Document& self, nb::object widget, const std::string& debug_name) {
            WidgetHandle handle = self.adopt(std::move(widget), debug_name);
            if (!tc_ui_document_add_root(self.get(), handle.handle)) {
                tc_ui_document_destroy_widget(self.get(), handle.handle);
                self.throw_pending_exception();
                throw std::runtime_error("failed to add Python widget root");
            }
            return handle;
        }, nb::arg("widget"), nb::arg("debug_name") = "")
        .def("remove_root", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_remove_root(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("destroy_widget", [](Document& self, WidgetHandle handle) {
            bool destroyed = tc_ui_document_destroy_widget(self.get(), handle.handle);
            self.throw_pending_exception();
            return destroyed;
        }, nb::arg("handle"))
        .def("destroy_widget_recursive", [](Document& self, WidgetHandle handle) {
            bool destroyed = tc_ui_document_destroy_widget_recursive(self.get(), handle.handle);
            self.throw_pending_exception();
            return destroyed;
        }, nb::arg("handle"))
        .def("is_alive", [](const Document& self, WidgetHandle handle) {
            return tc_ui_document_is_alive(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def_prop_ro("live_widget_count", [](const Document& self) {
            return tc_ui_document_live_widget_count(self.get());
        })
        .def_prop_ro("root_count", [](const Document& self) {
            return tc_ui_document_root_count(self.get());
        })
        .def("root_at", [](const Document& self, size_t index) {
            return WidgetHandle {tc_ui_document_root_at(self.get(), index)};
        }, nb::arg("index"))
        .def("ref", &Document::ref, nb::arg("handle"))
        .def("measure_text", [](Document& self, const std::string& text, float font_size) {
            tc_ui_text_metrics metrics {};
            if (!tc_ui_document_measure_text(
                    self.get(),
                    text.data(),
                    text.size(),
                    font_size,
                    &metrics)) {
                throw std::runtime_error("text measurement failed");
            }
            return metrics;
        }, nb::arg("text"), nb::arg("font_size"))
        .def("set_clipboard_handlers", &Document::set_clipboard_handlers,
             nb::arg("getter"), nb::arg("setter"))
        .def("create_hstack", [](Document& self, const std::string& debug_name) {
            return self.make_native<termin::gui_native::HStack>(debug_name.c_str());
        }, nb::arg("debug_name") = "HStack")
        .def("create_vstack", [](Document& self, const std::string& debug_name) {
            return self.make_native<termin::gui_native::VStack>(debug_name.c_str());
        }, nb::arg("debug_name") = "VStack")
        .def("create_panel", [](Document& self, const std::string& debug_name) {
            return self.make_native<termin::gui_native::Panel>(debug_name.c_str());
        }, nb::arg("debug_name") = "Panel")
        .def("create_label", [](Document& self, const std::string& text, const std::string& debug_name) {
            WidgetRef result = self.make_native<termin::gui_native::Label>(text);
            termin::gui_native::Widget* widget = static_cast<termin::gui_native::Widget*>(
                result.resolve_checked()->body
            );
            widget->set_debug_name(debug_name);
            return result;
        }, nb::arg("text"), nb::arg("debug_name") = "Label")
        .def("create_button", [](Document& self, const std::string& text, const std::string& debug_name) {
            WidgetRef result = self.make_native<termin::gui_native::Button>(text);
            termin::gui_native::Widget* widget = static_cast<termin::gui_native::Widget*>(
                result.resolve_checked()->body
            );
            widget->set_debug_name(debug_name);
            return result;
        }, nb::arg("text") = "", nb::arg("debug_name") = "Button")
        .def("create_text_input", [](Document& self, const std::string& text) {
            return TextInputRef {self.make_native<termin::gui_native::TextInput>(text)};
        }, nb::arg("text") = "")
        .def("create_text_area", [](Document& self, const std::string& text) {
            return TextAreaRef {self.make_native<termin::gui_native::TextArea>(text)};
        }, nb::arg("text") = "")
        .def("create_spin_box", [](Document& self, float value) {
            return SpinBoxRef {self.make_native<termin::gui_native::SpinBox>(value)};
        }, nb::arg("value") = 0.0f)
        .def("create_slider_edit", [](Document& self, float value) {
            return SliderEditRef {self.make_native<termin::gui_native::SliderEdit>(value)};
        }, nb::arg("value") = 0.0f)
        .def("create_combo_box", [](Document& self) {
            return ComboBoxRef {self.make_native<termin::gui_native::ComboBox>()};
        })
        .def("create_icon_button", [](Document& self, const std::string& icon) {
            return IconButtonRef {self.make_native<termin::gui_native::IconButton>(icon)};
        }, nb::arg("icon") = "")
        .def("create_image_widget", [](Document& self) {
            return ImageWidgetRef {self.make_native<termin::gui_native::ImageWidget>()};
        })
        .def("create_canvas", [](Document& self) {
            return CanvasRef {self.make_native<termin::gui_native::Canvas>()};
        })
        .def("layout_roots", [](Document& self, tc_ui_rect rect) {
            tc_ui_document_layout_roots(self.get(), rect);
            self.throw_pending_exception();
        }, nb::arg("rect"))
        .def("paint_roots", [](Document& self, PaintContext& context) {
            tc_ui_document_paint_roots(self.get(), context.get());
            self.throw_pending_exception();
        }, nb::arg("context"))
        .def("paint", [](Document& self, PaintContext& context) {
            tc_ui_document_paint(self.get(), context.get());
            self.throw_pending_exception();
        }, nb::arg("context"))
        .def("show_overlay", [](Document& self, WidgetHandle handle, uint32_t flags) {
            bool shown = tc_ui_document_show_overlay(self.get(), handle.handle, flags);
            self.throw_pending_exception();
            return shown;
        }, nb::arg("handle"), nb::arg("flags") = 0)
        .def("dismiss_overlay", [](Document& self,
                                    WidgetHandle handle,
                                    tc_ui_overlay_dismiss_reason reason) {
            bool dismissed = tc_ui_document_dismiss_overlay(self.get(), handle.handle, reason);
            self.throw_pending_exception();
            return dismissed;
        }, nb::arg("handle"),
           nb::arg("reason") = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
        .def_prop_ro("overlay_count", [](const Document& self) {
            return tc_ui_document_overlay_count(self.get());
        })
        .def("overlay_at", [](const Document& self, size_t index) {
            return WidgetHandle {tc_ui_document_overlay_at(self.get(), index)};
        }, nb::arg("index"))
        .def("overlay_flags_at", [](const Document& self, size_t index) {
            return tc_ui_document_overlay_flags_at(self.get(), index);
        }, nb::arg("index"))
        .def("hit_test", [](Document& self, float x, float y) {
            tc_widget_handle handle = tc_ui_document_hit_test(self.get(), x, y);
            self.throw_pending_exception();
            return WidgetHandle {handle};
        }, nb::arg("x"), nb::arg("y"))
        .def("dispatch_pointer_event", [](Document& self, const tc_ui_pointer_event& event) {
            tc_ui_event_result result = tc_ui_document_dispatch_pointer_event(self.get(), &event);
            self.throw_pending_exception();
            return result;
        }, nb::arg("event"))
        .def("dispatch_key_event", [](Document& self, const tc_ui_key_event& event) {
            tc_ui_event_result result = tc_ui_document_dispatch_key_event(self.get(), &event);
            self.throw_pending_exception();
            return result;
        }, nb::arg("event"))
        .def("dispatch_text_event", [](Document& self, const std::string& text) {
            const tc_ui_text_event event {text.c_str()};
            tc_ui_event_result result = tc_ui_document_dispatch_text_event(self.get(), &event);
            self.throw_pending_exception();
            return result;
        }, nb::arg("text"))
        .def_prop_ro("hovered_widget", [](const Document& self) {
            return WidgetHandle {tc_ui_document_hovered_widget(self.get())};
        })
        .def_prop_ro("pointer_capture", [](const Document& self) {
            return WidgetHandle {tc_ui_document_pointer_capture(self.get())};
        })
        .def_prop_ro("pressed_widget", [](const Document& self) {
            return WidgetHandle {tc_ui_document_pressed_widget(self.get())};
        })
        .def("set_pointer_capture", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_set_pointer_capture(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("release_pointer_capture", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_release_pointer_capture(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def_prop_ro("focused_widget", [](const Document& self) {
            return WidgetHandle {tc_ui_document_focused_widget(self.get())};
        })
        .def("set_focus", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_set_focus(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("clear_focus", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_clear_focus(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("focus_next", [](Document& self) {
            return tc_ui_document_focus_next(self.get());
        })
        .def("focus_previous", [](Document& self) {
            return tc_ui_document_focus_previous(self.get());
        });

    nb::class_<termin::gui_native::UiDrawListRenderer>(m, "DrawListRenderer")
        .def(nb::init<>())
        .def("set_default_font_path", &termin::gui_native::UiDrawListRenderer::set_default_font_path,
             nb::arg("path"), nb::arg("default_size_px") = 14)
        .def("bind_text_measurer", [](termin::gui_native::UiDrawListRenderer& self, Document& document) {
            self.bind_text_measurer(document.get());
        }, nb::arg("document"), nb::keep_alive<2, 1>())
        .def("render", [](termin::gui_native::UiDrawListRenderer& self,
                          tgfx::RenderContext2& context,
                          const DrawList& draw_list,
                          int width,
                          int height) {
            self.render(context, draw_list.get(), width, height);
        },
             nb::arg("context"), nb::arg("draw_list"), nb::arg("width"), nb::arg("height"))
        .def("release_gpu", &termin::gui_native::UiDrawListRenderer::release_gpu);
}
