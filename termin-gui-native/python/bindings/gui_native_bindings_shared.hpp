#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <tcbase/tc_log.h>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/tc_ui_document.h>
#include <termin/gui_native/widgets.hpp>
#include <tgfx2/render_context.hpp>

namespace nb = nanobind;

namespace termin::gui_native::python_bindings {

constexpr uint32_t PYTHON_WIDGET_MAGIC = 0x54475549u; // "TGUI"

class DrawList {
private:
  tc_ui_draw_list *draw_list_ = nullptr;

public:
  DrawList();
  ~DrawList();

  DrawList(const DrawList &) = delete;
  DrawList &operator=(const DrawList &) = delete;

  tc_ui_draw_list *get() const;
};

struct DrawCommand {
  tc_ui_draw_command value{};
  std::string text;
  std::vector<tc_ui_point> points;

  explicit DrawCommand(const tc_ui_draw_command &source);
  DrawCommand(const DrawCommand &other);
  DrawCommand(DrawCommand &&other) noexcept;
  DrawCommand &operator=(const DrawCommand &other);
  DrawCommand &operator=(DrawCommand &&other) noexcept;

private:
  void refresh_pointers();
};

class PaintContext {
private:
  tc_ui_paint_context *context_ = nullptr;
  bool owns_ = false;

public:
  explicit PaintContext(DrawList &draw_list);
  PaintContext(tc_ui_paint_context *context, bool owns);
  ~PaintContext();
  PaintContext(PaintContext &&other) noexcept;
  PaintContext &operator=(PaintContext &&other) noexcept;

  PaintContext(const PaintContext &) = delete;
  PaintContext &operator=(const PaintContext &) = delete;

  tc_ui_paint_context *get() const;
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
  CornerRadius = TC_UI_STYLE_CORNER_RADIUS,
  All = TC_UI_STYLE_ALL_FIELDS,
};

struct Theme {
  tc_ui_theme value{};

  Theme();
  explicit Theme(const tc_ui_theme &source);
  tc_ui_role_style &role(tc_ui_style_role role);
};

struct DocumentState {
  tc_ui_document *document = nullptr;
  std::exception_ptr pending_exception;
};

tc_value python_to_tc_value(nb::object value);
nb::object tc_value_to_python(const tc_value *value);
void register_document_state(const std::shared_ptr<DocumentState> &state);
void unregister_document_state(tc_ui_document *document);
std::shared_ptr<DocumentState> find_document_state(tc_ui_document *document);

struct WidgetRef {
  std::shared_ptr<DocumentState> state;
  tc_widget_handle handle = tc_widget_handle_invalid();

  bool alive() const;
  tc_widget *resolve() const;
  tc_widget *resolve_checked() const;
  void throw_pending_exception() const;
};

template <typename T>
T &native_widget_checked(const WidgetRef &ref, const char *expected_type) {
  tc_widget *widget = ref.resolve_checked();
  auto *base = static_cast<termin::gui_native::Widget *>(widget->body);
  T *typed = dynamic_cast<T *>(base);
  if (!typed) {
    throw std::runtime_error(std::string("widget is not a ") + expected_type);
  }
  return *typed;
}

struct TextInputRef {
  WidgetRef widget;
  termin::gui_native::TextInput &get() const;
};

struct TextAreaRef {
  WidgetRef widget;
  termin::gui_native::TextArea &get() const;
};

#define TERMIN_GUI_NATIVE_WIDGET_REF(Name, Type)                               \
  struct Name {                                                                \
    WidgetRef widget;                                                          \
    termin::gui_native::Type &get() const;                                     \
  }

TERMIN_GUI_NATIVE_WIDGET_REF(SpinBoxRef, SpinBox);
TERMIN_GUI_NATIVE_WIDGET_REF(SliderEditRef, SliderEdit);
TERMIN_GUI_NATIVE_WIDGET_REF(ButtonRef, Button);
TERMIN_GUI_NATIVE_WIDGET_REF(CheckboxRef, Checkbox);
TERMIN_GUI_NATIVE_WIDGET_REF(GroupBoxRef, GroupBox);
TERMIN_GUI_NATIVE_WIDGET_REF(ScrollAreaRef, ScrollArea);
TERMIN_GUI_NATIVE_WIDGET_REF(SplitterRef, Splitter);
TERMIN_GUI_NATIVE_WIDGET_REF(TabViewRef, TabView);
TERMIN_GUI_NATIVE_WIDGET_REF(RichTextViewRef, RichTextView);
TERMIN_GUI_NATIVE_WIDGET_REF(FrameTimeGraphRef, FrameTimeGraph);
TERMIN_GUI_NATIVE_WIDGET_REF(FrameTimelineWidgetRef, FrameTimelineWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(Viewport3DRef, Viewport3D);
TERMIN_GUI_NATIVE_WIDGET_REF(OverlayLayoutRef, OverlayLayout);
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
TERMIN_GUI_NATIVE_WIDGET_REF(TreeTableWidgetRef, TreeTableWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(TableWidgetRef, TableWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(ComboBoxRef, ComboBox);
TERMIN_GUI_NATIVE_WIDGET_REF(IconButtonRef, IconButton);
TERMIN_GUI_NATIVE_WIDGET_REF(ProgressBarRef, ProgressBar);
TERMIN_GUI_NATIVE_WIDGET_REF(ImageWidgetRef, ImageWidget);
TERMIN_GUI_NATIVE_WIDGET_REF(CanvasRef, Canvas);

#undef TERMIN_GUI_NATIVE_WIDGET_REF

class PythonViewportSurfaceHost final
    : public termin::gui_native::ViewportSurfaceHost {
private:
  nb::object object_;
  std::shared_ptr<DocumentState> state_;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;

public:
  PythonViewportSurfaceHost(nb::object object,
                            std::shared_ptr<DocumentState> state);
  bool is_valid() const override;
  uint32_t texture_id() const override;
  termin::gui_native::ViewportSurfaceSize framebuffer_size() const override;
  bool resize(int width, int height) override;
  bool pointer_move(double x, double y) override;
  bool pointer_button(int button, int action, int modifiers,
                      uint32_t click_count) override;
  bool scroll(double x, double y, int modifiers) override;
  bool key(int key, int scancode, int action, int modifiers) override;
  bool text(uint32_t codepoint) override;

private:
  template <typename Result, typename Callback>
  Result invoke(Callback &&callback) const {
    nb::gil_scoped_acquire gil;
    try {
      return callback();
    } catch (...) {
      if (state_ && !state_->pending_exception)
        state_->pending_exception = std::current_exception();
      throw;
    }
  }
};

struct PythonWidget {
  uint32_t magic = PYTHON_WIDGET_MAGIC;
  tc_widget widget{};
  nb::object object;
  std::shared_ptr<DocumentState> state;
  bool callbacks_enabled = false;
  static const tc_widget_vtable VTABLE;

  explicit PythonWidget(nb::object object_, std::string debug_name_,
                        std::shared_ptr<DocumentState> state_);
  static PythonWidget *from_widget(tc_widget *widget);
  static void delete_widget(tc_widget *widget);
  void capture_exception(const char *operation);
  static tc_ui_size measure(tc_widget *widget, tc_ui_document *,
                            tc_ui_constraints constraints);
  static void layout(tc_widget *widget, tc_ui_document *, tc_ui_rect rect);
  static void paint(tc_widget *widget, tc_ui_document *,
                    tc_ui_paint_context *context);
  static tc_ui_event_result pointer_event(tc_widget *widget, tc_ui_document *,
                                          const tc_ui_pointer_event *event);
  static tc_widget_handle hit_test(tc_widget *widget, tc_ui_document *document,
                                   float x, float y);
  static tc_ui_event_result key_event(tc_widget *widget, tc_ui_document *,
                                      const tc_ui_key_event *event);
  static tc_ui_event_result text_event(tc_widget *widget, tc_ui_document *,
                                       const tc_ui_text_event *event);
  static void focus_event(tc_widget *widget, tc_ui_document *, bool focused);
  static void overlay_dismissed(tc_widget *widget, tc_ui_document *,
                                tc_ui_overlay_dismiss_reason reason);
  static void on_destroy(tc_widget *widget, tc_ui_document *);
};

struct PythonWidgetFactory {
  nb::object callable;
  std::string debug_name;
  nb::object serialize_state;
  nb::object deserialize_state;
};

bool create_python_registered_widget(tc_ui_document *document, void *userdata,
                                     tc_widget_factory_result *result);
bool bind_python_registered_widget(tc_ui_document *, tc_widget *widget,
                                   tc_widget_handle handle, void *);
void destroy_python_widget_factory(void *userdata);
bool serialize_python_registered_widget(const tc_widget *widget, void *userdata,
                                        tc_value *out_state);
bool deserialize_python_registered_widget(tc_widget *widget,
                                          const tc_value *state,
                                          void *userdata);

class Document {
private:
  std::shared_ptr<DocumentState> state_;
  nb::object clipboard_getter_;
  nb::object clipboard_setter_;
  nb::object cursor_changed_handler_;
  std::string clipboard_buffer_;

public:
  Document();
  ~Document();

  Document(const Document &) = delete;
  Document &operator=(const Document &) = delete;

  tc_ui_document *get() const;
  WidgetHandle adopt(nb::object object, const std::string &debug_name);
  WidgetRef ref(WidgetHandle handle) const;
  WidgetRef create_registered_widget(const std::string &type_name);
  nb::object serialize();
  void restore(nb::object serialized);

  template <typename T, typename... Args>
  WidgetRef make_native(Args &&...args) {
    auto widget = std::make_unique<T>(std::forward<Args>(args)...);
    tc_widget_handle handle = tc_ui_document_adopt_widget(
        get(), widget->c_widget(),
        &termin::gui_native::Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(handle)) {
      throw std::runtime_error("failed to adopt native widget");
    }
    widget.release();
    return WidgetRef{state_, handle};
  }

  void throw_pending_exception();
  void set_clipboard_handlers(nb::object getter, nb::object setter);
  void set_cursor_changed_handler(nb::object handler);

private:
  static const char *clipboard_get(void *user_data);
  static bool clipboard_set(void *user_data, const char *text,
                            size_t byte_length);
  static void cursor_changed(void *user_data, tc_ui_cursor_intent cursor);
};

nb::object snapshot_handle_or_none(tc_widget_handle handle);
nb::dict document_snapshot_to_python(const Document &document);
DrawCommand command_at_checked(const DrawList &draw_list, size_t index);

} // namespace termin::gui_native::python_bindings
