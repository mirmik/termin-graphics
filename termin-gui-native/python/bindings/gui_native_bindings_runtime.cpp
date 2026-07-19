#include "gui_native_bindings_shared.hpp"

#include <mutex>
#include <unordered_map>

namespace termin::gui_native::python_bindings {

namespace {

std::mutex g_document_states_mutex;
std::unordered_map<tc_ui_document *, std::weak_ptr<DocumentState>>
    g_document_states;

} // namespace

DrawList::DrawList() : draw_list_(tc_ui_draw_list_create()) {
  if (!draw_list_) {
    throw std::runtime_error("failed to create tc_ui_draw_list");
  }
}

DrawList::~DrawList() { tc_ui_draw_list_destroy(draw_list_); }

tc_ui_draw_list *DrawList::get() const { return draw_list_; }

DrawCommand::DrawCommand(const tc_ui_draw_command &source)
    : value(source), text(source.text ? source.text : ""),
      points(source.points && source.point_count > 0
                 ? std::vector<tc_ui_point>(source.points,
                                            source.points + source.point_count)
                 : std::vector<tc_ui_point>{}) {
  refresh_pointers();
}

DrawCommand::DrawCommand(const DrawCommand &other)
    : value(other.value), text(other.text), points(other.points) {
  refresh_pointers();
}

DrawCommand::DrawCommand(DrawCommand &&other) noexcept
    : value(other.value), text(std::move(other.text)),
      points(std::move(other.points)) {
  refresh_pointers();
}

DrawCommand &DrawCommand::operator=(const DrawCommand &other) {
  if (this != &other) {
    value = other.value;
    text = other.text;
    points = other.points;
    refresh_pointers();
  }
  return *this;
}

DrawCommand &DrawCommand::operator=(DrawCommand &&other) noexcept {
  if (this != &other) {
    value = other.value;
    text = std::move(other.text);
    points = std::move(other.points);
    refresh_pointers();
  }
  return *this;
}

void DrawCommand::refresh_pointers() {
  value.text = text.empty() ? nullptr : text.c_str();
  value.points = points.empty() ? nullptr : points.data();
  value.point_count = points.size();
}

PaintContext::PaintContext(DrawList &draw_list)
    : context_(tc_ui_paint_context_create(draw_list.get())), owns_(true) {
  if (!context_) {
    throw std::runtime_error("failed to create tc_ui_paint_context");
  }
}

PaintContext::PaintContext(tc_ui_paint_context *context, bool owns)
    : context_(context), owns_(owns) {
  if (!context_) {
    throw std::runtime_error("cannot wrap null tc_ui_paint_context");
  }
}

PaintContext::~PaintContext() {
  if (owns_) {
    tc_ui_paint_context_destroy(context_);
  }
}

PaintContext::PaintContext(PaintContext &&other) noexcept
    : context_(other.context_), owns_(other.owns_) {
  other.context_ = nullptr;
  other.owns_ = false;
}

PaintContext &PaintContext::operator=(PaintContext &&other) noexcept {
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

tc_ui_paint_context *PaintContext::get() const { return context_; }

Theme::Theme() { tc_ui_theme_init_default(&value); }

Theme::Theme(const tc_ui_theme &source) : value(source) {}

tc_ui_role_style &Theme::role(tc_ui_style_role role_value) {
  if (role_value < TC_UI_STYLE_GENERIC ||
      role_value >= TC_UI_STYLE_ROLE_COUNT) {
    throw std::out_of_range("invalid native UI style role");
  }
  return value.roles[role_value];
}

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
      tc_value_list_push(&result,
                         python_to_tc_value(nb::borrow<nb::object>(item)));
    }
    return result;
  }
  if (nb::isinstance<nb::dict>(value)) {
    tc_value result = tc_value_dict_new();
    for (auto item : nb::cast<nb::dict>(value)) {
      if (!nb::isinstance<nb::str>(item.first)) {
        tc_value_free(&result);
        throw std::invalid_argument(
            "serialized widget state dict keys must be strings");
      }
      const std::string key = nb::cast<std::string>(item.first);
      tc_value_dict_set(
          &result, key.c_str(),
          python_to_tc_value(nb::borrow<nb::object>(item.second)));
    }
    return result;
  }
  throw std::invalid_argument("serialized widget state must contain only None, "
                              "bool, int, float, str, list or dict");
}

nb::object tc_value_to_python(const tc_value *value) {
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
      const tc_value_dict_entry &item = value->data.dict.entries[index];
      result[item.key] = tc_value_to_python(item.value);
    }
    return result;
  }
  }
  throw std::runtime_error("unknown tc_value type in native UI serialization");
}

void register_document_state(const std::shared_ptr<DocumentState> &state) {
  std::lock_guard<std::mutex> lock(g_document_states_mutex);
  g_document_states[state->document] = state;
}

void unregister_document_state(tc_ui_document *document) {
  std::lock_guard<std::mutex> lock(g_document_states_mutex);
  g_document_states.erase(document);
}

std::shared_ptr<DocumentState> find_document_state(tc_ui_document *document) {
  std::lock_guard<std::mutex> lock(g_document_states_mutex);
  const auto found = g_document_states.find(document);
  return found == g_document_states.end() ? nullptr : found->second.lock();
}

bool WidgetRef::alive() const {
  return state && state->document &&
         tc_ui_document_is_alive(state->document, handle);
}

tc_widget *WidgetRef::resolve() const {
  return state && state->document
             ? tc_ui_document_resolve_widget(state->document, handle)
             : nullptr;
}

tc_widget *WidgetRef::resolve_checked() const {
  tc_widget *widget = resolve();
  if (!widget) {
    throw std::runtime_error("widget reference is stale");
  }
  return widget;
}

void WidgetRef::throw_pending_exception() const {
  if (state && state->pending_exception) {
    std::exception_ptr exception = state->pending_exception;
    state->pending_exception = nullptr;
    std::rethrow_exception(exception);
  }
}

TextInput &TextInputRef::get() const {
  return native_widget_checked<TextInput>(widget, "TextInput");
}

TextArea &TextAreaRef::get() const {
  return native_widget_checked<TextArea>(widget, "TextArea");
}

#define TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(Name, Type)                          \
  termin::gui_native::Type &Name::get() const {                                \
    return native_widget_checked<termin::gui_native::Type>(widget, #Type);     \
  }

TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(SpinBoxRef, SpinBox)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(SliderEditRef, SliderEdit)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ButtonRef, Button)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(CheckboxRef, Checkbox)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(GroupBoxRef, GroupBox)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ScrollAreaRef, ScrollArea)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(SplitterRef, Splitter)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(TabViewRef, TabView)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(RichTextViewRef, RichTextView)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(FrameTimeGraphRef, FrameTimeGraph)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(FrameTimelineWidgetRef, FrameTimelineWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(Viewport3DRef, Viewport3D)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(SceneViewRef, SceneView)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ListWidgetRef, ListWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(FileGridWidgetRef, FileGridWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ToolBarRef, ToolBar)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(StatusBarRef, StatusBar)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(MenuRef, Menu)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(MenuBarRef, MenuBar)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(DialogRef, Dialog)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(MessageBoxRef, MessageBox)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(InputDialogRef, InputDialog)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(FileDialogOverlayRef, FileDialogOverlay)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ColorPickerRef, ColorPicker)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ColorDialogRef, ColorDialog)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(TreeWidgetRef, TreeWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(TreeTableWidgetRef, TreeTableWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(TableWidgetRef, TableWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ComboBoxRef, ComboBox)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(IconButtonRef, IconButton)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ProgressBarRef, ProgressBar)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(ImageWidgetRef, ImageWidget)
TERMIN_GUI_NATIVE_WIDGET_REF_IMPL(CanvasRef, Canvas)

#undef TERMIN_GUI_NATIVE_WIDGET_REF_IMPL

PythonViewportSurfaceHost::PythonViewportSurfaceHost(
    nb::object object, std::shared_ptr<DocumentState> state)
    : object_(std::move(object)), state_(std::move(state)) {}

bool PythonViewportSurfaceHost::is_valid() const {
  return invoke<bool>(
      [this] { return nb::cast<bool>(object_.attr("is_valid")()); });
}

uint32_t PythonViewportSurfaceHost::texture_id() const {
  return invoke<uint32_t>([this] {
    return nb::cast<uint32_t>(object_.attr("get_tgfx_color_tex_id")());
  });
}

ViewportSurfaceSize PythonViewportSurfaceHost::framebuffer_size() const {
  return invoke<ViewportSurfaceSize>([this] {
    nb::tuple size = nb::cast<nb::tuple>(object_.attr("framebuffer_size")());
    if (size.size() != 2) {
      throw std::runtime_error(
          "viewport surface framebuffer_size() must return two values");
    }
    return ViewportSurfaceSize{nb::cast<int>(size[0]), nb::cast<int>(size[1])};
  });
}

bool PythonViewportSurfaceHost::resize(int width, int height) {
  return invoke<bool>([this, width, height] {
    return nb::cast<bool>(object_.attr("resize")(width, height));
  });
}

bool PythonViewportSurfaceHost::pointer_move(double x, double y) {
  return invoke<bool>([this, x, y] {
    return nb::cast<bool>(object_.attr("dispatch_pointer_move")(x, y));
  });
}

bool PythonViewportSurfaceHost::pointer_button(int button, int action,
                                               int modifiers,
                                               uint32_t click_count) {
  return invoke<bool>([this, button, action, modifiers, click_count] {
    return nb::cast<bool>(object_.attr("dispatch_pointer_button")(
        button, action, modifiers, click_count));
  });
}

bool PythonViewportSurfaceHost::scroll(double x, double y, int modifiers) {
  return invoke<bool>([this, x, y, modifiers] {
    return nb::cast<bool>(object_.attr("dispatch_scroll")(x, y, modifiers));
  });
}

bool PythonViewportSurfaceHost::key(int key, int scancode, int action,
                                    int modifiers) {
  return invoke<bool>([this, key, scancode, action, modifiers] {
    return nb::cast<bool>(
        object_.attr("dispatch_key")(key, scancode, action, modifiers));
  });
}

bool PythonViewportSurfaceHost::text(uint32_t codepoint) {
  return invoke<bool>([this, codepoint] {
    return nb::cast<bool>(object_.attr("dispatch_text")(codepoint));
  });
}

PythonWidget::PythonWidget(nb::object object_, std::string debug_name_,
                           std::shared_ptr<DocumentState> state_)
    : object(std::move(object_)), state(std::move(state_)) {
  tc_widget_init_unowned(&widget, &VTABLE, TC_LANGUAGE_PYTHON, this);
  if (!tc_widget_set_debug_name(&widget, debug_name_.c_str())) {
    throw std::runtime_error("failed to set Python widget debug name");
  }
}

PythonWidget *PythonWidget::from_widget(tc_widget *widget) {
  if (!widget || !widget->body) {
    return nullptr;
  }
  auto *self = static_cast<PythonWidget *>(widget->body);
  return self->magic == PYTHON_WIDGET_MAGIC ? self : nullptr;
}

void PythonWidget::delete_widget(tc_widget *widget) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error(
        "[termin-gui-native/python] cannot delete invalid Python widget shim");
    return;
  }
  self->magic = 0;
  nb::gil_scoped_acquire gil;
  delete self;
}

void PythonWidget::capture_exception(const char *operation) {
  nb::gil_scoped_acquire gil;
  if (state && !state->pending_exception) {
    state->pending_exception = std::current_exception();
  }
  tc_log_error("[termin-gui-native/python] Python widget %s failed for '%s'",
               operation,
               tc_widget_debug_name(&widget) ? tc_widget_debug_name(&widget)
                                             : "<unnamed>");
}

tc_ui_size PythonWidget::measure(tc_widget *widget, tc_ui_document *,
                                 tc_ui_constraints constraints) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error(
        "[termin-gui-native/python] cannot measure invalid Python widget shim");
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

void PythonWidget::layout(tc_widget *widget, tc_ui_document *,
                          tc_ui_rect rect) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error(
        "[termin-gui-native/python] cannot layout invalid Python widget shim");
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

void PythonWidget::paint(tc_widget *widget, tc_ui_document *,
                         tc_ui_paint_context *context) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error(
        "[termin-gui-native/python] cannot paint invalid Python widget shim");
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

tc_ui_event_result
PythonWidget::pointer_event(tc_widget *widget, tc_ui_document *,
                            const tc_ui_pointer_event *event) {
  PythonWidget *self = from_widget(widget);
  if (!self || !event) {
    tc_log_error("[termin-gui-native/python] cannot route pointer event to "
                 "invalid Python widget shim");
    return TC_UI_EVENT_IGNORED;
  }
  try {
    nb::gil_scoped_acquire gil;
    return nb::cast<tc_ui_event_result>(
        self->object.attr("pointer_event")(*event));
  } catch (...) {
    self->capture_exception("pointer_event");
    return TC_UI_EVENT_IGNORED;
  }
}

tc_widget_handle PythonWidget::hit_test(tc_widget *widget,
                                        tc_ui_document *document, float x,
                                        float y) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error("[termin-gui-native/python] cannot hit-test invalid Python "
                 "widget shim");
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

tc_ui_event_result PythonWidget::key_event(tc_widget *widget, tc_ui_document *,
                                           const tc_ui_key_event *event) {
  PythonWidget *self = from_widget(widget);
  if (!self || !event) {
    tc_log_error("[termin-gui-native/python] cannot route key event to invalid "
                 "Python widget shim");
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

tc_ui_event_result PythonWidget::text_event(tc_widget *widget, tc_ui_document *,
                                            const tc_ui_text_event *event) {
  PythonWidget *self = from_widget(widget);
  if (!self || !event) {
    tc_log_error("[termin-gui-native/python] cannot route text event to "
                 "invalid Python widget shim");
    return TC_UI_EVENT_IGNORED;
  }
  try {
    nb::gil_scoped_acquire gil;
    return nb::cast<tc_ui_event_result>(
        self->object.attr("text_event")(event->text ? event->text : ""));
  } catch (...) {
    self->capture_exception("text_event");
    return TC_UI_EVENT_IGNORED;
  }
}

void PythonWidget::focus_event(tc_widget *widget, tc_ui_document *,
                               bool focused) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error("[termin-gui-native/python] cannot route focus event to "
                 "invalid Python widget shim");
    return;
  }
  try {
    nb::gil_scoped_acquire gil;
    self->object.attr("focus_event")(focused);
  } catch (...) {
    self->capture_exception("focus_event");
  }
}

void PythonWidget::overlay_dismissed(tc_widget *widget, tc_ui_document *,
                                     tc_ui_overlay_dismiss_reason reason) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error("[termin-gui-native/python] cannot notify invalid dismissed "
                 "overlay shim");
    return;
  }
  try {
    nb::gil_scoped_acquire gil;
    self->object.attr("overlay_dismissed")(reason);
  } catch (...) {
    self->capture_exception("overlay_dismissed");
  }
}

void PythonWidget::on_destroy(tc_widget *widget, tc_ui_document *) {
  PythonWidget *self = from_widget(widget);
  if (!self) {
    tc_log_error(
        "[termin-gui-native/python] cannot destroy invalid Python widget shim");
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

const tc_widget_vtable PythonWidget::VTABLE{
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

bool create_python_registered_widget(tc_ui_document *document, void *userdata,
                                     tc_widget_factory_result *result) {
  auto *factory = static_cast<PythonWidgetFactory *>(userdata);
  std::shared_ptr<DocumentState> state = find_document_state(document);
  if (!factory || !state || !result) {
    tc_log_error("[termin-gui-native/python] registered widget factory has no "
                 "document state");
    return false;
  }
  nb::gil_scoped_acquire gil;
  try {
    nb::object object = factory->callable();
    auto *widget = new PythonWidget(object, factory->debug_name, state);
    *result = tc_widget_factory_result{
        &widget->widget,
        &PythonWidget::delete_widget,
        TC_WIDGET_OWNED,
    };
    return true;
  } catch (...) {
    if (!state->pending_exception)
      state->pending_exception = std::current_exception();
    tc_log_error(
        "[termin-gui-native/python] registered widget constructor failed");
    return false;
  }
}

bool bind_python_registered_widget(tc_ui_document *, tc_widget *widget,
                                   tc_widget_handle handle, void *) {
  PythonWidget *python_widget = PythonWidget::from_widget(widget);
  if (!python_widget || !python_widget->state) {
    tc_log_error("[termin-gui-native/python] registered widget adoption lost "
                 "Python body");
    return false;
  }
  nb::gil_scoped_acquire gil;
  try {
    python_widget->object.attr("_bind_native")(
        WidgetRef{python_widget->state, handle});
    python_widget->callbacks_enabled = true;
    return true;
  } catch (...) {
    if (!python_widget->state->pending_exception)
      python_widget->state->pending_exception = std::current_exception();
    tc_log_error("[termin-gui-native/python] registered widget bind failed");
    return false;
  }
}

void destroy_python_widget_factory(void *userdata) {
  if (!userdata)
    return;
  nb::gil_scoped_acquire gil;
  delete static_cast<PythonWidgetFactory *>(userdata);
}

bool serialize_python_registered_widget(const tc_widget *widget, void *userdata,
                                        tc_value *out_state) {
  auto *factory = static_cast<PythonWidgetFactory *>(userdata);
  PythonWidget *python_widget =
      PythonWidget::from_widget(const_cast<tc_widget *>(widget));
  if (!factory || !python_widget || !out_state) {
    tc_log_error("[termin-gui-native/python] invalid Python widget state "
                 "serializer context");
    return false;
  }
  if (factory->serialize_state.is_none())
    return true;
  nb::gil_scoped_acquire gil;
  try {
    tc_value state =
        python_to_tc_value(factory->serialize_state(python_widget->object));
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
    tc_log_error("[termin-gui-native/python] registered widget state "
                 "serialization failed");
    return false;
  }
}

bool deserialize_python_registered_widget(tc_widget *widget,
                                          const tc_value *state,
                                          void *userdata) {
  auto *factory = static_cast<PythonWidgetFactory *>(userdata);
  PythonWidget *python_widget = PythonWidget::from_widget(widget);
  if (!factory || !python_widget || !state) {
    tc_log_error("[termin-gui-native/python] invalid Python widget state "
                 "deserializer context");
    return false;
  }
  if (factory->deserialize_state.is_none())
    return tc_value_dict_size(state) == 0;
  nb::gil_scoped_acquire gil;
  try {
    factory->deserialize_state(python_widget->object,
                               tc_value_to_python(state));
    return true;
  } catch (...) {
    if (!python_widget->state->pending_exception)
      python_widget->state->pending_exception = std::current_exception();
    tc_log_error("[termin-gui-native/python] registered widget state "
                 "deserialization failed");
    return false;
  }
}

Document::Document()
    : state_(std::make_shared<DocumentState>()), clipboard_getter_(nb::none()),
      clipboard_setter_(nb::none()), cursor_changed_handler_(nb::none()) {
  state_->document = tc_ui_document_create();
  if (!state_->document) {
    throw std::runtime_error("failed to create tc_ui_document");
  }
  register_document_state(state_);
}

Document::~Document() {
  if (state_ && state_->document) {
    tc_ui_document_set_cursor_changed_callback(state_->document, nullptr, nullptr);
    unregister_document_state(state_->document);
    tc_ui_document_destroy(state_->document);
    state_->document = nullptr;
    state_->pending_exception = nullptr;
  }
}

tc_ui_document *Document::get() const { return state_->document; }

WidgetHandle Document::adopt(nb::object object, const std::string &debug_name) {
  auto widget = std::make_unique<PythonWidget>(object, debug_name, state_);
  tc_widget_handle handle = tc_ui_document_adopt_widget(
      get(), &widget->widget, &PythonWidget::delete_widget);
  if (tc_widget_handle_is_invalid(handle)) {
    throw std::runtime_error("failed to adopt Python widget");
  }
  PythonWidget *adopted_widget = widget.release();
  try {
    object.attr("_bind_native")(WidgetRef{state_, handle});
    adopted_widget->callbacks_enabled = true;
  } catch (...) {
    tc_ui_document_destroy_widget(get(), handle);
    throw;
  }
  return WidgetHandle{handle};
}

WidgetRef Document::ref(WidgetHandle handle) const {
  return WidgetRef{state_, handle.handle};
}

WidgetRef Document::create_registered_widget(const std::string &type_name) {
  const tc_widget_handle handle =
      tc_ui_document_create_registered_widget(get(), type_name.c_str());
  throw_pending_exception();
  if (tc_widget_handle_is_invalid(handle)) {
    throw std::runtime_error("failed to create registered widget type '" +
                             type_name + "'");
  }
  return WidgetRef{state_, handle};
}

nb::object Document::serialize() {
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

void Document::restore(nb::object serialized) {
  tc_value value = python_to_tc_value(std::move(serialized));
  bool restored = tc_ui_document_restore(get(), &value);
  tc_value_free(&value);
  throw_pending_exception();
  if (!restored) {
    throw std::runtime_error("failed to restore native UI document");
  }
}

void Document::throw_pending_exception() {
  if (state_->pending_exception) {
    std::exception_ptr exception = state_->pending_exception;
    state_->pending_exception = nullptr;
    std::rethrow_exception(exception);
  }
}

void Document::set_clipboard_handlers(nb::object getter, nb::object setter) {
  clipboard_getter_ = std::move(getter);
  clipboard_setter_ = std::move(setter);
  tc_ui_document_set_clipboard(
      get(), clipboard_getter_.is_none() ? nullptr : &Document::clipboard_get,
      clipboard_setter_.is_none() ? nullptr : &Document::clipboard_set, this);
}

void Document::set_cursor_changed_handler(nb::object handler) {
  cursor_changed_handler_ = std::move(handler);
  tc_ui_document_set_cursor_changed_callback(
      get(), cursor_changed_handler_.is_none() ? nullptr : &Document::cursor_changed,
      cursor_changed_handler_.is_none() ? nullptr : this);
}

void Document::cursor_changed(void *user_data, tc_ui_cursor_intent cursor) {
  auto *self = static_cast<Document *>(user_data);
  try {
    nb::gil_scoped_acquire gil;
    self->cursor_changed_handler_(cursor);
  } catch (...) {
    if (!self->state_->pending_exception) {
      self->state_->pending_exception = std::current_exception();
    }
    tc_log_error("[termin-gui-native/python] cursor changed handler failed");
  }
}

const char *Document::clipboard_get(void *user_data) {
  auto *self = static_cast<Document *>(user_data);
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

bool Document::clipboard_set(void *user_data, const char *text,
                             size_t byte_length) {
  auto *self = static_cast<Document *>(user_data);
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

nb::object snapshot_handle_or_none(tc_widget_handle handle) {
  return tc_widget_handle_is_invalid(handle) ? nb::none()
                                             : nb::cast(WidgetHandle{handle});
}

nb::dict document_snapshot_to_python(const Document &document) {
  DocumentSnapshot snapshot(document.get());
  nb::dict result;
  nb::list widgets;
  for (const tc_ui_widget_snapshot &widget : snapshot.widgets()) {
    nb::dict item;
    item["handle"] = WidgetHandle{widget.handle};
    item["parent"] = snapshot_handle_or_none(widget.parent);
    item["type_name"] = widget.type_name ? widget.type_name : "";
    item["stable_id"] =
        widget.stable_id ? nb::cast(widget.stable_id) : nb::none();
    item["name"] = widget.name ? nb::cast(widget.name) : nb::none();
    item["debug_name"] =
        widget.debug_name ? nb::cast(widget.debug_name) : nb::none();
    item["native_language"] = widget.native_language;
    item["ownership"] = widget.ownership;
    item["bounds"] = widget.bounds;
    item["min_size"] = widget.min_size;
    item["preferred_size"] = widget.preferred_size;
    item["max_size"] = widget.max_size;
    item["flags"] = widget.flags;
    item["dirty_flags"] = widget.dirty_flags;
    item["cursor_intent"] = widget.cursor_intent;
    item["style_role"] = widget.style_role;
    item["style_override"] = widget.style_override;
    nb::list children;
    for (size_t index = 0; index < widget.child_count; ++index) {
      children.append(
          WidgetHandle{snapshot.children()[widget.child_offset + index]});
    }
    item["children"] = std::move(children);
    widgets.append(std::move(item));
  }
  result["widgets"] = std::move(widgets);

  nb::list roots;
  for (tc_widget_handle handle : snapshot.roots()) {
    roots.append(WidgetHandle{handle});
  }
  result["roots"] = std::move(roots);

  nb::list overlays;
  for (const tc_ui_overlay_snapshot &overlay : snapshot.overlays()) {
    nb::dict item;
    item["handle"] = WidgetHandle{overlay.handle};
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
  interaction["cursor_intent"] = snapshot.data().cursor_intent;
  result["interaction"] = std::move(interaction);
  result["theme_revision"] = snapshot.data().theme_revision;
  return result;
}

DrawCommand command_at_checked(const DrawList &draw_list, size_t index) {
  const tc_ui_draw_command *command =
      tc_ui_draw_list_command_at(draw_list.get(), index);
  if (!command) {
    throw std::out_of_range("draw command index out of range");
  }
  return DrawCommand{*command};
}

} // namespace termin::gui_native::python_bindings
