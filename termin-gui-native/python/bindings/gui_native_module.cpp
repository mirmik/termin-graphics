#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

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
    Document() : state_(std::make_shared<DocumentState>()) {
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

private:
    std::shared_ptr<DocumentState> state_;
};

tc_ui_draw_command command_at_checked(const DrawList& draw_list, size_t index) {
    const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list.get(), index);
    if (!command) {
        throw std::out_of_range("draw command index out of range");
    }
    return *command;
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
        .value("Delete", TC_UI_KEY_DELETE)
        .value("Left", TC_UI_KEY_LEFT)
        .value("Right", TC_UI_KEY_RIGHT)
        .value("Home", TC_UI_KEY_HOME)
        .value("End", TC_UI_KEY_END);

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

    nb::enum_<tc_ui_draw_command_type>(m, "DrawCommandType")
        .value("FillRect", TC_UI_DRAW_FILL_RECT)
        .value("StrokeRect", TC_UI_DRAW_STROKE_RECT)
        .value("Line", TC_UI_DRAW_LINE)
        .value("PushClip", TC_UI_DRAW_PUSH_CLIP)
        .value("PopClip", TC_UI_DRAW_POP_CLIP)
        .value("Text", TC_UI_DRAW_TEXT);

    nb::class_<tc_ui_draw_command>(m, "DrawCommand")
        .def_prop_ro("type", [](const tc_ui_draw_command& command) { return command.type; })
        .def_prop_ro("rect", [](const tc_ui_draw_command& command) { return command.rect; })
        .def_prop_ro("p0", [](const tc_ui_draw_command& command) { return command.p0; })
        .def_prop_ro("p1", [](const tc_ui_draw_command& command) { return command.p1; })
        .def_prop_ro("color", [](const tc_ui_draw_command& command) { return command.color; })
        .def_prop_ro("thickness", [](const tc_ui_draw_command& command) { return command.thickness; })
        .def_prop_ro("text", [](const tc_ui_draw_command& command) {
            return command.text ? std::string(command.text) : std::string();
        })
        .def_prop_ro("font_size", [](const tc_ui_draw_command& command) {
            return command.font_size;
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
        .def("stroke_rect", [](PaintContext& self, tc_ui_rect rect, tc_ui_color color, float thickness) {
            tc_ui_painter_stroke_rect(self.get(), rect, color, thickness);
        }, nb::arg("rect"), nb::arg("color"), nb::arg("thickness"))
        .def("draw_line", [](PaintContext& self, tc_ui_point p0, tc_ui_point p1, tc_ui_color color, float thickness) {
            tc_ui_painter_draw_line(self.get(), p0, p1, color, thickness);
        }, nb::arg("p0"), nb::arg("p1"), nb::arg("color"), nb::arg("thickness"))
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
