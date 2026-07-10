#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

tc_value python_to_tc_value(nb::object value) {
    if (value.is_none())
        return tc_value_nil();
    if (nb::isinstance<nb::bool_>(value))
        return tc_value_bool(nb::cast<bool>(value));
    if (nb::isinstance<nb::int_>(value))
        return tc_value_int(nb::cast<int64_t>(value));
    if (nb::isinstance<nb::float_>(value))
        return tc_value_double(nb::cast<double>(value));
    if (nb::isinstance<nb::str>(value))
        return tc_value_string(nb::cast<std::string>(value).c_str());
    if (nb::isinstance<nb::list>(value) || nb::isinstance<nb::tuple>(value)) {
        tc_value result = tc_value_list_new();
        for (nb::handle item : value) {
            tc_value_list_push(&result, python_to_tc_value(nb::borrow<nb::object>(item)));
        }
        return result;
    }
    if (nb::isinstance<nb::dict>(value)) {
        tc_value result = tc_value_dict_new();
        for (auto item : nb::cast<nb::dict>(value)) {
            if (!nb::isinstance<nb::str>(item.first)) {
                tc_value_free(&result);
                throw std::invalid_argument("serialized widget state dict keys must be strings");
            }
            const std::string key = nb::cast<std::string>(item.first);
            tc_value_dict_set(
                &result, key.c_str(),
                python_to_tc_value(nb::borrow<nb::object>(item.second)));
        }
        return result;
    }
    throw std::invalid_argument(
        "serialized widget state must contain only None, bool, int, float, str, list or dict");
}

nb::object tc_value_to_python(const tc_value* value) {
    if (!value)
        return nb::none();
    switch (value->type) {
    case TC_VALUE_NIL:
        return nb::none();
    case TC_VALUE_BOOL:
        return nb::bool_(value->data.b);
    case TC_VALUE_INT:
        return nb::int_(value->data.i);
    case TC_VALUE_FLOAT:
        return nb::float_(value->data.f);
    case TC_VALUE_DOUBLE:
        return nb::float_(value->data.d);
    case TC_VALUE_STRING:
        return value->data.s ? nb::cast(value->data.s) : nb::none();
    case TC_VALUE_LIST: {
        nb::list result;
        for (size_t index = 0; index < value->data.list.count; ++index)
            result.append(tc_value_to_python(&value->data.list.items[index]));
        return result;
    }
    case TC_VALUE_DICT: {
        nb::dict result;
        for (size_t index = 0; index < value->data.dict.count; ++index) {
            const tc_value_dict_entry& item = value->data.dict.entries[index];
            result[item.key] = tc_value_to_python(item.value);
        }
        return result;
    }
    }
    throw std::runtime_error("unknown tc_value type in native UI serialization");
}

std::mutex g_document_states_mutex;
std::unordered_map<tc_ui_document*, std::weak_ptr<DocumentState>> g_document_states;

void register_document_state(const std::shared_ptr<DocumentState>& state) {
    std::lock_guard<std::mutex> lock(g_document_states_mutex);
    g_document_states[state->document] = state;
}

void unregister_document_state(tc_ui_document* document) {
    std::lock_guard<std::mutex> lock(g_document_states_mutex);
    g_document_states.erase(document);
}

std::shared_ptr<DocumentState> find_document_state(tc_ui_document* document) {
    std::lock_guard<std::mutex> lock(g_document_states_mutex);
    const auto found = g_document_states.find(document);
    return found == g_document_states.end() ? nullptr : found->second.lock();
}

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

#define TERMIN_GUI_NATIVE_WIDGET_REF(Name, Type)                                                   \
    struct Name {                                                                                  \
        WidgetRef widget;                                                                          \
        termin::gui_native::Type& get() const {                                                    \
            return native_widget_checked<termin::gui_native::Type>(widget, #Type);                 \
        }                                                                                          \
    }

TERMIN_GUI_NATIVE_WIDGET_REF(SpinBoxRef, SpinBox);
TERMIN_GUI_NATIVE_WIDGET_REF(SliderEditRef, SliderEdit);
TERMIN_GUI_NATIVE_WIDGET_REF(ButtonRef, Button);
TERMIN_GUI_NATIVE_WIDGET_REF(CheckboxRef, Checkbox);
TERMIN_GUI_NATIVE_WIDGET_REF(ScrollAreaRef, ScrollArea);
TERMIN_GUI_NATIVE_WIDGET_REF(SplitterRef, Splitter);
TERMIN_GUI_NATIVE_WIDGET_REF(TabViewRef, TabView);
TERMIN_GUI_NATIVE_WIDGET_REF(RichTextViewRef, RichTextView);
TERMIN_GUI_NATIVE_WIDGET_REF(FrameTimeGraphRef, FrameTimeGraph);
TERMIN_GUI_NATIVE_WIDGET_REF(Viewport3DRef, Viewport3D);
TERMIN_GUI_NATIVE_WIDGET_REF(SceneViewRef, SceneView);
TERMIN_GUI_NATIVE_WIDGET_REF(ListWidgetRef, ListWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(FileGridWidgetRef, FileGridWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(ToolBarRef, ToolBar);
TERMIN_GUI_NATIVE_WIDGET_REF(StatusBarRef, StatusBar);
TERMIN_GUI_NATIVE_WIDGET_REF(MenuRef, Menu);
TERMIN_GUI_NATIVE_WIDGET_REF(MenuBarRef, MenuBar);
TERMIN_GUI_NATIVE_WIDGET_REF(DialogRef, Dialog);
TERMIN_GUI_NATIVE_WIDGET_REF(MessageBoxRef, MessageBox);
TERMIN_GUI_NATIVE_WIDGET_REF(InputDialogRef, InputDialog);
TERMIN_GUI_NATIVE_WIDGET_REF(FileDialogOverlayRef, FileDialogOverlay);
TERMIN_GUI_NATIVE_WIDGET_REF(ColorPickerRef, ColorPicker);
TERMIN_GUI_NATIVE_WIDGET_REF(ColorDialogRef, ColorDialog);
TERMIN_GUI_NATIVE_WIDGET_REF(TreeWidgetRef, TreeWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(TableWidgetRef, TableWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(ComboBoxRef, ComboBox);
TERMIN_GUI_NATIVE_WIDGET_REF(IconButtonRef, IconButton);
TERMIN_GUI_NATIVE_WIDGET_REF(ImageWidgetRef, ImageWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(CanvasRef, Canvas);

#undef TERMIN_GUI_NATIVE_WIDGET_REF

class PythonViewportSurfaceHost final : public termin::gui_native::ViewportSurfaceHost {
public:
    PythonViewportSurfaceHost(nb::object object, std::shared_ptr<DocumentState> state)
        : object_(std::move(object)), state_(std::move(state)) {}

    bool is_valid() const override {
        return invoke<bool>([this] { return nb::cast<bool>(object_.attr("is_valid")()); });
    }

    uint32_t texture_id() const override {
        return invoke<uint32_t>(
            [this] { return nb::cast<uint32_t>(object_.attr("get_tgfx_color_tex_id")()); });
    }

    termin::gui_native::ViewportSurfaceSize framebuffer_size() const override {
        return invoke<termin::gui_native::ViewportSurfaceSize>([this] {
            nb::tuple size = nb::cast<nb::tuple>(object_.attr("framebuffer_size")());
            if (size.size() != 2) {
                throw std::runtime_error(
                    "viewport surface framebuffer_size() must return two values");
            }
            return termin::gui_native::ViewportSurfaceSize{nb::cast<int>(size[0]),
                                                           nb::cast<int>(size[1])};
        });
    }

    bool resize(int width, int height) override {
        return invoke<bool>([this, width, height] {
            return nb::cast<bool>(object_.attr("resize")(width, height));
        });
    }

    bool pointer_move(double x, double y) override {
        return invoke<bool>(
            [this, x, y] { return nb::cast<bool>(object_.attr("dispatch_pointer_move")(x, y)); });
    }

    bool pointer_button(int button, int action, int modifiers, uint32_t click_count) override {
        return invoke<bool>([this, button, action, modifiers, click_count] {
            return nb::cast<bool>(
                object_.attr("dispatch_pointer_button")(button, action, modifiers, click_count));
        });
    }

    bool scroll(double x, double y, int modifiers) override {
        return invoke<bool>([this, x, y, modifiers] {
            return nb::cast<bool>(object_.attr("dispatch_scroll")(x, y, modifiers));
        });
    }

    bool key(int key, int scancode, int action, int modifiers) override {
        return invoke<bool>([this, key, scancode, action, modifiers] {
            return nb::cast<bool>(object_.attr("dispatch_key")(key, scancode, action, modifiers));
        });
    }

    bool text(uint32_t codepoint) override {
        return invoke<bool>(
            [this, codepoint] { return nb::cast<bool>(object_.attr("dispatch_text")(codepoint)); });
    }

private:
    template <typename Result, typename Callback> Result invoke(Callback&& callback) const {
        nb::gil_scoped_acquire gil;
        try {
            return callback();
        } catch (...) {
            if (state_ && !state_->pending_exception)
                state_->pending_exception = std::current_exception();
            throw;
        }
    }

    nb::object object_;
    std::shared_ptr<DocumentState> state_;
};

struct PythonWidget {
    uint32_t magic = PYTHON_WIDGET_MAGIC;
    tc_widget widget{};
    nb::object object;
    std::shared_ptr<DocumentState> state;
    bool callbacks_enabled = false;

    explicit PythonWidget(nb::object object_, std::string debug_name_,
                          std::shared_ptr<DocumentState> state_)
        : object(std::move(object_)), state(std::move(state_)) {
        tc_widget_init(&widget, &VTABLE, &PythonWidget::delete_widget, TC_LANGUAGE_PYTHON, this);
        if (!tc_widget_set_debug_name(&widget, debug_name_.c_str())) {
            throw std::runtime_error("failed to set Python widget debug name");
        }
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
            tc_widget_debug_name(&widget) ? tc_widget_debug_name(&widget) : "<unnamed>"
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

struct PythonWidgetFactory {
    nb::object callable;
    std::string debug_name;
    nb::object serialize_state;
    nb::object deserialize_state;
};

bool create_python_registered_widget(tc_ui_document* document, void* userdata,
                                     tc_widget_factory_result* result) {
    auto* factory = static_cast<PythonWidgetFactory*>(userdata);
    std::shared_ptr<DocumentState> state = find_document_state(document);
    if (!factory || !state || !result) {
        tc_log_error("[termin-gui-native/python] registered widget factory has no document state");
        return false;
    }
    nb::gil_scoped_acquire gil;
    try {
        nb::object object = factory->callable();
        auto* widget = new PythonWidget(object, factory->debug_name, state);
        *result = tc_widget_factory_result{
            &widget->widget,
            &PythonWidget::delete_widget,
            TC_WIDGET_OWNED,
        };
        return true;
    } catch (...) {
        if (!state->pending_exception)
            state->pending_exception = std::current_exception();
        tc_log_error("[termin-gui-native/python] registered widget constructor failed");
        return false;
    }
}

bool bind_python_registered_widget(tc_ui_document*, tc_widget* widget,
                                   tc_widget_handle handle, void*) {
    PythonWidget* python_widget = PythonWidget::from_widget(widget);
    if (!python_widget || !python_widget->state) {
        tc_log_error("[termin-gui-native/python] registered widget adoption lost Python body");
        return false;
    }
    nb::gil_scoped_acquire gil;
    try {
        python_widget->object.attr("_bind_native")(
            WidgetRef{python_widget->state, handle}
        );
        python_widget->callbacks_enabled = true;
        return true;
    } catch (...) {
        if (!python_widget->state->pending_exception)
            python_widget->state->pending_exception = std::current_exception();
        tc_log_error("[termin-gui-native/python] registered widget bind failed");
        return false;
    }
}

void destroy_python_widget_factory(void* userdata) {
    if (!userdata)
        return;
    nb::gil_scoped_acquire gil;
    delete static_cast<PythonWidgetFactory*>(userdata);
}

bool serialize_python_registered_widget(const tc_widget* widget, void* userdata,
                                        tc_value* out_state) {
    auto* factory = static_cast<PythonWidgetFactory*>(userdata);
    PythonWidget* python_widget = PythonWidget::from_widget(const_cast<tc_widget*>(widget));
    if (!factory || !python_widget || !out_state) {
        tc_log_error("[termin-gui-native/python] invalid Python widget state serializer context");
        return false;
    }
    if (factory->serialize_state.is_none())
        return true;
    nb::gil_scoped_acquire gil;
    try {
        tc_value state = python_to_tc_value(factory->serialize_state(python_widget->object));
        if (state.type != TC_VALUE_DICT) {
            tc_value_free(&state);
            throw std::invalid_argument("widget state serializer must return a dict");
        }
        tc_value_free(out_state);
        *out_state = state;
        return true;
    } catch (...) {
        if (!python_widget->state->pending_exception)
            python_widget->state->pending_exception = std::current_exception();
        tc_log_error("[termin-gui-native/python] registered widget state serialization failed");
        return false;
    }
}

bool deserialize_python_registered_widget(tc_widget* widget, const tc_value* state,
                                          void* userdata) {
    auto* factory = static_cast<PythonWidgetFactory*>(userdata);
    PythonWidget* python_widget = PythonWidget::from_widget(widget);
    if (!factory || !python_widget || !state) {
        tc_log_error("[termin-gui-native/python] invalid Python widget state deserializer context");
        return false;
    }
    if (factory->deserialize_state.is_none())
        return tc_value_dict_size(state) == 0;
    nb::gil_scoped_acquire gil;
    try {
        factory->deserialize_state(python_widget->object, tc_value_to_python(state));
        return true;
    } catch (...) {
        if (!python_widget->state->pending_exception)
            python_widget->state->pending_exception = std::current_exception();
        tc_log_error("[termin-gui-native/python] registered widget state deserialization failed");
        return false;
    }
}

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
        register_document_state(state_);
    }

    ~Document() {
        if (state_ && state_->document) {
            unregister_document_state(state_->document);
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

    WidgetRef create_registered_widget(const std::string& type_name) {
        const tc_widget_handle handle =
            tc_ui_document_create_registered_widget(get(), type_name.c_str());
        throw_pending_exception();
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to create registered widget type '" + type_name + "'");
        }
        return WidgetRef {state_, handle};
    }

    nb::object serialize() {
        tc_value value = tc_ui_document_serialize(get());
        throw_pending_exception();
        if (value.type != TC_VALUE_DICT) {
            throw std::runtime_error("failed to serialize native UI document");
        }
        try {
            nb::object result = tc_value_to_python(&value);
            tc_value_free(&value);
            return result;
        } catch (...) {
            tc_value_free(&value);
            throw;
        }
    }

    void restore(nb::object serialized) {
        tc_value value = python_to_tc_value(std::move(serialized));
        bool restored = tc_ui_document_restore(get(), &value);
        tc_value_free(&value);
        throw_pending_exception();
        if (!restored) {
            throw std::runtime_error("failed to restore native UI document");
        }
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

nb::object snapshot_handle_or_none(tc_widget_handle handle) {
    return tc_widget_handle_is_invalid(handle) ? nb::none()
                                               : nb::cast(WidgetHandle {handle});
}

nb::dict document_snapshot_to_python(const Document& document) {
    termin::gui_native::DocumentSnapshot snapshot(document.get());
    nb::dict result;
    nb::list widgets;
    for (const tc_ui_widget_snapshot& widget : snapshot.widgets()) {
        nb::dict item;
        item["handle"] = WidgetHandle {widget.handle};
        item["parent"] = snapshot_handle_or_none(widget.parent);
        item["type_name"] = widget.type_name ? widget.type_name : "";
        item["stable_id"] = widget.stable_id ? nb::cast(widget.stable_id) : nb::none();
        item["name"] = widget.name ? nb::cast(widget.name) : nb::none();
        item["debug_name"] = widget.debug_name ? nb::cast(widget.debug_name) : nb::none();
        item["native_language"] = widget.native_language;
        item["ownership"] = widget.ownership;
        item["bounds"] = widget.bounds;
        item["min_size"] = widget.min_size;
        item["preferred_size"] = widget.preferred_size;
        item["max_size"] = widget.max_size;
        item["flags"] = widget.flags;
        item["dirty_flags"] = widget.dirty_flags;
        item["style_role"] = widget.style_role;
        item["style_override"] = widget.style_override;
        nb::list children;
        for (size_t index = 0; index < widget.child_count; ++index) {
            children.append(WidgetHandle {snapshot.children()[widget.child_offset + index]});
        }
        item["children"] = std::move(children);
        widgets.append(std::move(item));
    }
    result["widgets"] = std::move(widgets);

    nb::list roots;
    for (tc_widget_handle handle : snapshot.roots()) {
        roots.append(WidgetHandle {handle});
    }
    result["roots"] = std::move(roots);

    nb::list overlays;
    for (const tc_ui_overlay_snapshot& overlay : snapshot.overlays()) {
        nb::dict item;
        item["handle"] = WidgetHandle {overlay.handle};
        item["flags"] = overlay.flags;
        overlays.append(std::move(item));
    }
    result["overlays"] = std::move(overlays);

    nb::dict interaction;
    interaction["hovered"] = snapshot_handle_or_none(snapshot.data().hovered);
    interaction["pressed"] = snapshot_handle_or_none(snapshot.data().pressed);
    interaction["pointer_capture"] =
        snapshot_handle_or_none(snapshot.data().pointer_capture);
    interaction["focused"] = snapshot_handle_or_none(snapshot.data().focused);
    result["interaction"] = std::move(interaction);
    result["theme_revision"] = snapshot.data().theme_revision;
    return result;
}

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

    nb::class_<WidgetRef>(m, "WidgetRef")
        .def_prop_ro("handle", [](const WidgetRef& self) { return WidgetHandle{self.handle}; })
        .def_prop_ro("alive", &WidgetRef::alive)
        .def("__bool__", &WidgetRef::alive)
        .def_prop_rw(
            "bounds",
            [](const WidgetRef& self) { return tc_widget_bounds(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_rect value) {
                tc_widget_set_bounds(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "min_size",
            [](const WidgetRef& self) { return tc_widget_min_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_min_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "preferred_size",
            [](const WidgetRef& self) { return tc_widget_preferred_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_preferred_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "max_size",
            [](const WidgetRef& self) { return tc_widget_max_size(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_size value) {
                tc_widget_set_max_size(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "visible",
            [](const WidgetRef& self) { return tc_widget_is_visible(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_visible(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "enabled",
            [](const WidgetRef& self) { return tc_widget_is_enabled(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_enabled(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "mouse_transparent",
            [](const WidgetRef& self) {
                return tc_widget_is_mouse_transparent(self.resolve_checked());
            },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_mouse_transparent(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "focusable",
            [](const WidgetRef& self) { return tc_widget_is_focusable(self.resolve_checked()); },
            [](const WidgetRef& self, bool value) {
                tc_widget_set_focusable(self.resolve_checked(), value);
            })
        .def_prop_rw(
            "style_role",
            [](const WidgetRef& self) { return tc_widget_style_role(self.resolve_checked()); },
            [](const WidgetRef& self, tc_ui_style_role role) {
                tc_widget_set_style_role(self.resolve_checked(), role);
            })
        .def_prop_rw(
            "style_override",
            [](const WidgetRef& self) { return tc_widget_style_override(self.resolve_checked()); },
            [](const WidgetRef& self, const tc_ui_style_override& style_override) {
                if (!tc_widget_set_style_override(self.resolve_checked(), &style_override)) {
                    throw std::invalid_argument("invalid native UI style override");
                }
            })
        .def("clear_style_override",
             [](const WidgetRef& self) { tc_widget_clear_style_override(self.resolve_checked()); })
        .def("style_state",
             [](const WidgetRef& self) {
                 return tc_ui_document_widget_style_state(self.state->document,
                                                          self.resolve_checked());
             })
        .def(
            "resolve_style",
            [](const WidgetRef& self, uint32_t extra_state) {
                tc_ui_style style{};
                if (!tc_ui_document_resolve_style(self.state->document, self.resolve_checked(),
                                                  extra_state, &style)) {
                    throw std::runtime_error("failed to resolve native UI widget style");
                }
                return style;
            },
            nb::arg("extra_state") = 0)
        .def_prop_rw(
            "stable_id",
            [](const WidgetRef& self) {
                const char* value = tc_widget_stable_id(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_stable_id(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget stable id");
            })
        .def_prop_rw(
            "name",
            [](const WidgetRef& self) {
                const char* value = tc_widget_name(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_name(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget name");
            })
        .def_prop_rw(
            "debug_name",
            [](const WidgetRef& self) {
                const char* value = tc_widget_debug_name(self.resolve_checked());
                return value ? std::string(value) : std::string();
            },
            [](const WidgetRef& self, const std::string& value) {
                if (!tc_widget_set_debug_name(self.resolve_checked(), value.c_str()))
                    throw std::runtime_error("failed to set widget debug name");
            })
        .def_prop_ro("type_name",
                     [](const WidgetRef& self) {
                         const char* type_name = tc_widget_type_name(self.resolve_checked());
                         return type_name ? std::string(type_name) : std::string();
                     })
        .def_prop_ro("native_language",
                     [](const WidgetRef& self) { return self.resolve_checked()->native_language; })
        .def_prop_ro(
            "ownership",
            [](const WidgetRef& self) { return tc_widget_ownership(self.resolve_checked()); })
        .def_prop_ro(
            "dirty_flags",
            [](const WidgetRef& self) { return tc_widget_dirty_flags(self.resolve_checked()); })
        .def(
            "mark_dirty",
            [](const WidgetRef& self, uint32_t flags) {
                tc_widget_mark_dirty(self.resolve_checked(), flags);
            },
            nb::arg("flags"))
        .def(
            "clear_dirty",
            [](const WidgetRef& self, uint32_t flags) {
                tc_widget_clear_dirty(self.resolve_checked(), flags);
            },
            nb::arg("flags"))
        .def_prop_ro("parent",
                     [](const WidgetRef& self) -> nb::object {
                         tc_widget* parent = tc_widget_parent(self.resolve_checked());
                         return parent ? nb::cast(WidgetRef{self.state, parent->handle})
                                       : nb::none();
                     })
        .def_prop_ro("children",
                     [](const WidgetRef& self) {
                         nb::list children;
                         tc_widget* widget = self.resolve_checked();
                         for (size_t index = 0; index < tc_widget_child_count(widget); ++index) {
                             tc_widget* child = tc_widget_child_at(widget, index);
                             if (child) {
                                 children.append(WidgetRef{self.state, child->handle});
                             }
                         }
                         return children;
                     })
        .def(
            "append_child",
            [](const WidgetRef& self, const WidgetRef& child,
               termin::gui_native::LayoutPolicy policy, float value) {
                tc_widget* parent = self.resolve_checked();
                tc_widget* child_widget = child.resolve_checked();
                if (parent->native_language == TC_LANGUAGE_CXX) {
                    auto* base = static_cast<termin::gui_native::Widget*>(parent->body);
                    if (auto* box = dynamic_cast<termin::gui_native::BoxLayout*>(base)) {
                        box->add_child(child_widget->handle, policy, value);
                        return child_widget->parent == parent;
                    }
                }
                if (policy != termin::gui_native::LayoutPolicy::Stretch || value != 0.0f) {
                    throw std::runtime_error("layout policy requires a BoxLayout parent");
                }
                return tc_widget_append_child(parent, child_widget);
            },
            nb::arg("child"), nb::arg("policy") = termin::gui_native::LayoutPolicy::Stretch,
            nb::arg("value") = 0.0f)
        .def(
            "add_fixed_child",
            [](const WidgetRef& self, const WidgetRef& child, float extent) {
                auto& box = native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout");
                box.add_fixed_child(child.handle, extent);
            },
            nb::arg("child"), nb::arg("extent"))
        .def(
            "add_preferred_child",
            [](const WidgetRef& self, const WidgetRef& child) {
                auto& box = native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout");
                box.add_preferred_child(child.handle);
            },
            nb::arg("child"))
        .def(
            "add_flex_child",
            [](const WidgetRef& self, const WidgetRef& child, float flex) {
                auto& box = native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout");
                box.add_flex_child(child.handle, flex);
            },
            nb::arg("child"), nb::arg("flex") = 1.0f)
        .def(
            "add_stretch_child",
            [](const WidgetRef& self, const WidgetRef& child) {
                auto& box = native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout");
                box.add_stretch_child(child.handle);
            },
            nb::arg("child"))
        .def(
            "set_layout_spacing",
            [](const WidgetRef& self, float spacing) {
                native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout")
                    .set_spacing(spacing);
            },
            nb::arg("spacing"))
        .def(
            "set_layout_padding",
            [](const WidgetRef& self, termin::gui_native::EdgeInsets padding) {
                native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout")
                    .set_padding(padding);
            },
            nb::arg("padding"))
        .def(
            "set_layout_background",
            [](const WidgetRef& self, tc_ui_color color) {
                native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout")
                    .set_background({color.r, color.g, color.b, color.a});
            },
            nb::arg("color"))
        .def(
            "set_layout_border",
            [](const WidgetRef& self, tc_ui_color color, float thickness) {
                native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout")
                    .set_border({color.r, color.g, color.b, color.a}, thickness);
            },
            nb::arg("color"), nb::arg("thickness") = 1.0f)
        .def(
            "set_child_extent_limits",
            [](const WidgetRef& self, const WidgetRef& child, float minimum, float maximum) {
                return native_widget_checked<termin::gui_native::BoxLayout>(self, "BoxLayout")
                    .set_child_extent_limits(child.handle, minimum, maximum);
            },
            nb::arg("child"), nb::arg("minimum"), nb::arg("maximum") = 0.0f)
        .def(
            "insert_child",
            [](const WidgetRef& self, size_t index, const WidgetRef& child) {
                return tc_widget_insert_child(self.resolve_checked(), index,
                                              child.resolve_checked());
            },
            nb::arg("index"), nb::arg("child"))
        .def(
            "remove_child",
            [](const WidgetRef& self, const WidgetRef& child) {
                return tc_widget_remove_child(self.resolve_checked(), child.resolve_checked());
            },
            nb::arg("child"))
        .def("detach",
             [](const WidgetRef& self) { return tc_widget_detach(self.resolve_checked()); })
        .def(
            "measure",
            [](const WidgetRef& self, tc_ui_constraints constraints) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_size result = constraints.min_size;
                if (widget->vtable && widget->vtable->measure) {
                    result = widget->vtable->measure(widget, self.state->document, constraints);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("constraints"))
        .def(
            "layout",
            [](const WidgetRef& self, tc_ui_rect rect) {
                tc_widget* widget = self.resolve_checked();
                if (widget->vtable && widget->vtable->layout) {
                    widget->vtable->layout(widget, self.state->document, rect);
                }
                self.throw_pending_exception();
            },
            nb::arg("rect"))
        .def(
            "paint",
            [](const WidgetRef& self, PaintContext& context) {
                tc_widget* widget = self.resolve_checked();
                if (widget->vtable && widget->vtable->paint) {
                    widget->vtable->paint(widget, self.state->document, context.get());
                }
                self.throw_pending_exception();
            },
            nb::arg("context"))
        .def(
            "hit_test",
            [](const WidgetRef& self, float x, float y) {
                tc_widget* widget = self.resolve_checked();
                tc_widget_handle result = tc_widget_handle_invalid();
                if (widget->vtable && widget->vtable->hit_test) {
                    result = widget->vtable->hit_test(widget, self.state->document, x, y);
                }
                self.throw_pending_exception();
                return WidgetHandle{result};
            },
            nb::arg("x"), nb::arg("y"))
        .def(
            "dispatch_pointer_event",
            [](const WidgetRef& self, const tc_ui_pointer_event& event) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_event_result result = TC_UI_EVENT_IGNORED;
                if (widget->vtable && widget->vtable->pointer_event) {
                    result = widget->vtable->pointer_event(widget, self.state->document, &event);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("event"))
        .def(
            "dispatch_key_event",
            [](const WidgetRef& self, const tc_ui_key_event& event) {
                tc_widget* widget = self.resolve_checked();
                tc_ui_event_result result = TC_UI_EVENT_IGNORED;
                if (widget->vtable && widget->vtable->key_event) {
                    result = widget->vtable->key_event(widget, self.state->document, &event);
                }
                self.throw_pending_exception();
                return result;
            },
            nb::arg("event"))
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
        .def("clear_selection", [](const TextInputRef& self) { self.get().clear_selection(); })
        .def("connect_changed", [](const TextInputRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TextInput&, const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] TextInput changed callback failed");
                    }
                });
        }, nb::arg("callback"))
        .def("connect_submitted", [](const TextInputRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().submitted().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::TextInput&, const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] TextInput submitted callback failed");
                    }
                });
        }, nb::arg("callback"));

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
        .def_prop_ro("handle",
                     [](const SliderEditRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "value", [](const SliderEditRef& self) { return self.get().value(); },
            [](const SliderEditRef& self, float value) {
                self.get().set_value(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_rw(
            "label", [](const SliderEditRef& self) { return self.get().label(); },
            [](const SliderEditRef& self, const std::string& value) {
                self.get().set_label(value);
            })
        .def_prop_ro(
            "slider_handle",
            [](const SliderEditRef& self) { return WidgetHandle{self.get().slider_handle()}; })
        .def_prop_ro(
            "spin_box_handle",
            [](const SliderEditRef& self) { return WidgetHandle{self.get().spin_box_handle()}; })
        .def(
            "set_range",
            [](const SliderEditRef& self, float minimum, float maximum) {
                self.get().set_range(minimum, maximum);
                self.widget.throw_pending_exception();
            },
            nb::arg("minimum"), nb::arg("maximum"))
        .def("set_step", [](const SliderEditRef& self, float step) { self.get().set_step(step); })
        .def("set_decimals",
             [](const SliderEditRef& self, int decimals) { self.get().set_decimals(decimals); })
        .def(
            "connect_changed",
            [](const SliderEditRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::SliderEdit&,
                                                            float value) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(value);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] SliderEdit changed callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<ButtonRef>(m, "Button")
        .def_prop_ro("widget", [](const ButtonRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ButtonRef& self) { return WidgetHandle{self.widget.handle}; })
        .def(
            "set_text",
            [](const ButtonRef& self, const std::string& text) { self.get().set_text(text); },
            nb::arg("text"))
        .def(
            "connect_clicked",
            [](const ButtonRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().clicked().connect(
                    [state, callback = std::move(callback)](termin::gui_native::Button&) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback();
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error("[termin-gui-native/python] Button click callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<CheckboxRef>(m, "Checkbox")
        .def_prop_ro("widget", [](const CheckboxRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const CheckboxRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "checked", [](const CheckboxRef& self) { return self.get().checked(); },
            [](const CheckboxRef& self, bool checked) {
                self.get().set_checked(checked);
                self.widget.throw_pending_exception();
            })
        .def(
            "connect_changed",
            [](const CheckboxRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect([state, callback = std::move(callback)](
                                                        termin::gui_native::Checkbox&,
                                                        bool checked) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(checked);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error("[termin-gui-native/python] Checkbox changed callback failed");
                    }
                });
            },
            nb::arg("callback"));

    nb::class_<ScrollAreaRef>(m, "ScrollArea")
        .def_prop_ro("widget",
                     [](const ScrollAreaRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ScrollAreaRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_ro("content_handle",
                     [](const ScrollAreaRef &self) {
                       return WidgetHandle{self.get().content()};
                     })
        .def_prop_rw(
            "scroll_x",
            [](const ScrollAreaRef &self) { return self.get().scroll_x(); },
            [](const ScrollAreaRef &self, float value) {
              self.get().set_scroll(value, self.get().scroll_y());
            })
        .def_prop_rw(
            "scroll_y",
            [](const ScrollAreaRef &self) { return self.get().scroll_y(); },
            [](const ScrollAreaRef &self, float value) {
              self.get().set_scroll(self.get().scroll_x(), value);
            })
        .def_prop_ro(
            "content_size",
            [](const ScrollAreaRef &self) { return self.get().content_size(); })
        .def(
            "set_content",
            [](const ScrollAreaRef &self, const WidgetRef &content) {
              self.get().set_content(content.handle);
              self.widget.throw_pending_exception();
            },
            nb::arg("content"));

    nb::class_<SplitterRef>(m, "Splitter")
        .def_prop_ro("widget", [](const SplitterRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const SplitterRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw(
            "split_fraction",
            [](const SplitterRef& self) { return self.get().split_fraction(); },
            [](const SplitterRef& self, float value) {
                self.get().set_split_fraction(value);
            })
        .def("set_split_fraction", [](const SplitterRef& self, float value) {
            self.get().set_split_fraction(value);
        }, nb::arg("value"))
        .def("set_first", [](const SplitterRef& self, const WidgetRef& widget) {
            if (self.widget.state != widget.state) {
                throw std::invalid_argument("Splitter first widget belongs to another document");
            }
            self.get().set_first(widget.handle);
            self.widget.throw_pending_exception();
        }, nb::arg("widget"))
        .def("set_second", [](const SplitterRef& self, const WidgetRef& widget) {
            if (self.widget.state != widget.state) {
                throw std::invalid_argument("Splitter second widget belongs to another document");
            }
            self.get().set_second(widget.handle);
            self.widget.throw_pending_exception();
        }, nb::arg("widget"))
        .def("set_min_extents", [](const SplitterRef& self, float first, float second) {
            self.get().set_min_extents(first, second);
        }, nb::arg("first"), nb::arg("second"))
        .def("set_divider_thickness", [](const SplitterRef& self, float thickness) {
            self.get().set_divider_thickness(thickness);
        }, nb::arg("thickness"));

    nb::enum_<termin::gui_native::CommandKind>(m, "CommandKind")
        .value("Action", termin::gui_native::CommandKind::Action)
        .value("Separator", termin::gui_native::CommandKind::Separator);

    nb::class_<termin::gui_native::CommandData>(m, "CommandData")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::CommandData* self, std::string stable_id, std::string label,
               std::string icon, std::string shortcut, std::string tooltip,
               termin::gui_native::CommandKind kind, bool enabled, bool checkable, bool checked,
               uint32_t texture_id, std::shared_ptr<termin::gui_native::CommandModel> submenu) {
                new (self) termin::gui_native::CommandData{std::move(stable_id),
                                                           std::move(label),
                                                           std::move(icon),
                                                           std::move(shortcut),
                                                           std::move(tooltip),
                                                           kind,
                                                           enabled,
                                                           checkable,
                                                           checked,
                                                           texture_id,
                                                           std::move(submenu)};
            },
            nb::arg("stable_id"), nb::arg("label") = "", nb::arg("icon") = "",
            nb::arg("shortcut") = "", nb::arg("tooltip") = "",
            nb::arg("kind") = termin::gui_native::CommandKind::Action, nb::arg("enabled") = true,
            nb::arg("checkable") = false, nb::arg("checked") = false, nb::arg("texture_id") = 0,
            nb::arg("submenu") = nullptr)
        .def_rw("stable_id", &termin::gui_native::CommandData::stable_id)
        .def_rw("label", &termin::gui_native::CommandData::label)
        .def_rw("icon", &termin::gui_native::CommandData::icon)
        .def_rw("shortcut", &termin::gui_native::CommandData::shortcut)
        .def_rw("tooltip", &termin::gui_native::CommandData::tooltip)
        .def_rw("kind", &termin::gui_native::CommandData::kind)
        .def_rw("enabled", &termin::gui_native::CommandData::enabled)
        .def_rw("checkable", &termin::gui_native::CommandData::checkable)
        .def_rw("checked", &termin::gui_native::CommandData::checked)
        .def_rw("texture_id", &termin::gui_native::CommandData::texture_id)
        .def_rw("submenu", &termin::gui_native::CommandData::submenu);

    nb::class_<termin::gui_native::Command>(m, "Command")
        .def_prop_ro("id", [](const termin::gui_native::Command& self) { return self.id; })
        .def_prop_ro("data", [](const termin::gui_native::Command& self) { return self.data; });

    nb::class_<termin::gui_native::CommandModel>(m, "CommandModel")
        .def(nb::init<>())
        .def_prop_ro("command_count", &termin::gui_native::CommandModel::size)
        .def_prop_ro("revision", &termin::gui_native::CommandModel::revision)
        .def_prop_ro("commands",
                     [](const termin::gui_native::CommandModel& self) { return self.commands(); })
        .def("contains", &termin::gui_native::CommandModel::contains, nb::arg("command"))
        .def("index_of", &termin::gui_native::CommandModel::index_of, nb::arg("command"))
        .def(
            "command_at",
            [](const termin::gui_native::CommandModel& self, size_t index) {
                return self.command_at(index);
            },
            nb::arg("index"))
        .def(
            "command",
            [](const termin::gui_native::CommandModel& self,
               termin::gui_native::CommandId command) { return self.command(command); },
            nb::arg("command"))
        .def("set_commands", &termin::gui_native::CommandModel::set_commands, nb::arg("commands"))
        .def("append", &termin::gui_native::CommandModel::append, nb::arg("command"))
        .def("insert", &termin::gui_native::CommandModel::insert, nb::arg("index"),
             nb::arg("command"))
        .def("update", &termin::gui_native::CommandModel::update, nb::arg("command"),
             nb::arg("data"))
        .def("set_enabled", &termin::gui_native::CommandModel::set_enabled, nb::arg("command"),
             nb::arg("enabled"))
        .def("set_checked", &termin::gui_native::CommandModel::set_checked, nb::arg("command"),
             nb::arg("checked"))
        .def("erase", &termin::gui_native::CommandModel::erase, nb::arg("command"))
        .def("clear", &termin::gui_native::CommandModel::clear);

    nb::class_<ToolBarRef>(m, "ToolBar")
        .def_prop_ro("widget", [](const ToolBarRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ToolBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const ToolBarRef& self) { return self.get().model(); },
            [](const ToolBarRef& self, std::shared_ptr<termin::gui_native::CommandModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("item_rects", [](const ToolBarRef& self) { return self.get().item_rects(); })
        .def_prop_ro("hovered_index",
                     [](const ToolBarRef& self) -> int64_t {
                         const size_t index = self.get().hovered_index();
                         return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
                     })
        .def_prop_ro("hovered_tooltip",
                     [](const ToolBarRef& self) { return self.get().hovered_tooltip(); })
        .def_prop_rw(
            "item_height", [](const ToolBarRef& self) { return self.get().item_height(); },
            [](const ToolBarRef& self, float value) { self.get().set_item_height(value); })
        .def_prop_rw(
            "padding", [](const ToolBarRef& self) { return self.get().padding(); },
            [](const ToolBarRef& self, float value) { self.get().set_padding(value); })
        .def(
            "connect_activated",
            [](const ToolBarRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ToolBar&, size_t index,
                        termin::gui_native::CommandId id,
                        const termin::gui_native::CommandData& command) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, id, command);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] ToolBar activation callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<TabViewRef>(m, "TabView")
        .def_prop_ro("widget",
                     [](const TabViewRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TabViewRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_ro(
            "page_count",
            [](const TabViewRef &self) { return self.get().page_count(); })
        .def_prop_rw(
            "selected_index",
            [](const TabViewRef &self) { return self.get().selected_index(); },
            [](const TabViewRef &self, size_t index) {
              if (index >= self.get().page_count()) {
                throw std::out_of_range("TabView selected_index out of range");
              }
              self.get().set_selected_index(index);
              self.widget.throw_pending_exception();
            })
        .def(
            "add_page",
            [](const TabViewRef &self, const std::string &title,
               const WidgetRef &page) {
              if (self.widget.state != page.state) {
                throw std::invalid_argument(
                    "TabView page belongs to another document");
              }
              self.get().add_page(title, page.handle);
              self.widget.throw_pending_exception();
            },
            nb::arg("title"), nb::arg("page"))
        .def(
            "remove_page",
            [](const TabViewRef &self, size_t index) {
              const bool removed = self.get().remove_page(index);
              self.widget.throw_pending_exception();
              return removed;
            },
            nb::arg("index"))
        .def(
            "set_page_title",
            [](const TabViewRef &self, size_t index, const std::string &title) {
              if (!self.get().set_page_title(index, title)) {
                throw std::out_of_range("TabView page index out of range");
              }
            },
            nb::arg("index"), nb::arg("title"))
        .def(
            "page_title",
            [](const TabViewRef &self, size_t index) {
              return self.get().page_title(index);
            },
            nb::arg("index"))
        .def(
            "page_handle",
            [](const TabViewRef &self, size_t index) {
              return WidgetHandle{self.get().page_handle(index)};
            },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const TabViewRef &self, nb::object callback) {
              auto state = self.widget.state;
              return self.get().selection_changed().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::TabView &, size_t index) {
                    try {
                      nb::gil_scoped_acquire gil;
                      callback(index);
                    } catch (...) {
                      if (state && !state->pending_exception) {
                        state->pending_exception = std::current_exception();
                      }
                      tc_log_error("[termin-gui-native/python] TabView "
                                   "selection callback failed");
                    }
                  });
            },
            nb::arg("callback"));

    nb::class_<MenuRef>(m, "Menu")
        .def_prop_ro("widget", [](const MenuRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MenuRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const MenuRef& self) { return self.get().model(); },
            [](const MenuRef& self, std::shared_ptr<termin::gui_native::CommandModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("open", [](const MenuRef& self) { return self.get().open(); })
        .def_prop_ro("current_index", [](const MenuRef& self) -> int64_t {
            const size_t index = self.get().current_index();
            return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
        })
        .def_prop_ro("scroll_offset", [](const MenuRef& self) { return self.get().scroll_offset(); })
        .def_prop_ro("content_height", [](const MenuRef& self) { return self.get().content_height(); })
        .def_prop_rw(
            "max_visible_height", [](const MenuRef& self) { return self.get().max_visible_height(); },
            [](const MenuRef& self, float value) { self.get().set_max_visible_height(value); })
        .def("show", [](const MenuRef& self, tc_ui_point position, tc_ui_rect viewport,
                         bool dismiss_on_outside) {
            return self.get().show(self.widget.state->document, position, viewport,
                                   dismiss_on_outside);
        }, nb::arg("position"), nb::arg("viewport"), nb::arg("dismiss_on_outside") = true)
        .def("dismiss", [](const MenuRef& self, tc_ui_overlay_dismiss_reason reason) {
            return self.get().dismiss(self.widget.state->document, reason);
        }, nb::arg("reason") = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
        .def("connect_activated", [](const MenuRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](termin::gui_native::Menu&, size_t index,
                    termin::gui_native::CommandId id,
                    const termin::gui_native::CommandData& command) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, id, command);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Menu activation callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<termin::gui_native::MenuBarEntry>(m, "MenuBarEntry")
        .def(nb::init<std::string, std::string,
                      std::shared_ptr<termin::gui_native::CommandModel>>(),
             nb::arg("stable_id"), nb::arg("label"), nb::arg("menu"))
        .def_rw("stable_id", &termin::gui_native::MenuBarEntry::stable_id)
        .def_rw("label", &termin::gui_native::MenuBarEntry::label)
        .def_rw("menu", &termin::gui_native::MenuBarEntry::menu);

    nb::class_<MenuBarRef>(m, "MenuBar")
        .def_prop_ro("widget", [](const MenuBarRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MenuBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw("entries", [](const MenuBarRef& self) { return self.get().entries(); },
                     [](const MenuBarRef& self,
                        std::vector<termin::gui_native::MenuBarEntry> entries) {
                         self.get().set_entries(std::move(entries));
                     })
        .def_prop_ro("item_rects", [](const MenuBarRef& self) { return self.get().item_rects(); })
        .def_prop_ro("menu_open", [](const MenuBarRef& self) { return self.get().menu_open(); })
        .def_prop_ro("open_index", [](const MenuBarRef& self) -> int64_t {
            const size_t index = self.get().open_index();
            return index == SIZE_MAX ? -1 : static_cast<int64_t>(index);
        })
        .def("add_menu", [](const MenuBarRef& self, termin::gui_native::MenuBarEntry entry) {
            self.get().add_menu(std::move(entry));
        }, nb::arg("entry"))
        .def("clear", [](const MenuBarRef& self) { self.get().clear(); })
        .def("dispatch_shortcut", [](const MenuBarRef& self, int32_t key, int32_t modifiers) {
            return self.get().dispatch_shortcut(key, modifiers);
        }, nb::arg("key"), nb::arg("modifiers") = 0)
        .def("connect_activated", [](const MenuBarRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().activated().connect(
                [state, callback = std::move(callback)](termin::gui_native::MenuBar&, size_t menu,
                    termin::gui_native::CommandId id,
                    const termin::gui_native::CommandData& command) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(menu, id, command);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] MenuBar activation callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<StatusBarRef>(m, "StatusBar")
        .def_prop_ro("widget", [](const StatusBarRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const StatusBarRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "text", [](const StatusBarRef& self) { return self.get().text(); },
            [](const StatusBarRef& self, std::string value) {
                self.get().set_text(std::move(value));
            })
        .def_prop_ro("message", [](const StatusBarRef& self) { return self.get().message(); })
        .def_prop_ro("has_message",
                     [](const StatusBarRef& self) { return self.get().has_message(); })
        .def_prop_ro("displayed_text",
                     [](const StatusBarRef& self) { return self.get().displayed_text(); })
        .def(
            "show_message",
            [](const StatusBarRef& self, std::string message) {
                self.get().show_message(std::move(message));
            },
            nb::arg("message"))
        .def("clear_message", [](const StatusBarRef& self) { self.get().clear_message(); });

    nb::enum_<termin::gui_native::DialogDismissReason>(m, "DialogDismissReason")
        .value("Action", termin::gui_native::DialogDismissReason::Action)
        .value("Escape", termin::gui_native::DialogDismissReason::Escape)
        .value("Programmatic", termin::gui_native::DialogDismissReason::Programmatic);

    nb::enum_<termin::gui_native::FileDialogMode>(m, "FileDialogMode")
        .value("OpenFile", termin::gui_native::FileDialogMode::OpenFile)
        .value("SaveFile", termin::gui_native::FileDialogMode::SaveFile)
        .value("OpenDirectory", termin::gui_native::FileDialogMode::OpenDirectory);

    nb::class_<termin::gui_native::FileDialogFilter>(m, "FileDialogFilter")
        .def(nb::init<std::string, std::vector<std::string>>(), nb::arg("label"),
             nb::arg("patterns"))
        .def_rw("label", &termin::gui_native::FileDialogFilter::label)
        .def_rw("patterns", &termin::gui_native::FileDialogFilter::patterns);

    nb::class_<termin::gui_native::FileDialogEntry>(m, "FileDialogEntry")
        .def_prop_ro("name", [](const termin::gui_native::FileDialogEntry& self) {
            return self.name;
        })
        .def_prop_ro("path", [](const termin::gui_native::FileDialogEntry& self) {
            return self.path;
        })
        .def_prop_ro("is_directory", [](const termin::gui_native::FileDialogEntry& self) {
            return self.is_directory;
        })
        .def_prop_ro("size", [](const termin::gui_native::FileDialogEntry& self) {
            return self.size;
        })
        .def_prop_ro("modified_time", [](const termin::gui_native::FileDialogEntry& self) {
            return self.modified_time;
        });

    nb::class_<termin::gui_native::FileDialogConfirmResult>(m, "FileDialogConfirmResult")
        .def_prop_ro("path", [](const termin::gui_native::FileDialogConfirmResult& self) {
            return self.path;
        })
        .def_prop_ro("error", [](const termin::gui_native::FileDialogConfirmResult& self) {
            return self.error;
        });

    nb::class_<termin::gui_native::FileDialogModel>(m, "FileDialogModel")
        .def(nb::init<termin::gui_native::FileDialogMode>(), nb::arg("mode"))
        .def_prop_ro("mode", &termin::gui_native::FileDialogModel::mode)
        .def_prop_ro("current_directory",
                     &termin::gui_native::FileDialogModel::current_directory)
        .def_prop_ro("entries", &termin::gui_native::FileDialogModel::entries)
        .def_prop_ro("filters", &termin::gui_native::FileDialogModel::filters)
        .def_prop_ro("selected_filter",
                     &termin::gui_native::FileDialogModel::selected_filter)
        .def_prop_ro("selected_index", &termin::gui_native::FileDialogModel::selected_index)
        .def_prop_rw("file_name", &termin::gui_native::FileDialogModel::file_name,
                     &termin::gui_native::FileDialogModel::set_file_name)
        .def_prop_ro("error", &termin::gui_native::FileDialogModel::error)
        .def_prop_ro("can_go_back", &termin::gui_native::FileDialogModel::can_go_back)
        .def_prop_ro("can_go_forward", &termin::gui_native::FileDialogModel::can_go_forward)
        .def_static("parse_filter_string",
                    &termin::gui_native::FileDialogModel::parse_filter_string,
                    nb::arg("text"))
        .def("set_filters", &termin::gui_native::FileDialogModel::set_filters,
             nb::arg("filters"))
        .def("set_filter", &termin::gui_native::FileDialogModel::set_filter, nb::arg("index"))
        .def("navigate", &termin::gui_native::FileDialogModel::navigate, nb::arg("path"),
             nb::arg("push_history") = true)
        .def("go_back", &termin::gui_native::FileDialogModel::go_back)
        .def("go_forward", &termin::gui_native::FileDialogModel::go_forward)
        .def("go_up", &termin::gui_native::FileDialogModel::go_up)
        .def("refresh", &termin::gui_native::FileDialogModel::refresh)
        .def("select", &termin::gui_native::FileDialogModel::select, nb::arg("index"))
        .def("confirm", &termin::gui_native::FileDialogModel::confirm)
        .def("create_directory", &termin::gui_native::FileDialogModel::create_directory,
             nb::arg("name"));

    nb::class_<termin::gui_native::DialogAction>(m, "DialogAction")
        .def(nb::init<std::string, std::string, bool, bool>(), nb::arg("stable_id"),
             nb::arg("label"), nb::arg("is_default") = false, nb::arg("is_cancel") = false)
        .def_rw("stable_id", &termin::gui_native::DialogAction::stable_id)
        .def_rw("label", &termin::gui_native::DialogAction::label)
        .def_rw("is_default", &termin::gui_native::DialogAction::is_default)
        .def_rw("is_cancel", &termin::gui_native::DialogAction::is_cancel);

    nb::class_<termin::gui_native::DialogResult>(m, "DialogResult")
        .def_prop_ro("action_id", [](const termin::gui_native::DialogResult& self) {
            return self.action_id;
        })
        .def_prop_ro("reason", [](const termin::gui_native::DialogResult& self) {
            return self.reason;
        });

    nb::class_<DialogRef>(m, "Dialog")
        .def_prop_ro("widget", [](const DialogRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const DialogRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw("title", [](const DialogRef& self) { return self.get().title(); },
                     [](const DialogRef& self, std::string title) {
                         self.get().set_title(std::move(title));
                     })
        .def_prop_rw("actions", [](const DialogRef& self) { return self.get().actions(); },
                     [](const DialogRef& self,
                        std::vector<termin::gui_native::DialogAction> actions) {
                         self.get().set_actions(std::move(actions));
                     })
        .def_prop_ro("open", [](const DialogRef& self) { return self.get().open(); })
        .def("set_content", [](const DialogRef& self, const WidgetRef& content) {
            self.get().set_content(
                native_widget_checked<termin::gui_native::NativeWidget>(content, "NativeWidget"));
        }, nb::arg("content"))
        .def("show", [](const DialogRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("close", [](const DialogRef& self) {
            return self.get().close(self.widget.state->document);
        })
        .def("activate", [](const DialogRef& self, const std::string& action_id) {
            return self.get().activate(action_id, self.widget.state->document);
        }, nb::arg("action_id"))
        .def("connect_finished", [](const DialogRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::Dialog&,
                    const termin::gui_native::DialogResult& result) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(result);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] Dialog finished callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::enum_<termin::gui_native::MessageBoxKind>(m, "MessageBoxKind")
        .value("Information", termin::gui_native::MessageBoxKind::Information)
        .value("Warning", termin::gui_native::MessageBoxKind::Warning)
        .value("Error", termin::gui_native::MessageBoxKind::Error)
        .value("Question", termin::gui_native::MessageBoxKind::Question);

    nb::class_<MessageBoxRef>(m, "MessageBox")
        .def_prop_ro("widget", [](const MessageBoxRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const MessageBoxRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("message", [](const MessageBoxRef& self) { return self.get().message(); })
        .def_prop_ro("kind", [](const MessageBoxRef& self) { return self.get().kind(); })
        .def_prop_ro("open", [](const MessageBoxRef& self) { return self.get().open(); })
        .def("show", [](const MessageBoxRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("connect_finished", [](const MessageBoxRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::Dialog&,
                    const termin::gui_native::DialogResult& result) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(result);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] MessageBox callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<InputDialogRef>(m, "InputDialog")
        .def_prop_ro("widget", [](const InputDialogRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const InputDialogRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("message", [](const InputDialogRef& self) { return self.get().message(); })
        .def_prop_rw("value", [](const InputDialogRef& self) { return self.get().value(); },
                     [](const InputDialogRef& self, std::string value) {
                         self.get().set_value(std::move(value));
                     })
        .def_prop_ro("open", [](const InputDialogRef& self) { return self.get().open(); })
        .def("show", [](const InputDialogRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("connect_value_finished", [](const InputDialogRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().value_finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::InputDialog&,
                    const std::optional<std::string>& value) {
                    try {
                        nb::gil_scoped_acquire gil;
                        if (value)
                            callback(*value);
                        else
                            callback(nb::none());
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] InputDialog callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<FileDialogOverlayRef>(m, "FileDialogOverlay")
        .def_prop_ro("widget", [](const FileDialogOverlayRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const FileDialogOverlayRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_ro("model", [](const FileDialogOverlayRef& self) ->
                     termin::gui_native::FileDialogModel& { return self.get().model(); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("open", [](const FileDialogOverlayRef& self) { return self.get().open(); })
        .def("set_filters", [](const FileDialogOverlayRef& self,
                               std::vector<termin::gui_native::FileDialogFilter> filters) {
            self.get().set_filters(std::move(filters));
        }, nb::arg("filters"))
        .def("set_initial_directory", [](const FileDialogOverlayRef& self, std::string directory) {
            self.get().set_initial_directory(std::move(directory));
        }, nb::arg("directory"))
        .def("set_file_name", [](const FileDialogOverlayRef& self, std::string file_name) {
            self.get().set_file_name(std::move(file_name));
        }, nb::arg("file_name"))
        .def("show", [](const FileDialogOverlayRef& self, tc_ui_rect viewport) {
            return self.get().show(self.widget.state->document, viewport);
        }, nb::arg("viewport"))
        .def("activate", [](const FileDialogOverlayRef& self, const std::string& action_id) {
            return self.get().activate(action_id, self.widget.state->document);
        }, nb::arg("action_id"))
        .def("connect_path_finished", [](const FileDialogOverlayRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().path_finished().connect(
                [state, callback = std::move(callback)](termin::gui_native::FileDialogOverlay&,
                    const std::optional<std::string>& path) {
                    try {
                        nb::gil_scoped_acquire gil;
                        if (path)
                            callback(*path);
                        else
                            callback(nb::none());
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] FileDialogOverlay callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::enum_<termin::gui_native::ColorPickerSurfaceKind>(m, "ColorPickerSurfaceKind")
        .value("SaturationValue", termin::gui_native::ColorPickerSurfaceKind::SaturationValue)
        .value("Hue", termin::gui_native::ColorPickerSurfaceKind::Hue)
        .value("Alpha", termin::gui_native::ColorPickerSurfaceKind::Alpha);

    nb::class_<termin::gui_native::ColorPickerSurface>(m, "ColorPickerSurface")
        .def_prop_ro("width", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.width;
        })
        .def_prop_ro("height", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.height;
        })
        .def_prop_ro("revision", [](const termin::gui_native::ColorPickerSurface& self) {
            return self.revision;
        })
        .def_prop_ro("rgba", [](const termin::gui_native::ColorPickerSurface& self) {
            return nb::bytes(reinterpret_cast<const char*>(self.rgba.data()), self.rgba.size());
        });

    nb::class_<termin::gui_native::ColorPickerTextureIds>(m, "ColorPickerTextureIds")
        .def(nb::init<uint32_t, uint32_t, uint32_t>(), nb::arg("saturation_value") = 0,
             nb::arg("hue") = 0, nb::arg("alpha") = 0)
        .def_rw("saturation_value",
                &termin::gui_native::ColorPickerTextureIds::saturation_value)
        .def_rw("hue", &termin::gui_native::ColorPickerTextureIds::hue)
        .def_rw("alpha", &termin::gui_native::ColorPickerTextureIds::alpha);

    nb::class_<termin::gui_native::ColorPickerModel>(m, "ColorPickerModel")
        .def("__init__", [](termin::gui_native::ColorPickerModel* self, tc_ui_color initial,
                            bool show_alpha) {
            new (self) termin::gui_native::ColorPickerModel(
                termin::gui_native::Color{initial.r, initial.g, initial.b, initial.a}, show_alpha);
        }, nb::arg("initial") = tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
           nb::arg("show_alpha") = true)
        .def_prop_rw("color", [](const termin::gui_native::ColorPickerModel& self) {
            return self.color().c_color();
        }, [](termin::gui_native::ColorPickerModel& self, tc_ui_color color) {
            self.set_color(termin::gui_native::Color{color.r, color.g, color.b, color.a});
        })
        .def_prop_ro("initial_color", [](const termin::gui_native::ColorPickerModel& self) {
            return self.initial_color().c_color();
        })
        .def_prop_rw("hue", &termin::gui_native::ColorPickerModel::hue,
                     &termin::gui_native::ColorPickerModel::set_hue)
        .def_prop_rw("saturation", &termin::gui_native::ColorPickerModel::saturation,
                     &termin::gui_native::ColorPickerModel::set_saturation)
        .def_prop_rw("value", &termin::gui_native::ColorPickerModel::value,
                     &termin::gui_native::ColorPickerModel::set_value)
        .def_prop_rw("alpha", &termin::gui_native::ColorPickerModel::alpha,
                     &termin::gui_native::ColorPickerModel::set_alpha)
        .def_prop_rw("show_alpha", &termin::gui_native::ColorPickerModel::show_alpha,
                     &termin::gui_native::ColorPickerModel::set_show_alpha)
        .def_prop_ro("revision", &termin::gui_native::ColorPickerModel::revision)
        .def("set_hsv", &termin::gui_native::ColorPickerModel::set_hsv, nb::arg("hue"),
             nb::arg("saturation"), nb::arg("value"));

    nb::class_<ColorPickerRef>(m, "ColorPicker")
        .def_prop_ro("widget", [](const ColorPickerRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const ColorPickerRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw("model", [](const ColorPickerRef& self) { return self.get().model(); },
                     [](const ColorPickerRef& self,
                        std::shared_ptr<termin::gui_native::ColorPickerModel> model) {
                         self.get().set_model(std::move(model));
                     })
        .def_prop_rw("texture_ids", [](const ColorPickerRef& self) {
            return self.get().texture_ids();
        }, [](const ColorPickerRef& self, termin::gui_native::ColorPickerTextureIds ids) {
            self.get().set_texture_ids(ids);
        })
        .def("surface", [](const ColorPickerRef& self,
                           termin::gui_native::ColorPickerSurfaceKind kind) ->
             const termin::gui_native::ColorPickerSurface& { return self.get().surface(kind); },
             nb::arg("kind"), nb::rv_policy::reference_internal)
        .def("connect_surfaces_invalidated", [](const ColorPickerRef& self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().surfaces_invalidated().connect(
                [state, callback = std::move(callback)](termin::gui_native::ColorPicker&,
                                                        uint32_t flags) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(flags);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error(
                            "[termin-gui-native/python] ColorPicker surface callback failed");
                    }
                });
        }, nb::arg("callback"));

    nb::class_<ColorDialogRef>(m, "ColorDialog")
        .def_prop_ro("widget", [](const ColorDialogRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ColorDialogRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("model", [](const ColorDialogRef& self) { return self.get().model(); })
        .def_prop_rw(
            "color", [](const ColorDialogRef& self) { return self.get().color().c_color(); },
            [](const ColorDialogRef& self, tc_ui_color color) {
                self.get().set_color(termin::gui_native::Color{color.r, color.g, color.b, color.a});
            })
        .def_prop_ro("open", [](const ColorDialogRef& self) { return self.get().open(); })
        .def(
            "show",
            [](const ColorDialogRef& self, tc_ui_rect viewport) {
                return self.get().show(self.widget.state->document, viewport);
            },
            nb::arg("viewport"))
        .def(
            "activate",
            [](const ColorDialogRef& self, const std::string& action_id) {
                return self.get().activate(action_id, self.widget.state->document);
            },
            nb::arg("action_id"))
        .def(
            "connect_color_finished",
            [](const ColorDialogRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().color_finished().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ColorDialog&,
                        const std::optional<termin::gui_native::Color>& color) {
                        try {
                            nb::gil_scoped_acquire gil;
                            if (color)
                                callback(color->c_color());
                            else
                                callback(nb::none());
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] ColorDialog callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::SceneTransform>(m, "SceneTransform")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::SceneTransform* self, float origin_x, float origin_y,
               float zoom) {
                new (self) termin::gui_native::SceneTransform{origin_x, origin_y, zoom};
            },
            nb::arg("origin_x"), nb::arg("origin_y"), nb::arg("zoom"))
        .def_rw("origin_x", &termin::gui_native::SceneTransform::origin_x)
        .def_rw("origin_y", &termin::gui_native::SceneTransform::origin_y)
        .def_rw("zoom", &termin::gui_native::SceneTransform::zoom)
        .def("world_to_screen", &termin::gui_native::SceneTransform::world_to_screen,
             nb::arg("point"))
        .def("screen_to_world", &termin::gui_native::SceneTransform::screen_to_world,
             nb::arg("point"));

    nb::class_<termin::gui_native::GraphicsItem>(m, "GraphicsItem")
        .def(nb::init<std::string>(), nb::arg("stable_id") = "")
        .def_prop_ro("id", &termin::gui_native::GraphicsItem::id)
        .def_prop_rw("stable_id", &termin::gui_native::GraphicsItem::stable_id,
                     &termin::gui_native::GraphicsItem::set_stable_id)
        .def_prop_ro("parent", &termin::gui_native::GraphicsItem::parent)
        .def_prop_ro("children",
                     [](const termin::gui_native::GraphicsItem& self) { return self.children(); })
        .def_prop_rw("position", &termin::gui_native::GraphicsItem::position,
                     &termin::gui_native::GraphicsItem::set_position)
        .def_prop_rw("size", &termin::gui_native::GraphicsItem::size,
                     &termin::gui_native::GraphicsItem::set_size)
        .def_prop_rw("z_index", &termin::gui_native::GraphicsItem::z_index,
                     &termin::gui_native::GraphicsItem::set_z_index)
        .def_prop_rw("visible", &termin::gui_native::GraphicsItem::visible,
                     &termin::gui_native::GraphicsItem::set_visible)
        .def_prop_rw("enabled", &termin::gui_native::GraphicsItem::enabled,
                     &termin::gui_native::GraphicsItem::set_enabled)
        .def_prop_rw("selectable", &termin::gui_native::GraphicsItem::selectable,
                     &termin::gui_native::GraphicsItem::set_selectable)
        .def_prop_rw("draggable", &termin::gui_native::GraphicsItem::draggable,
                     &termin::gui_native::GraphicsItem::set_draggable)
        .def_prop_ro("selected", &termin::gui_native::GraphicsItem::selected)
        .def_prop_ro("hovered", &termin::gui_native::GraphicsItem::hovered)
        .def_prop_ro("world_position", &termin::gui_native::GraphicsItem::world_position)
        .def_prop_ro("world_bounds", &termin::gui_native::GraphicsItem::world_bounds)
        .def_prop_rw(
            "embedded_widget",
            [](const termin::gui_native::GraphicsItem& self) {
                return WidgetHandle{self.embedded_widget()};
            },
            [](termin::gui_native::GraphicsItem& self, WidgetHandle handle) {
                self.set_embedded_widget(handle.handle);
            })
        .def("add_child", &termin::gui_native::GraphicsItem::add_child, nb::arg("child"))
        .def("remove_child", &termin::gui_native::GraphicsItem::remove_child, nb::arg("child"))
        .def("clear_children", &termin::gui_native::GraphicsItem::clear_children)
        .def("contains_local", &termin::gui_native::GraphicsItem::contains_local, nb::arg("x"),
             nb::arg("y"))
        .def("hit_test", &termin::gui_native::GraphicsItem::hit_test, nb::arg("world_x"),
             nb::arg("world_y"))
        .def("clear_embedded_widget", &termin::gui_native::GraphicsItem::clear_embedded_widget)
        .def(
            "set_paint_callback",
            [](termin::gui_native::GraphicsItem& self, nb::object callback) {
                if (callback.is_none()) {
                    self.set_paint_callback({});
                    return;
                }
                self.set_paint_callback([callback = std::move(callback)](
                                            termin::gui_native::GraphicsItem& item,
                                            tc_ui_paint_context* context,
                                            const termin::gui_native::SceneTransform& transform) {
                    nb::gil_scoped_acquire gil;
                    try {
                        PaintContext borrowed(context, false);
                        callback(item.shared_from_this(), std::move(borrowed),
                                 termin::gui_native::SceneTransform{transform});
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] GraphicsItem paint callback failed");
                        throw;
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "set_hit_test_callback",
            [](termin::gui_native::GraphicsItem& self, nb::object callback) {
                if (callback.is_none()) {
                    self.set_hit_test_callback({});
                    return;
                }
                self.set_hit_test_callback([callback = std::move(callback)](
                                               const termin::gui_native::GraphicsItem& item,
                                               float x, float y) {
                    nb::gil_scoped_acquire gil;
                    try {
                        return nb::cast<bool>(callback(
                            const_cast<termin::gui_native::GraphicsItem&>(item).shared_from_this(),
                            x, y));
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] GraphicsItem hit-test callback failed");
                        throw;
                    }
                });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::GraphicsScene>(m, "GraphicsScene")
        .def(nb::init<>())
        .def_prop_ro("items",
                     [](const termin::gui_native::GraphicsScene& self) { return self.items(); })
        .def_prop_ro(
            "selected_items",
            [](const termin::gui_native::GraphicsScene& self) { return self.selected_items(); })
        .def_prop_ro("revision", &termin::gui_native::GraphicsScene::revision)
        .def("add_item", &termin::gui_native::GraphicsScene::add_item, nb::arg("item"))
        .def("remove_item", &termin::gui_native::GraphicsScene::remove_item, nb::arg("item"))
        .def("clear", &termin::gui_native::GraphicsScene::clear)
        .def("hit_test", &termin::gui_native::GraphicsScene::hit_test, nb::arg("world_x"),
             nb::arg("world_y"))
        .def("set_selected", &termin::gui_native::GraphicsScene::set_selected, nb::arg("item"))
        .def("toggle_selected", &termin::gui_native::GraphicsScene::toggle_selected,
             nb::arg("item"))
        .def("clear_selection", &termin::gui_native::GraphicsScene::clear_selection)
        .def(
            "contains",
            [](const termin::gui_native::GraphicsScene& self,
               const std::shared_ptr<termin::gui_native::GraphicsItem>& item) {
                return self.contains(item.get());
            },
            nb::arg("item"))
        .def(
            "connect_selection_changed",
            [](termin::gui_native::GraphicsScene& self, nb::object callback) {
                return self.selection_changed().connect(
                    [callback = std::move(callback)](termin::gui_native::GraphicsScene&,
                                                     const auto& selected) {
                        nb::gil_scoped_acquire gil;
                        try {
                            callback(selected);
                        } catch (...) {
                            tc_log_error("[termin-gui-native/python] GraphicsScene selection "
                                         "callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::CollectionItem>(m, "CollectionItem")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::CollectionItem* self, std::string stable_id, std::string text,
               std::string subtitle, bool enabled, uint32_t texture_id) {
                new (self)
                    termin::gui_native::CollectionItem{std::move(stable_id), std::move(text),
                                                       std::move(subtitle), enabled, texture_id};
            },
            nb::arg("stable_id"), nb::arg("text"), nb::arg("subtitle") = "",
            nb::arg("enabled") = true, nb::arg("texture_id") = 0)
        .def_rw("stable_id", &termin::gui_native::CollectionItem::stable_id)
        .def_rw("text", &termin::gui_native::CollectionItem::text)
        .def_rw("subtitle", &termin::gui_native::CollectionItem::subtitle)
        .def_rw("enabled", &termin::gui_native::CollectionItem::enabled)
        .def_rw("texture_id", &termin::gui_native::CollectionItem::texture_id);

    nb::class_<termin::gui_native::CollectionModel>(m, "CollectionModel")
        .def(nb::init<>())
        .def_prop_ro("item_count", &termin::gui_native::CollectionModel::size)
        .def_prop_ro("revision", &termin::gui_native::CollectionModel::revision)
        .def_prop_ro("items",
                     [](const termin::gui_native::CollectionModel& self) { return self.items(); })
        .def(
            "item",
            [](const termin::gui_native::CollectionModel& self, size_t index) {
                return self.item(index);
            },
            nb::arg("index"))
        .def("set_items", &termin::gui_native::CollectionModel::set_items, nb::arg("items"))
        .def("append", &termin::gui_native::CollectionModel::append, nb::arg("item"))
        .def("update", &termin::gui_native::CollectionModel::update, nb::arg("index"),
             nb::arg("item"))
        .def("erase", &termin::gui_native::CollectionModel::erase, nb::arg("index"))
        .def("clear", &termin::gui_native::CollectionModel::clear);

    nb::class_<termin::gui_native::RichTextStyle>(m, "RichTextStyle")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextStyle* self, std::optional<tc_ui_color> color,
               bool bold, bool italic) {
                new (self) termin::gui_native::RichTextStyle{std::move(color), bold, italic};
            },
            nb::arg("color") = nb::none(), nb::arg("bold") = false,
            nb::arg("italic") = false)
        .def_rw("color", &termin::gui_native::RichTextStyle::color)
        .def_rw("bold", &termin::gui_native::RichTextStyle::bold)
        .def_rw("italic", &termin::gui_native::RichTextStyle::italic);

    nb::class_<termin::gui_native::RichTextSegment>(m, "RichTextSegment")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::RichTextSegment* self, std::string text,
               termin::gui_native::RichTextStyle style) {
                new (self)
                    termin::gui_native::RichTextSegment{std::move(text), std::move(style)};
            },
            nb::arg("text"), nb::arg("style") = termin::gui_native::RichTextStyle{})
        .def_rw("text", &termin::gui_native::RichTextSegment::text)
        .def_rw("style", &termin::gui_native::RichTextSegment::style);

    nb::class_<termin::gui_native::RichTextModel>(m, "RichTextModel")
        .def(nb::init<>())
        .def_prop_ro("revision", &termin::gui_native::RichTextModel::revision)
        .def_prop_ro("text", &termin::gui_native::RichTextModel::text)
        .def_prop_ro("lines", [](const termin::gui_native::RichTextModel& self) {
            return self.lines();
        })
        .def("set_text", &termin::gui_native::RichTextModel::set_text, nb::arg("text"))
        .def("set_lines", &termin::gui_native::RichTextModel::set_lines, nb::arg("lines"))
        .def("set_html", &termin::gui_native::RichTextModel::set_html, nb::arg("html"))
        .def("clear", &termin::gui_native::RichTextModel::clear);

    nb::class_<RichTextViewRef>(m, "RichTextView")
        .def_prop_ro("widget", [](const RichTextViewRef& self) { return self.widget; })
        .def_prop_ro("handle", [](const RichTextViewRef& self) {
            return WidgetHandle{self.widget.handle};
        })
        .def_prop_rw(
            "model", [](const RichTextViewRef& self) { return self.get().model(); },
            [](const RichTextViewRef& self,
               std::shared_ptr<termin::gui_native::RichTextModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "placeholder", [](const RichTextViewRef& self) { return self.get().placeholder(); },
            [](const RichTextViewRef& self, const std::string& value) {
                self.get().set_placeholder(value);
            })
        .def_prop_rw(
            "word_wrap", [](const RichTextViewRef& self) { return self.get().word_wrap(); },
            [](const RichTextViewRef& self, bool value) { self.get().set_word_wrap(value); })
        .def_prop_rw(
            "show_scrollbar",
            [](const RichTextViewRef& self) { return self.get().show_scrollbar(); },
            [](const RichTextViewRef& self, bool value) {
                self.get().set_show_scrollbar(value);
            })
        .def_prop_rw(
            "line_height", [](const RichTextViewRef& self) { return self.get().line_height(); },
            [](const RichTextViewRef& self, float value) { self.get().set_line_height(value); })
        .def_prop_rw(
            "scroll_y", [](const RichTextViewRef& self) { return self.get().scroll_y(); },
            [](const RichTextViewRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const RichTextViewRef& self) { return self.get().content_height(); })
        .def_prop_ro("visual_line_count",
                     [](const RichTextViewRef& self) { return self.get().visual_line_count(); })
        .def_prop_ro("has_selection",
                     [](const RichTextViewRef& self) { return self.get().has_selection(); })
        .def_prop_ro("selected_text",
                     [](const RichTextViewRef& self) { return self.get().selected_text(); })
        .def(
            "select",
            [](const RichTextViewRef& self, size_t anchor, size_t cursor) {
                self.get().select(anchor, cursor);
            },
            nb::arg("anchor"), nb::arg("cursor"))
        .def("select_all", [](const RichTextViewRef& self) { self.get().select_all(); })
        .def("clear_selection",
             [](const RichTextViewRef& self) { self.get().clear_selection(); });

    nb::class_<termin::gui_native::FrameTimeModel>(m, "FrameTimeModel")
        .def(nb::init<>())
        .def_prop_rw(
            "max_samples",
            [](const termin::gui_native::FrameTimeModel& self) { return self.max_samples(); },
            &termin::gui_native::FrameTimeModel::set_max_samples)
        .def_prop_ro("samples",
                     [](const termin::gui_native::FrameTimeModel& self) { return self.samples(); })
        .def_prop_ro("revision", &termin::gui_native::FrameTimeModel::revision)
        .def("add_sample", &termin::gui_native::FrameTimeModel::add_sample, nb::arg("milliseconds"))
        .def("set_samples", &termin::gui_native::FrameTimeModel::set_samples, nb::arg("samples"))
        .def("clear", &termin::gui_native::FrameTimeModel::clear);

    nb::class_<FrameTimeGraphRef>(m, "FrameTimeGraph")
        .def_prop_ro("widget", [](const FrameTimeGraphRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const FrameTimeGraphRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const FrameTimeGraphRef& self) { return self.get().model(); },
            [](const FrameTimeGraphRef& self,
               std::shared_ptr<termin::gui_native::FrameTimeModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_ro("target_frame_ms",
                     [](const FrameTimeGraphRef& self) { return self.get().target_frame_ms(); })
        .def_prop_ro("warning_frame_ms",
                     [](const FrameTimeGraphRef& self) { return self.get().warning_frame_ms(); })
        .def(
            "set_thresholds",
            [](const FrameTimeGraphRef& self, float target, float warning) {
                self.get().set_thresholds(target, warning);
            },
            nb::arg("target_frame_ms"), nb::arg("warning_frame_ms"));

    nb::class_<termin::gui_native::ViewportSurfaceSize>(m, "ViewportSurfaceSize")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::ViewportSurfaceSize* self, int width, int height) {
                new (self) termin::gui_native::ViewportSurfaceSize{width, height};
            },
            nb::arg("width"), nb::arg("height"))
        .def_rw("width", &termin::gui_native::ViewportSurfaceSize::width)
        .def_rw("height", &termin::gui_native::ViewportSurfaceSize::height);

    nb::enum_<termin::gui_native::ViewportExternalDragPhase>(m, "ViewportExternalDragPhase")
        .value("Enter", termin::gui_native::ViewportExternalDragPhase::Enter)
        .value("Move", termin::gui_native::ViewportExternalDragPhase::Move)
        .value("Leave", termin::gui_native::ViewportExternalDragPhase::Leave)
        .value("Drop", termin::gui_native::ViewportExternalDragPhase::Drop);

    nb::class_<termin::gui_native::ViewportExternalDragEvent>(m, "ViewportExternalDragEvent")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::ViewportExternalDragEvent* self,
               termin::gui_native::ViewportExternalDragPhase phase, std::string mime_type,
               std::string payload, float x, float y) {
                new (self) termin::gui_native::ViewportExternalDragEvent{
                    phase, std::move(mime_type), std::move(payload), x, y};
            },
            nb::arg("phase"), nb::arg("mime_type"), nb::arg("payload"), nb::arg("x"), nb::arg("y"))
        .def_rw("phase", &termin::gui_native::ViewportExternalDragEvent::phase)
        .def_rw("mime_type", &termin::gui_native::ViewportExternalDragEvent::mime_type)
        .def_rw("payload", &termin::gui_native::ViewportExternalDragEvent::payload)
        .def_rw("x", &termin::gui_native::ViewportExternalDragEvent::x)
        .def_rw("y", &termin::gui_native::ViewportExternalDragEvent::y);

    nb::class_<Viewport3DRef>(m, "Viewport3D")
        .def_prop_ro("widget", [](const Viewport3DRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const Viewport3DRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("has_surface",
                     [](const Viewport3DRef& self) { return self.get().has_surface(); })
        .def_prop_ro("surface_valid",
                     [](const Viewport3DRef& self) {
                         const bool valid = self.get().surface_valid();
                         self.widget.throw_pending_exception();
                         return valid;
                     })
        .def_prop_ro("texture_id",
                     [](const Viewport3DRef& self) {
                         const uint32_t texture = self.get().texture_id();
                         self.widget.throw_pending_exception();
                         return texture;
                     })
        .def_prop_ro("surface_size",
                     [](const Viewport3DRef& self) {
                         const auto size = self.get().surface_size();
                         self.widget.throw_pending_exception();
                         return size;
                     })
        .def(
            "set_surface_host",
            [](const Viewport3DRef& self, nb::object host) {
                if (host.is_none()) {
                    self.get().detach_surface();
                    return;
                }
                self.get().set_surface_host(std::make_shared<PythonViewportSurfaceHost>(
                    std::move(host), self.widget.state));
                self.widget.throw_pending_exception();
            },
            nb::arg("host"))
        .def("detach_surface", [](const Viewport3DRef& self) { self.get().detach_surface(); })
        .def(
            "connect_before_resize",
            [](const Viewport3DRef& self, nb::object callback) {
                const std::shared_ptr<DocumentState> state = self.widget.state;
                return self.get().before_resize().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::Viewport3D&,
                        termin::gui_native::ViewportSurfaceSize previous,
                        termin::gui_native::ViewportSurfaceSize next) {
                        if (!state || !state->document)
                            return;
                        nb::gil_scoped_acquire gil;
                        try {
                            callback(previous, next);
                        } catch (...) {
                            tc_log_error("[termin-gui-native/python] Viewport3D before-resize "
                                         "callback failed");
                            if (!state->pending_exception)
                                state->pending_exception = std::current_exception();
                            throw;
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "set_external_drag_handler",
            [](const Viewport3DRef& self, nb::object callback) {
                if (callback.is_none()) {
                    self.get().set_external_drag_handler({});
                    return;
                }
                const std::shared_ptr<DocumentState> state = self.widget.state;
                self.get().set_external_drag_handler([state, callback = std::move(callback)](
                                                         const termin::gui_native::
                                                             ViewportExternalDragEvent& event) {
                    if (!state || !state->document)
                        return false;
                    nb::gil_scoped_acquire gil;
                    try {
                        return nb::cast<bool>(callback(event));
                    } catch (...) {
                        tc_log_error(
                            "[termin-gui-native/python] Viewport3D external drag callback failed");
                        if (!state->pending_exception)
                            state->pending_exception = std::current_exception();
                        return false;
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "dispatch_external_drag",
            [](const Viewport3DRef& self,
               const termin::gui_native::ViewportExternalDragEvent& event) {
                const bool handled = self.get().dispatch_external_drag(event);
                self.widget.throw_pending_exception();
                return handled;
            },
            nb::arg("event"));

    nb::class_<SceneViewRef>(m, "SceneView")
        .def_prop_ro("widget",
                     [](const SceneViewRef &self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const SceneViewRef &self) {
                       return WidgetHandle{self.widget.handle};
                     })
        .def_prop_rw(
            "scene",
            [](const SceneViewRef &self) { return self.get().scene(); },
            [](const SceneViewRef &self,
               std::shared_ptr<termin::gui_native::GraphicsScene> scene) {
              self.get().set_scene(std::move(scene));
            })
        .def_prop_ro(
            "transform",
            [](const SceneViewRef &self) { return self.get().transform(); })
        .def_prop_rw(
            "offset",
            [](const SceneViewRef &self) { return self.get().offset(); },
            [](const SceneViewRef &self, tc_ui_point offset) {
              self.get().set_offset(offset);
              self.widget.throw_pending_exception();
            })
        .def_prop_ro("zoom",
                     [](const SceneViewRef &self) { return self.get().zoom(); })
        .def_prop_ro(
            "min_zoom",
            [](const SceneViewRef &self) { return self.get().min_zoom(); })
        .def_prop_ro(
            "max_zoom",
            [](const SceneViewRef &self) { return self.get().max_zoom(); })
        .def_prop_rw(
            "zoom_factor",
            [](const SceneViewRef &self) { return self.get().zoom_factor(); },
            [](const SceneViewRef &self, float factor) {
              self.get().set_zoom_factor(factor);
            })
        .def_prop_rw(
            "show_grid",
            [](const SceneViewRef &self) { return self.get().show_grid(); },
            [](const SceneViewRef &self, bool show) {
              self.get().set_show_grid(show);
            })
        .def_prop_rw(
            "grid_step",
            [](const SceneViewRef &self) { return self.get().grid_step(); },
            [](const SceneViewRef &self, float step) {
              self.get().set_grid_step(step);
            })
        .def(
            "set_zoom",
            [](const SceneViewRef &self, float zoom, tc_ui_point anchor) {
              self.get().set_zoom(zoom, anchor);
              self.widget.throw_pending_exception();
            },
            nb::arg("zoom"), nb::arg("anchor"))
        .def(
            "set_zoom_range",
            [](const SceneViewRef &self, float minimum, float maximum) {
              self.get().set_zoom_range(minimum, maximum);
              self.widget.throw_pending_exception();
            },
            nb::arg("minimum"), nb::arg("maximum"))
        .def(
            "set_scene_colors",
            [](const SceneViewRef &self, tc_ui_color background,
               tc_ui_color grid, tc_ui_color axes) {
              self.get().set_scene_colors(
                  termin::gui_native::Color{background.r, background.g,
                                            background.b, background.a},
                  termin::gui_native::Color{grid.r, grid.g, grid.b, grid.a},
                  termin::gui_native::Color{axes.r, axes.g, axes.b, axes.a});
            },
            nb::arg("background"), nb::arg("grid"), nb::arg("axes"))
        .def(
            "world_to_screen",
            [](const SceneViewRef &self, tc_ui_point point) {
              return self.get().world_to_screen(point);
            },
            nb::arg("point"))
        .def(
            "screen_to_world",
            [](const SceneViewRef &self, tc_ui_point point) {
              return self.get().screen_to_world(point);
            },
            nb::arg("point"))
        .def(
            "set_pointer_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_pointer_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_pointer_handler(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &, tc_ui_point world,
                      const tc_ui_pointer_event &event) {
                    nb::gil_scoped_acquire gil;
                    try {
                      return nb::cast<bool>(callback(world, event));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "pointer handler failed");
                      throw;
                    }
                  });
            },
            nb::arg("callback").none())
        .def(
            "set_key_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_key_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_key_handler(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      const tc_ui_key_event &event) {
                    nb::gil_scoped_acquire gil;
                    try {
                      return nb::cast<bool>(callback(event));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView key "
                                   "handler failed");
                      throw;
                    }
                  });
            },
            nb::arg("callback").none())
        .def(
            "set_text_handler",
            [](const SceneViewRef &self, nb::object callback) {
              if (callback.is_none()) {
                self.get().set_text_handler({});
                return;
              }
              const auto state = self.widget.state;
              self.get().set_text_handler([state,
                                           callback = std::move(callback)](
                                              termin::gui_native::SceneView &,
                                              const tc_ui_text_event &event) {
                nb::gil_scoped_acquire gil;
                try {
                  return nb::cast<bool>(callback(event.text ? event.text : ""));
                } catch (...) {
                  if (state && !state->pending_exception)
                    state->pending_exception = std::current_exception();
                  tc_log_error("[termin-gui-native/python] SceneView text "
                               "handler failed");
                  throw;
                }
              });
            },
            nb::arg("callback").none())
        .def(
            "connect_item_moved",
            [](const SceneViewRef &self, nb::object callback) {
              const auto state = self.widget.state;
              return self.get().item_moved().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      std::shared_ptr<termin::gui_native::GraphicsItem> item) {
                    nb::gil_scoped_acquire gil;
                    try {
                      callback(std::move(item));
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "item-moved callback failed");
                    }
                  });
            },
            nb::arg("callback"))
        .def(
            "connect_transform_changed",
            [](const SceneViewRef &self, nb::object callback) {
              const auto state = self.widget.state;
              return self.get().transform_changed().connect(
                  [state, callback = std::move(callback)](
                      termin::gui_native::SceneView &,
                      const termin::gui_native::SceneTransform &transform) {
                    nb::gil_scoped_acquire gil;
                    try {
                      callback(termin::gui_native::SceneTransform{transform});
                    } catch (...) {
                      if (state && !state->pending_exception)
                        state->pending_exception = std::current_exception();
                      tc_log_error("[termin-gui-native/python] SceneView "
                                   "transform callback "
                                   "failed");
                    }
                  });
            },
            nb::arg("callback"));

    nb::enum_<termin::gui_native::SelectionMode>(m, "SelectionMode")
        .value("Single", termin::gui_native::SelectionMode::Single)
        .value("Multiple", termin::gui_native::SelectionMode::Multiple);

    nb::class_<ListWidgetRef>(m, "ListWidget")
        .def_prop_ro("widget", [](const ListWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ListWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const ListWidgetRef& self) { return self.get().model(); },
            [](const ListWidgetRef& self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "selection_mode",
            [](const ListWidgetRef& self) { return self.get().selection().mode(); },
            [](const ListWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
            })
        .def_prop_ro(
            "selected_indices",
            [](const ListWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const ListWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_rw(
            "scroll_y", [](const ListWidgetRef& self) { return self.get().scroll_y(); },
            [](const ListWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const ListWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_range",
                     [](const ListWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def(
            "set_row_height",
            [](const ListWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_row_spacing",
            [](const ListWidgetRef& self, float spacing) { self.get().set_row_spacing(spacing); },
            nb::arg("spacing"))
        .def(
            "select",
            [](const ListWidgetRef& self, size_t index, bool toggle, bool extend, bool additive) {
                const bool changed = self.get().select_index(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const ListWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const ListWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const ListWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::ListWidget&,
                                                            const std::vector<size_t>& selected) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(selected);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] ListWidget selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const ListWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::ListWidget&, size_t index,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, item);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] ListWidget activation callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<FileGridWidgetRef>(m, "FileGridWidget")
        .def_prop_ro("widget", [](const FileGridWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const FileGridWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const FileGridWidgetRef& self) { return self.get().model(); },
            [](const FileGridWidgetRef& self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "selection_mode",
            [](const FileGridWidgetRef& self) { return self.get().selection().mode(); },
            [](const FileGridWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro(
            "selected_indices",
            [](const FileGridWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const FileGridWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_ro("column_count",
                     [](const FileGridWidgetRef& self) { return self.get().column_count(); })
        .def_prop_ro("row_count",
                     [](const FileGridWidgetRef& self) { return self.get().row_count(); })
        .def_prop_rw(
            "scroll_y", [](const FileGridWidgetRef& self) { return self.get().scroll_y(); },
            [](const FileGridWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const FileGridWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("has_scrollbar",
                     [](const FileGridWidgetRef& self) { return self.get().has_scrollbar(); })
        .def_prop_ro(
            "scrollbar_thumb_rect",
            [](const FileGridWidgetRef& self) { return self.get().scrollbar_thumb_rect(); })
        .def_prop_ro("visible_range",
                     [](const FileGridWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def_prop_rw(
            "show_scrollbar",
            [](const FileGridWidgetRef& self) { return self.get().show_scrollbar(); },
            [](const FileGridWidgetRef& self, bool value) { self.get().set_show_scrollbar(value); })
        .def_prop_rw(
            "empty_text", [](const FileGridWidgetRef& self) { return self.get().empty_text(); },
            [](const FileGridWidgetRef& self, std::string value) {
                self.get().set_empty_text(std::move(value));
            })
        .def(
            "set_tile_size",
            [](const FileGridWidgetRef& self, float width, float height) {
                self.get().set_tile_size(width, height);
            },
            nb::arg("width"), nb::arg("height"))
        .def(
            "set_tile_spacing",
            [](const FileGridWidgetRef& self, float spacing) {
                self.get().set_tile_spacing(spacing);
            },
            nb::arg("spacing"))
        .def(
            "set_padding",
            [](const FileGridWidgetRef& self, float padding) { self.get().set_padding(padding); },
            nb::arg("padding"))
        .def(
            "set_icon_size",
            [](const FileGridWidgetRef& self, float size) { self.get().set_icon_size(size); },
            nb::arg("size"))
        .def(
            "set_scrollbar_width",
            [](const FileGridWidgetRef& self, float width) {
                self.get().set_scrollbar_width(width);
            },
            nb::arg("width"))
        .def(
            "item_rect",
            [](const FileGridWidgetRef& self, size_t index) { return self.get().item_rect(index); },
            nb::arg("index"))
        .def(
            "select",
            [](const FileGridWidgetRef& self, size_t index, bool toggle, bool extend,
               bool additive) {
                const bool changed = self.get().select_index(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const FileGridWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const FileGridWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect([state,
                                                               callback = std::move(callback)](
                                                                  termin::gui_native::
                                                                      FileGridWidget&,
                                                                  const std::vector<size_t>&
                                                                      selected) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(selected);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget selection callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect([state, callback = std::move(callback)](
                                                          termin::gui_native::FileGridWidget&,
                                                          size_t index,
                                                          const termin::gui_native::CollectionItem&
                                                              item) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, item);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget activation callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_delete_requested",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().delete_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::FileGridWidget&, size_t index,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, item);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] FileGridWidget delete callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const FileGridWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect([state,
                                                                    callback = std::move(callback)](
                                                                       termin::gui_native::
                                                                           FileGridWidget&,
                                                                       int64_t index, float x,
                                                                       float y) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, x, y);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] FileGridWidget context callback failed");
                    }
                });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::TreeNode>(m, "TreeNode")
        .def_prop_ro("id", [](const termin::gui_native::TreeNode& self) { return self.id; })
        .def_prop_ro("item", [](const termin::gui_native::TreeNode& self) { return self.item; })
        .def_prop_ro("parent", [](const termin::gui_native::TreeNode& self) { return self.parent; })
        .def_prop_ro("children",
                     [](const termin::gui_native::TreeNode& self) { return self.children; });

    nb::class_<termin::gui_native::TreeVisibleRow>(m, "TreeVisibleRow")
        .def_prop_ro("node",
                     [](const termin::gui_native::TreeVisibleRow& self) { return self.node; })
        .def_prop_ro("depth",
                     [](const termin::gui_native::TreeVisibleRow& self) { return self.depth; });

    nb::enum_<termin::gui_native::TreeDropPosition>(m, "TreeDropPosition")
        .value("Before", termin::gui_native::TreeDropPosition::Before)
        .value("Inside", termin::gui_native::TreeDropPosition::Inside)
        .value("After", termin::gui_native::TreeDropPosition::After)
        .value("Root", termin::gui_native::TreeDropPosition::Root);

    nb::class_<termin::gui_native::TreeModel>(m, "TreeModel")
        .def(nb::init<>())
        .def_prop_ro("node_count", &termin::gui_native::TreeModel::size)
        .def_prop_ro("revision", &termin::gui_native::TreeModel::revision)
        .def_prop_ro("roots",
                     [](const termin::gui_native::TreeModel& self) { return self.roots(); })
        .def("contains", &termin::gui_native::TreeModel::contains, nb::arg("node"))
        .def(
            "node",
            [](const termin::gui_native::TreeModel& self, termin::gui_native::TreeNodeId node) {
                return self.node(node);
            },
            nb::arg("node"))
        .def(
            "children",
            [](const termin::gui_native::TreeModel& self, termin::gui_native::TreeNodeId parent) {
                return self.children(parent);
            },
            nb::arg("parent") = termin::gui_native::kInvalidTreeNodeId)
        .def("append_root", &termin::gui_native::TreeModel::append_root, nb::arg("item"))
        .def("append_child", &termin::gui_native::TreeModel::append_child, nb::arg("parent"),
             nb::arg("item"))
        .def("insert_child", &termin::gui_native::TreeModel::insert_child, nb::arg("parent"),
             nb::arg("index"), nb::arg("item"))
        .def("update", &termin::gui_native::TreeModel::update, nb::arg("node"), nb::arg("item"))
        .def("move", &termin::gui_native::TreeModel::move, nb::arg("node"),
             nb::arg("new_parent") = termin::gui_native::kInvalidTreeNodeId,
             nb::arg("index") = SIZE_MAX)
        .def("erase", &termin::gui_native::TreeModel::erase, nb::arg("node"))
        .def("clear", &termin::gui_native::TreeModel::clear);

    nb::class_<termin::gui_native::TreeExpansionModel>(m, "TreeExpansionModel")
        .def(nb::init<>())
        .def_prop_ro("revision", &termin::gui_native::TreeExpansionModel::revision)
        .def("expanded", &termin::gui_native::TreeExpansionModel::expanded, nb::arg("node"))
        .def("set_expanded", &termin::gui_native::TreeExpansionModel::set_expanded, nb::arg("node"),
             nb::arg("expanded"))
        .def("toggle", &termin::gui_native::TreeExpansionModel::toggle, nb::arg("node"))
        .def("clear", &termin::gui_native::TreeExpansionModel::clear)
        .def("reconcile", &termin::gui_native::TreeExpansionModel::reconcile, nb::arg("model"));

    nb::class_<TreeWidgetRef>(m, "TreeWidget")
        .def_prop_ro("widget", [](const TreeWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TreeWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const TreeWidgetRef& self) { return self.get().model(); },
            [](const TreeWidgetRef& self, std::shared_ptr<termin::gui_native::TreeModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "expansion_model",
            [](const TreeWidgetRef& self) { return self.get().expansion_model(); },
            [](const TreeWidgetRef& self,
               std::shared_ptr<termin::gui_native::TreeExpansionModel> expansion) {
                self.get().set_expansion_model(std::move(expansion));
            })
        .def_prop_ro("selected_node",
                     [](const TreeWidgetRef& self) { return self.get().selected_node(); })
        .def_prop_rw(
            "draggable", [](const TreeWidgetRef& self) { return self.get().draggable(); },
            [](const TreeWidgetRef& self, bool value) { self.get().set_draggable(value); })
        .def_prop_ro("dragging", [](const TreeWidgetRef& self) { return self.get().dragging(); })
        .def_prop_rw(
            "scroll_y", [](const TreeWidgetRef& self) { return self.get().scroll_y(); },
            [](const TreeWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const TreeWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_count",
                     [](const TreeWidgetRef& self) { return self.get().visible_count(); })
        .def_prop_ro("visible_range",
                     [](const TreeWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def(
            "visible_row",
            [](const TreeWidgetRef& self, size_t index) { return self.get().visible_row(index); },
            nb::arg("index"))
        .def(
            "set_row_height",
            [](const TreeWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_row_spacing",
            [](const TreeWidgetRef& self, float spacing) { self.get().set_row_spacing(spacing); },
            nb::arg("spacing"))
        .def(
            "set_indent_size",
            [](const TreeWidgetRef& self, float size) { self.get().set_indent_size(size); },
            nb::arg("size"))
        .def(
            "select",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node, bool reveal) {
                const bool changed = self.get().select_node(node, reveal);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"), nb::arg("reveal") = true)
        .def("clear_selection",
             [](const TreeWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "set_expanded",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node, bool expanded) {
                const bool changed = self.get().set_expanded(node, expanded);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"), nb::arg("expanded"))
        .def(
            "toggle",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                const bool changed = self.get().toggle(node);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("node"))
        .def(
            "expanded",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                return self.get().expanded(node);
            },
            nb::arg("node"))
        .def(
            "ensure_visible",
            [](const TreeWidgetRef& self, termin::gui_native::TreeNodeId node) {
                self.get().ensure_visible(node);
                self.widget.throw_pending_exception();
            },
            nb::arg("node"))
        .def(
            "connect_selection_changed",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
                                                            termin::gui_native::TreeNodeId node) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] TreeWidget "
                                         "selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_expansion_changed",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().expansion_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
                                                            termin::gui_native::TreeNodeId node,
                                                            bool expanded) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node, expanded);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] TreeWidget "
                                         "expansion callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId node,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node, item);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] TreeWidget "
                                         "activation callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_delete_requested",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().delete_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId node,
                        const termin::gui_native::CollectionItem& item) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node, item);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error("[termin-gui-native/python] TreeWidget "
                                         "delete callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TreeWidget&,
                                                            termin::gui_native::TreeNodeId node,
                                                            float x, float y) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(node, x, y);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TreeWidget context callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_drop_requested",
            [](const TreeWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().drop_requested().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TreeWidget&, termin::gui_native::TreeNodeId dragged,
                        termin::gui_native::TreeNodeId target,
                        termin::gui_native::TreeDropPosition position) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(dragged, target, position);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TreeWidget drop callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<termin::gui_native::TableRowData>(m, "TableRowData")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::TableRowData* self, std::string stable_id,
               std::vector<std::string> cells, bool enabled) {
                new (self) termin::gui_native::TableRowData{std::move(stable_id), std::move(cells),
                                                            enabled};
            },
            nb::arg("stable_id"), nb::arg("cells"), nb::arg("enabled") = true)
        .def_rw("stable_id", &termin::gui_native::TableRowData::stable_id)
        .def_rw("cells", &termin::gui_native::TableRowData::cells)
        .def_rw("enabled", &termin::gui_native::TableRowData::enabled);

    nb::class_<termin::gui_native::TableRow>(m, "TableRow")
        .def_prop_ro("id", [](const termin::gui_native::TableRow& self) { return self.id; })
        .def_prop_ro("data", [](const termin::gui_native::TableRow& self) { return self.data; });

    nb::class_<termin::gui_native::TableModel>(m, "TableModel")
        .def(nb::init<>())
        .def_prop_ro("row_count", &termin::gui_native::TableModel::size)
        .def_prop_ro("revision", &termin::gui_native::TableModel::revision)
        .def_prop_ro("rows", [](const termin::gui_native::TableModel& self) { return self.rows(); })
        .def("contains", &termin::gui_native::TableModel::contains, nb::arg("row"))
        .def("index_of", &termin::gui_native::TableModel::index_of, nb::arg("row"))
        .def(
            "row_at",
            [](const termin::gui_native::TableModel& self, size_t index) {
                return self.row_at(index);
            },
            nb::arg("index"))
        .def(
            "row",
            [](const termin::gui_native::TableModel& self, termin::gui_native::TableRowId row) {
                return self.row(row);
            },
            nb::arg("row"))
        .def("set_rows", &termin::gui_native::TableModel::set_rows, nb::arg("rows"))
        .def("append", &termin::gui_native::TableModel::append, nb::arg("row"))
        .def("insert", &termin::gui_native::TableModel::insert, nb::arg("index"), nb::arg("row"))
        .def("update", &termin::gui_native::TableModel::update, nb::arg("row"), nb::arg("data"))
        .def("erase", &termin::gui_native::TableModel::erase, nb::arg("row"))
        .def("clear", &termin::gui_native::TableModel::clear);

    nb::enum_<termin::gui_native::TableColumnPolicy>(m, "TableColumnPolicy")
        .value("Fixed", termin::gui_native::TableColumnPolicy::Fixed)
        .value("Stretch", termin::gui_native::TableColumnPolicy::Stretch);

    nb::class_<termin::gui_native::TableColumn>(m, "TableColumn")
        .def(nb::init<>())
        .def(
            "__init__",
            [](termin::gui_native::TableColumn* self, std::string stable_id, std::string header,
               termin::gui_native::TableColumnPolicy policy, float width, float min_width,
               float max_width, float stretch, bool resizable) {
                new (self) termin::gui_native::TableColumn{std::move(stable_id),
                                                           std::move(header),
                                                           policy,
                                                           width,
                                                           min_width,
                                                           max_width,
                                                           stretch,
                                                           resizable};
            },
            nb::arg("stable_id"), nb::arg("header"),
            nb::arg("policy") = termin::gui_native::TableColumnPolicy::Stretch,
            nb::arg("width") = 0.0f, nb::arg("min_width") = 40.0f, nb::arg("max_width") = 0.0f,
            nb::arg("stretch") = 1.0f, nb::arg("resizable") = true)
        .def_rw("stable_id", &termin::gui_native::TableColumn::stable_id)
        .def_rw("header", &termin::gui_native::TableColumn::header)
        .def_rw("policy", &termin::gui_native::TableColumn::policy)
        .def_rw("width", &termin::gui_native::TableColumn::width)
        .def_rw("min_width", &termin::gui_native::TableColumn::min_width)
        .def_rw("max_width", &termin::gui_native::TableColumn::max_width)
        .def_rw("stretch", &termin::gui_native::TableColumn::stretch)
        .def_rw("resizable", &termin::gui_native::TableColumn::resizable);

    nb::class_<termin::gui_native::TableColumnModel>(m, "TableColumnModel")
        .def(nb::init<>())
        .def_prop_ro("column_count", &termin::gui_native::TableColumnModel::size)
        .def_prop_ro("revision", &termin::gui_native::TableColumnModel::revision)
        .def_prop_ro(
            "columns",
            [](const termin::gui_native::TableColumnModel& self) { return self.columns(); })
        .def(
            "column",
            [](const termin::gui_native::TableColumnModel& self, size_t index) {
                return self.column(index);
            },
            nb::arg("index"))
        .def("set_columns", &termin::gui_native::TableColumnModel::set_columns, nb::arg("columns"))
        .def("append", &termin::gui_native::TableColumnModel::append, nb::arg("column"))
        .def("insert", &termin::gui_native::TableColumnModel::insert, nb::arg("index"),
             nb::arg("column"))
        .def("update", &termin::gui_native::TableColumnModel::update, nb::arg("index"),
             nb::arg("column"))
        .def("resize", &termin::gui_native::TableColumnModel::resize, nb::arg("index"),
             nb::arg("width"))
        .def("erase", &termin::gui_native::TableColumnModel::erase, nb::arg("index"))
        .def("clear", &termin::gui_native::TableColumnModel::clear);

    nb::class_<termin::gui_native::TableColumnLayout>(m, "TableColumnLayout")
        .def_prop_ro("x", [](const termin::gui_native::TableColumnLayout& self) { return self.x; })
        .def_prop_ro("width",
                     [](const termin::gui_native::TableColumnLayout& self) { return self.width; });

    nb::class_<TableWidgetRef>(m, "TableWidget")
        .def_prop_ro("widget", [](const TableWidgetRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const TableWidgetRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_rw(
            "model", [](const TableWidgetRef& self) { return self.get().model(); },
            [](const TableWidgetRef& self, std::shared_ptr<termin::gui_native::TableModel> model) {
                self.get().set_model(std::move(model));
            })
        .def_prop_rw(
            "column_model", [](const TableWidgetRef& self) { return self.get().column_model(); },
            [](const TableWidgetRef& self,
               std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
                self.get().set_column_model(std::move(columns));
            })
        .def_prop_rw(
            "selection_mode",
            [](const TableWidgetRef& self) { return self.get().selection().mode(); },
            [](const TableWidgetRef& self, termin::gui_native::SelectionMode mode) {
                self.get().set_selection_mode(mode);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro(
            "selected_indices",
            [](const TableWidgetRef& self) { return self.get().selection().selected_indices(); })
        .def_prop_ro("current_index",
                     [](const TableWidgetRef& self) -> int64_t {
                         const size_t index = self.get().selection().current();
                         return index == termin::gui_native::SelectionModel::npos
                                    ? -1
                                    : static_cast<int64_t>(index);
                     })
        .def_prop_rw(
            "scroll_y", [](const TableWidgetRef& self) { return self.get().scroll_y(); },
            [](const TableWidgetRef& self, float value) { self.get().set_scroll_y(value); })
        .def_prop_ro("content_height",
                     [](const TableWidgetRef& self) { return self.get().content_height(); })
        .def_prop_ro("visible_range",
                     [](const TableWidgetRef& self) {
                         const auto [first, last] = self.get().visible_range();
                         return std::vector<size_t>{first, last};
                     })
        .def_prop_ro("column_layout",
                     [](const TableWidgetRef& self) { return self.get().column_layout(); })
        .def(
            "set_row_height",
            [](const TableWidgetRef& self, float height) { self.get().set_row_height(height); },
            nb::arg("height"))
        .def(
            "set_header_height",
            [](const TableWidgetRef& self, float height) { self.get().set_header_height(height); },
            nb::arg("height"))
        .def(
            "select",
            [](const TableWidgetRef& self, size_t index, bool toggle, bool extend, bool additive) {
                const bool changed = self.get().select_row(index, toggle, extend, additive);
                self.widget.throw_pending_exception();
                return changed;
            },
            nb::arg("index"), nb::arg("toggle") = false, nb::arg("extend") = false,
            nb::arg("additive") = false)
        .def("clear_selection",
             [](const TableWidgetRef& self) {
                 const bool changed = self.get().clear_selection();
                 self.widget.throw_pending_exception();
                 return changed;
             })
        .def(
            "ensure_visible",
            [](const TableWidgetRef& self, size_t index) { self.get().ensure_visible(index); },
            nb::arg("index"))
        .def(
            "connect_selection_changed",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().selection_changed().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            const std::vector<size_t>& selected) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(selected);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget selection callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_activated",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().activated().connect([state, callback = std::move(callback)](
                                                          termin::gui_native::TableWidget&,
                                                          size_t index,
                                                          termin::gui_native::TableRowId row,
                                                          const termin::gui_native::TableRowData&
                                                              data) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, row, data);
                    } catch (...) {
                        if (state && !state->pending_exception) {
                            state->pending_exception = std::current_exception();
                        }
                        tc_log_error(
                            "[termin-gui-native/python] TableWidget activation callback failed");
                    }
                });
            },
            nb::arg("callback"))
        .def(
            "connect_header_clicked",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().header_clicked().connect(
                    [state, callback = std::move(callback)](
                        termin::gui_native::TableWidget&, size_t index,
                        const termin::gui_native::TableColumn& column) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, column);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget header callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_column_resized",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().column_resized().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            size_t index, float width) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, width);
                        } catch (...) {
                            if (state && !state->pending_exception) {
                                state->pending_exception = std::current_exception();
                            }
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget resize callback failed");
                        }
                    });
            },
            nb::arg("callback"))
        .def(
            "connect_context_menu_requested",
            [](const TableWidgetRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().context_menu_requested().connect(
                    [state, callback = std::move(callback)](termin::gui_native::TableWidget&,
                                                            int64_t index, float x, float y) {
                        try {
                            nb::gil_scoped_acquire gil;
                            callback(index, x, y);
                        } catch (...) {
                            if (state && !state->pending_exception)
                                state->pending_exception = std::current_exception();
                            tc_log_error(
                                "[termin-gui-native/python] TableWidget context callback failed");
                        }
                    });
            },
            nb::arg("callback"));

    nb::class_<ComboBoxRef>(m, "ComboBox")
        .def_prop_ro("widget", [](const ComboBoxRef& self) { return self.widget; })
        .def_prop_ro("handle",
                     [](const ComboBoxRef& self) { return WidgetHandle{self.widget.handle}; })
        .def_prop_ro("item_count", [](const ComboBoxRef& self) { return self.get().item_count(); })
        .def_prop_rw(
            "selected_index", [](const ComboBoxRef& self) { return self.get().selected_index(); },
            [](const ComboBoxRef& self, int value) {
                self.get().set_selected_index(value);
                self.widget.throw_pending_exception();
            })
        .def_prop_ro("selected_text",
                     [](const ComboBoxRef& self) { return self.get().selected_text(); })
        .def_prop_ro("open", [](const ComboBoxRef& self) { return self.get().open(); })
        .def(
            "add_item",
            [](const ComboBoxRef& self, const std::string& item) { self.get().add_item(item); },
            nb::arg("item"))
        .def(
            "item_text",
            [](const ComboBoxRef& self, size_t index) { return self.get().item_text(index); },
            nb::arg("index"))
        .def("clear", [](const ComboBoxRef& self) { self.get().clear_items(); })
        .def(
            "connect_changed",
            [](const ComboBoxRef& self, nb::object callback) {
                auto state = self.widget.state;
                return self.get().changed().connect([state, callback = std::move(callback)](
                                                        termin::gui_native::ComboBox&, int index,
                                                        const std::string& text) {
                    try {
                        nb::gil_scoped_acquire gil;
                        callback(index, text);
                    } catch (...) {
                        if (state && !state->pending_exception)
                            state->pending_exception = std::current_exception();
                        tc_log_error("[termin-gui-native/python] ComboBox changed callback failed");
                    }
                });
            },
            nb::arg("callback"));

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
        .def("inspect_snapshot", &document_snapshot_to_python)
        .def("serialize", &Document::serialize)
        .def("restore", &Document::restore, nb::arg("serialized"))
        .def_prop_rw(
            "theme",
            [](const Document &self) {
              return Theme{*tc_ui_document_theme(self.get())};
            },
            [](Document &self, const Theme &theme) {
              if (!tc_ui_document_set_theme(self.get(), &theme.value)) {
                throw std::invalid_argument("invalid native UI theme");
              }
            })
        .def_prop_ro("theme_revision",
                     [](const Document &self) {
                       return tc_ui_document_theme_revision(self.get());
                     })
        .def(
            "adopt",
            [](Document &self, nb::object widget,
               const std::string &debug_name) {
              return self.adopt(std::move(widget), debug_name);
            },
            nb::arg("widget"), nb::arg("debug_name") = "")
        .def("create_registered_widget", &Document::create_registered_widget,
             nb::arg("type_name"))
        .def(
            "add_root",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_add_root(self.get(), handle.handle);
            },
            nb::arg("handle"))
        .def(
            "adopt_root",
            [](Document &self, nb::object widget,
               const std::string &debug_name) {
              WidgetHandle handle = self.adopt(std::move(widget), debug_name);
              if (!tc_ui_document_add_root(self.get(), handle.handle)) {
                tc_ui_document_destroy_widget(self.get(), handle.handle);
                self.throw_pending_exception();
                throw std::runtime_error("failed to add Python widget root");
              }
              return handle;
            },
            nb::arg("widget"), nb::arg("debug_name") = "")
        .def(
            "remove_root",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_remove_root(self.get(), handle.handle);
            },
            nb::arg("handle"))
        .def(
            "destroy_widget",
            [](Document &self, WidgetHandle handle) {
              bool destroyed =
                  tc_ui_document_destroy_widget(self.get(), handle.handle);
              self.throw_pending_exception();
              return destroyed;
            },
            nb::arg("handle"))
        .def(
            "destroy_widget_recursive",
            [](Document &self, WidgetHandle handle) {
              bool destroyed = tc_ui_document_destroy_widget_recursive(
                  self.get(), handle.handle);
              self.throw_pending_exception();
              return destroyed;
            },
            nb::arg("handle"))
        .def(
            "is_alive",
            [](const Document &self, WidgetHandle handle) {
              return tc_ui_document_is_alive(self.get(), handle.handle);
            },
            nb::arg("handle"))
        .def_prop_ro("live_widget_count",
                     [](const Document &self) {
                       return tc_ui_document_live_widget_count(self.get());
                     })
        .def_prop_ro("root_count",
                     [](const Document &self) {
                       return tc_ui_document_root_count(self.get());
                     })
        .def(
            "root_at",
            [](const Document &self, size_t index) {
              return WidgetHandle{tc_ui_document_root_at(self.get(), index)};
            },
            nb::arg("index"))
        .def("ref", &Document::ref, nb::arg("handle"))
        .def(
            "measure_text",
            [](Document &self, const std::string &text, float font_size) {
              tc_ui_text_metrics metrics{};
              if (!tc_ui_document_measure_text(self.get(), text.data(),
                                               text.size(), font_size,
                                               &metrics)) {
                throw std::runtime_error("text measurement failed");
              }
              return metrics;
            },
            nb::arg("text"), nb::arg("font_size"))
        .def("set_clipboard_handlers", &Document::set_clipboard_handlers,
             nb::arg("getter"), nb::arg("setter"))
        .def(
            "create_hstack",
            [](Document &self, const std::string &debug_name) {
              return self.make_native<termin::gui_native::HStack>(
                  debug_name.c_str());
            },
            nb::arg("debug_name") = "HStack")
        .def(
            "create_vstack",
            [](Document &self, const std::string &debug_name) {
              return self.make_native<termin::gui_native::VStack>(
                  debug_name.c_str());
            },
            nb::arg("debug_name") = "VStack")
        .def(
            "create_panel",
            [](Document &self, const std::string &debug_name) {
              return self.make_native<termin::gui_native::Panel>(
                  debug_name.c_str());
            },
            nb::arg("debug_name") = "Panel")
        .def(
            "create_label",
            [](Document &self, const std::string &text,
               const std::string &debug_name) {
              WidgetRef result =
                  self.make_native<termin::gui_native::Label>(text);
              termin::gui_native::Widget *widget =
                  static_cast<termin::gui_native::Widget *>(
                      result.resolve_checked()->body);
              widget->set_debug_name(debug_name);
              return result;
            },
            nb::arg("text"), nb::arg("debug_name") = "Label")
        .def(
            "create_button",
            [](Document &self, const std::string &text,
               const std::string &debug_name) {
              ButtonRef result{
                  self.make_native<termin::gui_native::Button>(text)};
              termin::gui_native::Widget *widget =
                  static_cast<termin::gui_native::Widget *>(
                      result.widget.resolve_checked()->body);
              widget->set_debug_name(debug_name);
              return result;
            },
            nb::arg("text") = "", nb::arg("debug_name") = "Button")
        .def(
            "create_checkbox",
            [](Document &self, bool checked) {
              return CheckboxRef{
                  self.make_native<termin::gui_native::Checkbox>(checked)};
            },
            nb::arg("checked") = false)
        .def(
            "create_scroll_area",
            [](Document &self, const std::string &debug_name) {
              return ScrollAreaRef{
                  self.make_native<termin::gui_native::ScrollArea>(
                      debug_name.c_str())};
            },
            nb::arg("debug_name") = "ScrollArea")
        .def(
            "create_splitter",
            [](Document& self, bool horizontal, const std::string& debug_name) {
              return SplitterRef{
                  self.make_native<termin::gui_native::Splitter>(
                      horizontal
                          ? termin::gui_native::Orientation::Horizontal
                          : termin::gui_native::Orientation::Vertical,
                      debug_name.c_str())};
            },
            nb::arg("horizontal") = true,
            nb::arg("debug_name") = "Splitter")
        .def(
            "create_tab_view",
            [](Document &self, const std::string &debug_name) {
              return TabViewRef{self.make_native<termin::gui_native::TabView>(
                  debug_name.c_str())};
            },
            nb::arg("debug_name") = "TabView")
        .def(
            "create_text_input",
            [](Document &self, const std::string &text) {
              return TextInputRef{
                  self.make_native<termin::gui_native::TextInput>(text)};
            },
            nb::arg("text") = "")
        .def(
            "create_text_area",
            [](Document &self, const std::string &text) {
              return TextAreaRef{
                  self.make_native<termin::gui_native::TextArea>(text)};
            },
            nb::arg("text") = "")
        .def(
            "create_rich_text_view",
            [](Document &self,
               std::shared_ptr<termin::gui_native::RichTextModel> model) {
              return RichTextViewRef{
                  self.make_native<termin::gui_native::RichTextView>(
                      std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def(
            "create_frame_time_graph",
            [](Document &self,
               std::shared_ptr<termin::gui_native::FrameTimeModel> model) {
              return FrameTimeGraphRef{
                  self.make_native<termin::gui_native::FrameTimeGraph>(
                      std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def("create_viewport3d",
             [](Document &self) {
               return Viewport3DRef{
                   self.make_native<termin::gui_native::Viewport3D>()};
             })
        .def(
            "create_scene_view",
            [](Document &self,
               std::shared_ptr<termin::gui_native::GraphicsScene> scene) {
              return SceneViewRef{
                  self.make_native<termin::gui_native::SceneView>(
                      std::move(scene))};
            },
            nb::arg("scene") = nullptr)
        .def(
            "create_spin_box",
            [](Document &self, float value) {
              return SpinBoxRef{
                  self.make_native<termin::gui_native::SpinBox>(value)};
            },
            nb::arg("value") = 0.0f)
        .def(
            "create_slider_edit",
            [](Document &self, float value) {
              return SliderEditRef{
                  self.make_native<termin::gui_native::SliderEdit>(value)};
            },
            nb::arg("value") = 0.0f)
        .def(
            "create_list_widget",
            [](Document &self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
              return ListWidgetRef{
                  self.make_native<termin::gui_native::ListWidget>(
                      std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def(
            "create_file_grid_widget",
            [](Document &self,
               std::shared_ptr<termin::gui_native::CollectionModel> model) {
              return FileGridWidgetRef{
                  self.make_native<termin::gui_native::FileGridWidget>(
                      std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def(
            "create_tool_bar",
            [](Document &self,
               std::shared_ptr<termin::gui_native::CommandModel> model) {
              return ToolBarRef{self.make_native<termin::gui_native::ToolBar>(
                  std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def(
            "create_status_bar",
            [](Document &self, const std::string &text) {
              return StatusBarRef{
                  self.make_native<termin::gui_native::StatusBar>(text)};
            },
            nb::arg("text") = "Ready")
        .def(
            "create_menu",
            [](Document &self,
               std::shared_ptr<termin::gui_native::CommandModel> model) {
              return MenuRef{
                  self.make_native<termin::gui_native::Menu>(std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def("create_menu_bar",
             [](Document &self) {
               return MenuBarRef{
                   self.make_native<termin::gui_native::MenuBar>()};
             })
        .def(
            "create_dialog",
            [](Document &self, const std::string &title) {
              return DialogRef{
                  self.make_native<termin::gui_native::Dialog>(title)};
            },
            nb::arg("title") = "")
        .def(
            "create_message_box",
            [](Document &self, const std::string &title,
               const std::string &message,
               termin::gui_native::MessageBoxKind kind) {
              return MessageBoxRef{
                  self.make_native<termin::gui_native::MessageBox>(
                      title, message, kind)};
            },
            nb::arg("title"), nb::arg("message"),
            nb::arg("kind") = termin::gui_native::MessageBoxKind::Information)
        .def(
            "create_input_dialog",
            [](Document &self, const std::string &title,
               const std::string &message, const std::string &value) {
              return InputDialogRef{
                  self.make_native<termin::gui_native::InputDialog>(
                      title, message, value)};
            },
            nb::arg("title"), nb::arg("message") = "", nb::arg("value") = "")
        .def(
            "create_file_dialog",
            [](Document &self, termin::gui_native::FileDialogMode mode) {
              return FileDialogOverlayRef{
                  self.make_native<termin::gui_native::FileDialogOverlay>(
                      mode)};
            },
            nb::arg("mode"))
        .def(
            "create_color_picker",
            [](Document &self,
               std::shared_ptr<termin::gui_native::ColorPickerModel> model) {
              return ColorPickerRef{
                  self.make_native<termin::gui_native::ColorPicker>(
                      std::move(model))};
            },
            nb::arg("model") = nullptr)
        .def(
            "create_color_dialog",
            [](Document &self, tc_ui_color initial, bool show_alpha,
               const std::string &title) {
              return ColorDialogRef{
                  self.make_native<termin::gui_native::ColorDialog>(
                      termin::gui_native::Color{initial.r, initial.g, initial.b,
                                                initial.a},
                      show_alpha, title)};
            },
            nb::arg("initial") = tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
            nb::arg("show_alpha") = true, nb::arg("title") = "Color Picker")
        .def(
            "create_tree_widget",
            [](Document &self,
               std::shared_ptr<termin::gui_native::TreeModel> model,
               std::shared_ptr<termin::gui_native::TreeExpansionModel>
                   expansion) {
              return TreeWidgetRef{
                  self.make_native<termin::gui_native::TreeWidget>(
                      std::move(model), std::move(expansion))};
            },
            nb::arg("model") = nullptr, nb::arg("expansion") = nullptr)
        .def(
            "create_table_widget",
            [](Document &self,
               std::shared_ptr<termin::gui_native::TableModel> model,
               std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
              return TableWidgetRef{
                  self.make_native<termin::gui_native::TableWidget>(
                      std::move(model), std::move(columns))};
            },
            nb::arg("model") = nullptr, nb::arg("columns") = nullptr)
        .def("create_combo_box",
             [](Document &self) {
               return ComboBoxRef{
                   self.make_native<termin::gui_native::ComboBox>()};
             })
        .def(
            "create_icon_button",
            [](Document &self, const std::string &icon) {
              return IconButtonRef{
                  self.make_native<termin::gui_native::IconButton>(icon)};
            },
            nb::arg("icon") = "")
        .def("create_image_widget",
             [](Document &self) {
               return ImageWidgetRef{
                   self.make_native<termin::gui_native::ImageWidget>()};
             })
        .def("create_canvas",
             [](Document &self) {
               return CanvasRef{self.make_native<termin::gui_native::Canvas>()};
             })
        .def(
            "layout_roots",
            [](Document &self, tc_ui_rect rect) {
              tc_ui_document_layout_roots(self.get(), rect);
              self.throw_pending_exception();
            },
            nb::arg("rect"))
        .def(
            "paint_roots",
            [](Document &self, PaintContext &context) {
              tc_ui_document_paint_roots(self.get(), context.get());
              self.throw_pending_exception();
            },
            nb::arg("context"))
        .def(
            "paint",
            [](Document &self, PaintContext &context) {
              tc_ui_document_paint(self.get(), context.get());
              self.throw_pending_exception();
            },
            nb::arg("context"))
        .def(
            "show_overlay",
            [](Document &self, WidgetHandle handle, uint32_t flags) {
              bool shown =
                  tc_ui_document_show_overlay(self.get(), handle.handle, flags);
              self.throw_pending_exception();
              return shown;
            },
            nb::arg("handle"), nb::arg("flags") = 0)
        .def(
            "dismiss_overlay",
            [](Document &self, WidgetHandle handle,
               tc_ui_overlay_dismiss_reason reason) {
              bool dismissed = tc_ui_document_dismiss_overlay(
                  self.get(), handle.handle, reason);
              self.throw_pending_exception();
              return dismissed;
            },
            nb::arg("handle"),
            nb::arg("reason") = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
        .def_prop_ro("overlay_count",
                     [](const Document &self) {
                       return tc_ui_document_overlay_count(self.get());
                     })
        .def(
            "overlay_at",
            [](const Document &self, size_t index) {
              return WidgetHandle{tc_ui_document_overlay_at(self.get(), index)};
            },
            nb::arg("index"))
        .def(
            "overlay_flags_at",
            [](const Document &self, size_t index) {
              return tc_ui_document_overlay_flags_at(self.get(), index);
            },
            nb::arg("index"))
        .def(
            "hit_test",
            [](Document &self, float x, float y) {
              tc_widget_handle handle =
                  tc_ui_document_hit_test(self.get(), x, y);
              self.throw_pending_exception();
              return WidgetHandle{handle};
            },
            nb::arg("x"), nb::arg("y"))
        .def(
            "dispatch_pointer_event",
            [](Document &self, const tc_ui_pointer_event &event) {
              tc_ui_event_result result =
                  tc_ui_document_dispatch_pointer_event(self.get(), &event);
              self.throw_pending_exception();
              return result;
            },
            nb::arg("event"))
        .def(
            "dispatch_key_event",
            [](Document &self, const tc_ui_key_event &event) {
              tc_ui_event_result result =
                  tc_ui_document_dispatch_key_event(self.get(), &event);
              self.throw_pending_exception();
              return result;
            },
            nb::arg("event"))
        .def(
            "dispatch_text_event",
            [](Document &self, const std::string &text) {
              const tc_ui_text_event event{text.c_str()};
              tc_ui_event_result result =
                  tc_ui_document_dispatch_text_event(self.get(), &event);
              self.throw_pending_exception();
              return result;
            },
            nb::arg("text"))
        .def_prop_ro("hovered_widget",
                     [](const Document &self) {
                       return WidgetHandle{
                           tc_ui_document_hovered_widget(self.get())};
                     })
        .def_prop_ro("pointer_capture",
                     [](const Document &self) {
                       return WidgetHandle{
                           tc_ui_document_pointer_capture(self.get())};
                     })
        .def_prop_ro("pressed_widget",
                     [](const Document &self) {
                       return WidgetHandle{
                           tc_ui_document_pressed_widget(self.get())};
                     })
        .def(
            "set_pointer_capture",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_set_pointer_capture(self.get(),
                                                        handle.handle);
            },
            nb::arg("handle"))
        .def(
            "release_pointer_capture",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_release_pointer_capture(self.get(),
                                                            handle.handle);
            },
            nb::arg("handle"))
        .def_prop_ro("focused_widget",
                     [](const Document &self) {
                       return WidgetHandle{
                           tc_ui_document_focused_widget(self.get())};
                     })
        .def(
            "set_focus",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_set_focus(self.get(), handle.handle);
            },
            nb::arg("handle"))
        .def(
            "clear_focus",
            [](Document &self, WidgetHandle handle) {
              return tc_ui_document_clear_focus(self.get(), handle.handle);
            },
            nb::arg("handle"))
        .def("focus_next",
             [](Document &self) {
               return tc_ui_document_focus_next(self.get());
             })
        .def("focus_previous", [](Document &self) {
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
