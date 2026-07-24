#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_rendering_and_document(nb::module_ &m) {
  m.def("tc_ui_document_registry_get_all_info", []() {
    nb::list result;
    const size_t capacity = tc_ui_document_pool_capacity();
    for (size_t index = 0; index < capacity; ++index) {
      tc_ui_document_info info{};
      if (!tc_ui_document_info_at(index, &info))
        continue;
      nb::dict record;
      record["handle"] = nb::make_tuple(info.handle.index, info.handle.generation);
      record["name"] = info.debug_name;
      record["live_widget_count"] = info.live_widget_count;
      record["root_count"] = info.root_count;
      record["overlay_count"] = info.overlay_count;
      result.append(std::move(record));
    }
    return result;
  });

  nb::enum_<tc_ui_texture_sampling>(m, "TextureSampling")
      .value("Linear", TC_UI_TEXTURE_SAMPLING_LINEAR)
      .value("Nearest", TC_UI_TEXTURE_SAMPLING_NEAREST);

  nb::class_<DrawCommand>(m, "DrawCommand")
      .def_prop_ro(
          "type", [](const DrawCommand &command) { return command.value.type; })
      .def_prop_ro(
          "rect", [](const DrawCommand &command) { return command.value.rect; })
      .def_prop_ro("p0",
                   [](const DrawCommand &command) { return command.value.p0; })
      .def_prop_ro("p1",
                   [](const DrawCommand &command) { return command.value.p1; })
      .def_prop_ro(
          "color",
          [](const DrawCommand &command) { return command.value.color; })
      .def_prop_ro(
          "thickness",
          [](const DrawCommand &command) { return command.value.thickness; })
      .def_prop_ro("text",
                   [](const DrawCommand &command) { return command.text; })
      .def_prop_ro(
          "font_size",
          [](const DrawCommand &command) { return command.value.font_size; })
      .def_prop_ro(
          "radius",
          [](const DrawCommand &command) { return command.value.radius; })
      .def_prop_ro("start_radians",
                   [](const DrawCommand &command) {
                     return command.value.start_radians;
                   })
      .def_prop_ro(
          "end_radians",
          [](const DrawCommand &command) { return command.value.end_radians; })
      .def_prop_ro(
          "segments",
          [](const DrawCommand &command) { return command.value.segments; })
      .def_prop_ro("points",
                   [](const DrawCommand &command) { return command.points; })
      .def_prop_ro(
          "texture_id",
          [](const DrawCommand &command) { return command.value.texture_id; })
      .def_prop_ro("texture_sampling", [](const DrawCommand &command) {
        return command.value.texture_sampling;
      })
      .def_prop_ro("flip_v", [](const DrawCommand &command) {
        return command.value.flip_v;
      });

  nb::class_<DrawList>(m, "DrawList")
      .def(nb::init<>())
      .def("clear", [](DrawList &self) { tc_ui_draw_list_clear(self.get()); })
      .def_prop_ro("command_count",
                   [](const DrawList &self) {
                     return tc_ui_draw_list_command_count(self.get());
                   })
      .def("command_at", &command_at_checked, nb::arg("index"))
      .def_prop_ro("commands", [](const DrawList &self) {
        nb::list commands;
        const size_t count = tc_ui_draw_list_command_count(self.get());
        for (size_t i = 0; i < count; ++i) {
          commands.append(command_at_checked(self, i));
        }
        return commands;
      });

  nb::class_<PaintContext>(m, "PaintContext")
      .def(nb::init<DrawList &>(), nb::arg("draw_list"), nb::keep_alive<1, 2>())
      .def(
          "fill_rect",
          [](PaintContext &self, tc_ui_rect rect, tc_ui_color color) {
            tc_ui_painter_fill_rect(self.get(), rect, color);
          },
          nb::arg("rect"), nb::arg("color"))
      .def(
          "fill_rounded_rect",
          [](PaintContext &self, tc_ui_rect rect, float radius,
             tc_ui_color color) {
            tc_ui_painter_fill_rounded_rect(self.get(), rect, radius, color);
          },
          nb::arg("rect"), nb::arg("radius"), nb::arg("color"))
      .def(
          "stroke_rect",
          [](PaintContext &self, tc_ui_rect rect, tc_ui_color color,
             float thickness) {
            tc_ui_painter_stroke_rect(self.get(), rect, color, thickness);
          },
          nb::arg("rect"), nb::arg("color"), nb::arg("thickness"))
      .def(
          "stroke_rounded_rect",
          [](PaintContext &self, tc_ui_rect rect, float radius,
             tc_ui_color color, float thickness) {
            tc_ui_painter_stroke_rounded_rect(self.get(), rect, radius, color,
                                              thickness);
          },
          nb::arg("rect"), nb::arg("radius"), nb::arg("color"),
          nb::arg("thickness") = 1.0f)
      .def(
          "fill_circle",
          [](PaintContext &self, tc_ui_point center, float radius,
             tc_ui_color color, int32_t segments) {
            tc_ui_painter_fill_circle(self.get(), center, radius, color,
                                      segments);
          },
          nb::arg("center"), nb::arg("radius"), nb::arg("color"),
          nb::arg("segments") = 0)
      .def(
          "stroke_circle",
          [](PaintContext &self, tc_ui_point center, float radius,
             tc_ui_color color, float thickness, int32_t segments) {
            tc_ui_painter_stroke_circle(self.get(), center, radius, color,
                                        thickness, segments);
          },
          nb::arg("center"), nb::arg("radius"), nb::arg("color"),
          nb::arg("thickness") = 1.0f, nb::arg("segments") = 0)
      .def(
          "draw_arc",
          [](PaintContext &self, tc_ui_point center, float radius,
             float start_radians, float end_radians, tc_ui_color color,
             float thickness, int32_t segments) {
            const tc_ui_arc_draw_desc desc{center,      radius, start_radians,
                                           end_radians, color,  thickness,
                                           segments};
            tc_ui_painter_draw_arc(self.get(), &desc);
          },
          nb::arg("center"), nb::arg("radius"), nb::arg("start_radians"),
          nb::arg("end_radians"), nb::arg("color"), nb::arg("thickness") = 1.0f,
          nb::arg("segments") = 0)
      .def(
          "draw_line",
          [](PaintContext &self, tc_ui_point p0, tc_ui_point p1,
             tc_ui_color color, float thickness) {
            tc_ui_painter_draw_line(self.get(), p0, p1, color, thickness);
          },
          nb::arg("p0"), nb::arg("p1"), nb::arg("color"), nb::arg("thickness"))
      .def(
          "draw_polyline",
          [](PaintContext &self, const std::vector<tc_ui_point> &points,
             tc_ui_color color, float thickness) {
            tc_ui_painter_draw_polyline(self.get(), points.data(),
                                        points.size(), color, thickness);
          },
          nb::arg("points"), nb::arg("color"), nb::arg("thickness") = 1.0f)
      .def(
          "draw_texture",
          [](PaintContext &self, tgfx::TextureHandle texture, tc_ui_rect rect,
             std::optional<tc_ui_color> tint, bool flip_v,
             tc_ui_texture_sampling sampling) {
            tc_ui_painter_draw_texture(
                self.get(), texture.id, rect,
                tint.value_or(tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f}), sampling,
                flip_v);
          },
          nb::arg("texture"), nb::arg("rect"),
          nb::arg("tint").none() = nb::none(), nb::arg("flip_v") = false,
          nb::arg("sampling") = TC_UI_TEXTURE_SAMPLING_LINEAR)
      .def(
          "draw_image",
          [](PaintContext &self, tgfx::TextureHandle texture, tc_ui_rect rect,
             std::optional<tc_ui_color> tint, bool flip_v,
             tc_ui_texture_sampling sampling) {
            tc_ui_painter_draw_texture(
                self.get(), texture.id, rect,
                tint.value_or(tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f}), sampling,
                flip_v);
          },
          nb::arg("texture"), nb::arg("rect"),
          nb::arg("tint").none() = nb::none(), nb::arg("flip_v") = false,
          nb::arg("sampling") = TC_UI_TEXTURE_SAMPLING_LINEAR)
      .def(
          "draw_text",
          [](PaintContext &self, const std::string &text, tc_ui_point position,
             float font_size, tc_ui_color color) {
            tc_ui_painter_draw_text(self.get(), text.c_str(), position,
                                    font_size, color);
          },
          nb::arg("text"), nb::arg("position"), nb::arg("font_size"),
          nb::arg("color"))
      .def(
          "push_clip",
          [](PaintContext &self, tc_ui_rect rect) {
            tc_ui_painter_push_clip(self.get(), rect);
          },
          nb::arg("rect"))
      .def("pop_clip",
           [](PaintContext &self) { tc_ui_painter_pop_clip(self.get()); });

  m.def("tc_ui_document_create", []() {
    termin::gui_native::TcDocument document{tc_ui_document_create()};
    require_document_state(document);
    return document;
  });
  m.def("tc_ui_document_destroy", [](termin::gui_native::TcDocument &self) {
    const tc_ui_document_handle handle = checked_document_handle(self);
    release_document_state(self);
    tc_ui_document_destroy(handle);
    self = termin::gui_native::TcDocument{};
  });

  nb::class_<termin::gui_native::TcDocument>(m, "TcDocument")
      .def("__eq__",
           [](const termin::gui_native::TcDocument &self,
              const termin::gui_native::TcDocument &other) {
             return self == other;
           })
      .def_prop_ro("valid",
                   [](const termin::gui_native::TcDocument &self) {
                     return self.valid();
                   })
      .def_prop_rw(
          "debug_name",
          [](const termin::gui_native::TcDocument &self) {
            const char *name = tc_ui_document_debug_name(checked_document_handle(self));
            return std::string(name ? name : "");
          },
          [](termin::gui_native::TcDocument &self, const std::string &name) {
            if (!tc_ui_document_set_debug_name(checked_document_handle(self), name.c_str()))
              throw std::runtime_error("failed to set native UI document debug name");
          })
      .def("inspect_snapshot", &document_snapshot_to_python)
      .def("serialize", &document_serialize)
      .def("restore", &document_restore, nb::arg("serialized"))
      .def_prop_rw(
          "theme",
          [](const termin::gui_native::TcDocument &self) {
            return Theme{*tc_ui_document_theme(checked_document_handle(self))};
          },
          [](termin::gui_native::TcDocument &self, const Theme &theme) {
            if (!tc_ui_document_set_theme(checked_document_handle(self), &theme.value)) {
              throw std::invalid_argument("invalid native UI theme");
            }
          })
      .def_prop_ro("theme_revision",
                   [](const termin::gui_native::TcDocument &self) {
                     return tc_ui_document_theme_revision(checked_document_handle(self));
                   })
      .def("adopt", &document_adopt,
          nb::arg("widget"), nb::arg("debug_name") = "")
      .def("create_registered_widget", &document_create_registered_widget,
           nb::arg("type_name"))
      .def(
          "add_root",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_add_root(checked_document_handle(self), handle.handle);
          },
          nb::arg("handle"))
      .def(
          "adopt_root",
          [](termin::gui_native::TcDocument &self, nb::object widget, const std::string &debug_name) {
            WidgetHandle handle = document_adopt(self, std::move(widget), debug_name);
            if (!tc_ui_document_add_root(checked_document_handle(self), handle.handle)) {
              tc_ui_document_destroy_widget(checked_document_handle(self), handle.handle);
              throw_pending_document_exception(self);
              throw std::runtime_error("failed to add Python widget root");
            }
            return handle;
          },
          nb::arg("widget"), nb::arg("debug_name") = "")
      .def(
          "remove_root",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_remove_root(checked_document_handle(self), handle.handle);
          },
          nb::arg("handle"))
      .def(
          "destroy_widget",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            bool destroyed =
                tc_ui_document_destroy_widget(checked_document_handle(self), handle.handle);
            throw_pending_document_exception(self);
            return destroyed;
          },
          nb::arg("handle"))
      .def(
          "destroy_widget_recursive",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            bool destroyed = tc_ui_document_destroy_widget_recursive(
                checked_document_handle(self), handle.handle);
            throw_pending_document_exception(self);
            return destroyed;
          },
          nb::arg("handle"))
      .def(
          "is_alive",
          [](const termin::gui_native::TcDocument &self, WidgetHandle handle) {
            if (!self.valid())
              return false;
            return tc_ui_document_is_alive(checked_document_handle(self), handle.handle);
          },
          nb::arg("handle"))
      .def_prop_ro("live_widget_count",
                   [](const termin::gui_native::TcDocument &self) {
                     return tc_ui_document_live_widget_count(checked_document_handle(self));
                   })
      .def_prop_ro("root_count",
                   [](const termin::gui_native::TcDocument &self) {
                     return tc_ui_document_root_count(checked_document_handle(self));
                   })
      .def(
          "root_at",
          [](const termin::gui_native::TcDocument &self, size_t index) {
            return WidgetHandle{tc_ui_document_root_at(checked_document_handle(self), index)};
          },
          nb::arg("index"))
      .def("ref", &document_ref, nb::arg("handle"))
      .def(
          "measure_text",
          [](termin::gui_native::TcDocument &self, const std::string &text, float font_size) {
            tc_ui_text_metrics metrics{};
            if (!tc_ui_document_measure_text(checked_document_handle(self), text.data(),
                                             text.size(), font_size,
                                             &metrics)) {
              throw std::runtime_error("text measurement failed");
            }
            return metrics;
          },
          nb::arg("text"), nb::arg("font_size"))
      .def("set_clipboard_handlers", &set_document_clipboard_handlers,
           nb::arg("getter"), nb::arg("setter"))
      .def("set_cursor_changed_handler", &set_document_cursor_changed_handler,
           nb::arg("handler").none())
      .def(
          "create_hstack",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return document_make_native<termin::gui_native::HStack>(self,
                debug_name.c_str());
          },
          nb::arg("debug_name") = "HStack")
      .def(
          "create_vstack",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return document_make_native<termin::gui_native::VStack>(self,
                debug_name.c_str());
          },
          nb::arg("debug_name") = "VStack")
      .def(
          "create_panel",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return document_make_native<termin::gui_native::Panel>(self,
                debug_name.c_str());
          },
          nb::arg("debug_name") = "Panel")
      .def(
          "create_label",
          [](termin::gui_native::TcDocument &self, const std::string &text,
             const std::string &debug_name) {
            WidgetRef result =
                document_make_native<termin::gui_native::Label>(self, text);
            termin::gui_native::Widget *widget =
                static_cast<termin::gui_native::Widget *>(
                    result.resolve_checked()->body);
            widget->set_debug_name(debug_name);
            return result;
          },
          nb::arg("text"), nb::arg("debug_name") = "Label")
      .def(
          "create_button",
          [](termin::gui_native::TcDocument &self, const std::string &text,
             const std::string &debug_name) {
            ButtonRef result{
                document_make_native<termin::gui_native::Button>(self, text)};
            termin::gui_native::Widget *widget =
                static_cast<termin::gui_native::Widget *>(
                    result.widget.resolve_checked()->body);
            widget->set_debug_name(debug_name);
            return result;
          },
          nb::arg("text") = "", nb::arg("debug_name") = "Button")
      .def(
          "create_checkbox",
          [](termin::gui_native::TcDocument &self, bool checked) {
            return CheckboxRef{
                document_make_native<termin::gui_native::Checkbox>(self, checked)};
          },
          nb::arg("checked") = false)
      .def(
          "create_group_box",
          [](termin::gui_native::TcDocument &self, const std::string &title,
             const std::string &debug_name) {
            return GroupBoxRef{
                document_make_native<termin::gui_native::GroupBox>(self,
                    title, debug_name.c_str())};
          },
          nb::arg("title") = "", nb::arg("debug_name") = "GroupBox")
      .def(
          "create_scroll_area",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return ScrollAreaRef{
                document_make_native<termin::gui_native::ScrollArea>(self,
                    debug_name.c_str())};
          },
          nb::arg("debug_name") = "ScrollArea")
      .def(
          "create_splitter",
          [](termin::gui_native::TcDocument &self, bool horizontal, const std::string &debug_name) {
            return SplitterRef{document_make_native<termin::gui_native::Splitter>(self,
                horizontal ? termin::gui_native::Orientation::Horizontal
                           : termin::gui_native::Orientation::Vertical,
                debug_name.c_str())};
          },
          nb::arg("horizontal") = true, nb::arg("debug_name") = "Splitter")
      .def(
          "create_tab_view",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return TabViewRef{document_make_native<termin::gui_native::TabView>(self,
                debug_name.c_str())};
          },
          nb::arg("debug_name") = "TabView")
      .def(
          "create_text_input",
          [](termin::gui_native::TcDocument &self, const std::string &text) {
            return TextInputRef{
                document_make_native<termin::gui_native::TextInput>(self, text)};
          },
          nb::arg("text") = "")
      .def(
          "create_text_area",
          [](termin::gui_native::TcDocument &self, const std::string &text) {
            return TextAreaRef{
                document_make_native<termin::gui_native::TextArea>(self, text)};
          },
          nb::arg("text") = "")
      .def(
          "create_rich_text_view",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::RichTextModel> model) {
            return RichTextViewRef{
                document_make_native<termin::gui_native::RichTextView>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_frame_time_graph",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::FrameTimeModel> model) {
            return FrameTimeGraphRef{
                document_make_native<termin::gui_native::FrameTimeGraph>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_frame_timeline",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::FrameTimelineModel> model) {
            return FrameTimelineWidgetRef{
                document_make_native<termin::gui_native::FrameTimelineWidget>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def("create_viewport3d",
           [](termin::gui_native::TcDocument &self) {
             return Viewport3DRef{
                 document_make_native<termin::gui_native::Viewport3D>(self)};
           })
      .def(
          "create_overlay_layout",
          [](termin::gui_native::TcDocument &self, const std::string &debug_name) {
            return OverlayLayoutRef{
                document_make_native<termin::gui_native::OverlayLayout>(self,
                    debug_name.c_str())};
          },
          nb::arg("debug_name") = "OverlayLayout")
      .def(
          "create_scene_view",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::GraphicsScene> scene) {
            return SceneViewRef{document_make_native<termin::gui_native::SceneView>(self,
                std::move(scene))};
          },
          nb::arg("scene") = nullptr)
      .def(
          "create_spin_box",
          [](termin::gui_native::TcDocument &self, float value) {
            return SpinBoxRef{
                document_make_native<termin::gui_native::SpinBox>(self, value)};
          },
          nb::arg("value") = 0.0f)
      .def(
          "create_slider_edit",
          [](termin::gui_native::TcDocument &self, float value) {
            return SliderEditRef{
                document_make_native<termin::gui_native::SliderEdit>(self, value)};
          },
          nb::arg("value") = 0.0f)
      .def(
          "create_list_widget",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::CollectionModel> model) {
            return ListWidgetRef{
                document_make_native<termin::gui_native::ListWidget>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_file_grid_widget",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::CollectionModel> model) {
            return FileGridWidgetRef{
                document_make_native<termin::gui_native::FileGridWidget>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_tool_bar",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::CommandModel> model) {
            return ToolBarRef{document_make_native<termin::gui_native::ToolBar>(self,
                std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_status_bar",
          [](termin::gui_native::TcDocument &self, const std::string &text) {
            return StatusBarRef{
                document_make_native<termin::gui_native::StatusBar>(self, text)};
          },
          nb::arg("text") = "Ready")
      .def(
          "create_menu",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::CommandModel> model) {
            return MenuRef{
                document_make_native<termin::gui_native::Menu>(self, std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def("create_menu_bar",
           [](termin::gui_native::TcDocument &self) {
             return MenuBarRef{
                 document_make_native<termin::gui_native::MenuBar>(self)};
           })
      .def(
          "create_dialog",
          [](termin::gui_native::TcDocument &self, const std::string &title) {
            return DialogRef{
                document_make_native<termin::gui_native::Dialog>(self, title)};
          },
          nb::arg("title") = "")
      .def(
          "create_message_box",
          [](termin::gui_native::TcDocument &self, const std::string &title,
             const std::string &message,
             termin::gui_native::MessageBoxKind kind) {
            return MessageBoxRef{
                document_make_native<termin::gui_native::MessageBox>(self, title, message,
                                                                 kind)};
          },
          nb::arg("title"), nb::arg("message"),
          nb::arg("kind") = termin::gui_native::MessageBoxKind::Information)
      .def(
          "create_input_dialog",
          [](termin::gui_native::TcDocument &self, const std::string &title,
             const std::string &message, const std::string &value) {
            return InputDialogRef{
                document_make_native<termin::gui_native::InputDialog>(self,
                    title, message, value)};
          },
          nb::arg("title"), nb::arg("message") = "", nb::arg("value") = "")
      .def(
          "create_file_dialog",
          [](termin::gui_native::TcDocument &self, termin::gui_native::FileDialogMode mode) {
            return FileDialogOverlayRef{
                document_make_native<termin::gui_native::FileDialogOverlay>(self, mode)};
          },
          nb::arg("mode"))
      .def(
          "create_color_picker",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::ColorPickerModel> model) {
            return ColorPickerRef{
                document_make_native<termin::gui_native::ColorPicker>(self,
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def(
          "create_color_dialog",
          [](termin::gui_native::TcDocument &self, std::optional<tc_ui_color> initial,
             bool show_alpha, const std::string &title) {
            const tc_ui_color resolved_initial =
                initial.value_or(tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f});
            return ColorDialogRef{
                document_make_native<termin::gui_native::ColorDialog>(self,
                    termin::gui_native::Color{
                        resolved_initial.r, resolved_initial.g,
                        resolved_initial.b, resolved_initial.a},
                    show_alpha, title)};
          },
          nb::arg("initial").none() = nb::none(), nb::arg("show_alpha") = true,
          nb::arg("title") = "Color Picker")
      .def(
          "create_tree_widget",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::TreeModel> model,
             std::shared_ptr<termin::gui_native::TreeExpansionModel>
                 expansion) {
            return TreeWidgetRef{
                document_make_native<termin::gui_native::TreeWidget>(self,
                    std::move(model), std::move(expansion))};
          },
          nb::arg("model") = nullptr, nb::arg("expansion") = nullptr)
      .def(
          "create_tree_table_widget",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::TreeTableModel> model,
             std::shared_ptr<termin::gui_native::TableColumnModel> columns,
             std::shared_ptr<termin::gui_native::TreeExpansionModel>
                 expansion) {
            return TreeTableWidgetRef{
                document_make_native<termin::gui_native::TreeTableWidget>(self,
                    std::move(model), std::move(columns),
                    std::move(expansion))};
          },
          nb::arg("model") = nullptr, nb::arg("columns") = nullptr,
          nb::arg("expansion") = nullptr)
      .def(
          "create_table_widget",
          [](termin::gui_native::TcDocument &self,
             std::shared_ptr<termin::gui_native::TableModel> model,
             std::shared_ptr<termin::gui_native::TableColumnModel> columns) {
            return TableWidgetRef{
                document_make_native<termin::gui_native::TableWidget>(self,
                    std::move(model), std::move(columns))};
          },
          nb::arg("model") = nullptr, nb::arg("columns") = nullptr)
      .def("create_combo_box",
           [](termin::gui_native::TcDocument &self) {
             return ComboBoxRef{
                 document_make_native<termin::gui_native::ComboBox>(self)};
           })
      .def(
          "create_icon_button",
          [](termin::gui_native::TcDocument &self, const std::string &icon) {
            return IconButtonRef{
                document_make_native<termin::gui_native::IconButton>(self, icon)};
          },
          nb::arg("icon") = "")
      .def(
          "create_progress_bar",
          [](termin::gui_native::TcDocument &self, float value) {
            return ProgressBarRef{
                document_make_native<termin::gui_native::ProgressBar>(self, value)};
          },
          nb::arg("value") = 0.0f)
      .def("create_image_widget",
           [](termin::gui_native::TcDocument &self) {
             return ImageWidgetRef{
                 document_make_native<termin::gui_native::ImageWidget>(self)};
           })
      .def("create_canvas",
           [](termin::gui_native::TcDocument &self) {
             return CanvasRef{
                 document_make_native<termin::gui_native::Canvas>(self)};
           })
      .def(
          "layout_roots",
          [](termin::gui_native::TcDocument &self, tc_ui_rect rect) {
            tc_ui_document_layout_roots(checked_document_handle(self), rect);
            throw_pending_document_exception(self);
          },
          nb::arg("rect"))
      .def(
          "paint_roots",
          [](termin::gui_native::TcDocument &self, PaintContext &context) {
            tc_ui_document_paint_roots(checked_document_handle(self), context.get());
            throw_pending_document_exception(self);
          },
          nb::arg("context"))
      .def(
          "paint",
          [](termin::gui_native::TcDocument &self, PaintContext &context) {
            tc_ui_document_paint(checked_document_handle(self), context.get());
            throw_pending_document_exception(self);
          },
          nb::arg("context"))
      .def(
          "show_overlay",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle, uint32_t flags) {
            bool shown =
                tc_ui_document_show_overlay(checked_document_handle(self), handle.handle, flags);
            throw_pending_document_exception(self);
            return shown;
          },
          nb::arg("handle"), nb::arg("flags") = 0)
      .def(
          "dismiss_overlay",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle,
             tc_ui_overlay_dismiss_reason reason) {
            bool dismissed = tc_ui_document_dismiss_overlay(
                checked_document_handle(self), handle.handle, reason);
            throw_pending_document_exception(self);
            return dismissed;
          },
          nb::arg("handle"),
          nb::arg("reason") = TC_UI_OVERLAY_DISMISS_PROGRAMMATIC)
      .def_prop_ro("overlay_count",
                   [](const termin::gui_native::TcDocument &self) {
                     return tc_ui_document_overlay_count(checked_document_handle(self));
                   })
      .def(
          "overlay_at",
          [](const termin::gui_native::TcDocument &self, size_t index) {
            return WidgetHandle{tc_ui_document_overlay_at(checked_document_handle(self), index)};
          },
          nb::arg("index"))
      .def(
          "overlay_flags_at",
          [](const termin::gui_native::TcDocument &self, size_t index) {
            return tc_ui_document_overlay_flags_at(checked_document_handle(self), index);
          },
          nb::arg("index"))
      .def(
          "hit_test",
          [](termin::gui_native::TcDocument &self, float x, float y) {
            tc_widget_handle handle = tc_ui_document_hit_test(checked_document_handle(self), x, y);
            throw_pending_document_exception(self);
            return WidgetHandle{handle};
          },
          nb::arg("x"), nb::arg("y"))
      .def(
          "dispatch_pointer_event",
          [](termin::gui_native::TcDocument &self, const tc_ui_pointer_event &event) {
            tc_ui_event_result result =
                tc_ui_document_dispatch_pointer_event(checked_document_handle(self), &event);
            throw_pending_document_exception(self);
            return result;
          },
          nb::arg("event"))
      .def(
          "dispatch_key_event",
          [](termin::gui_native::TcDocument &self, const tc_ui_key_event &event) {
            tc_ui_event_result result =
                tc_ui_document_dispatch_key_event(checked_document_handle(self), &event);
            throw_pending_document_exception(self);
            return result;
          },
          nb::arg("event"))
      .def(
          "dispatch_text_event",
          [](termin::gui_native::TcDocument &self, const std::string &text) {
            const tc_ui_text_event event{text.c_str()};
            tc_ui_event_result result =
                tc_ui_document_dispatch_text_event(checked_document_handle(self), &event);
            throw_pending_document_exception(self);
            return result;
          },
          nb::arg("text"))
      .def_prop_ro("hovered_widget",
                   [](const termin::gui_native::TcDocument &self) {
                     return WidgetHandle{
                         tc_ui_document_hovered_widget(checked_document_handle(self))};
                   })
      .def_prop_ro("cursor_intent",
                   [](const termin::gui_native::TcDocument &self) {
                     return tc_ui_document_cursor_intent(checked_document_handle(self));
                   })
      .def_prop_ro("pointer_capture",
                   [](const termin::gui_native::TcDocument &self) {
                     return WidgetHandle{
                         tc_ui_document_pointer_capture(checked_document_handle(self))};
                   })
      .def_prop_ro("pressed_widget",
                   [](const termin::gui_native::TcDocument &self) {
                     return WidgetHandle{
                         tc_ui_document_pressed_widget(checked_document_handle(self))};
                   })
      .def(
          "set_pointer_capture",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_set_pointer_capture(checked_document_handle(self),
                                                      handle.handle);
          },
          nb::arg("handle"))
      .def(
          "release_pointer_capture",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_release_pointer_capture(checked_document_handle(self),
                                                          handle.handle);
          },
          nb::arg("handle"))
      .def_prop_ro("focused_widget",
                   [](const termin::gui_native::TcDocument &self) {
                     return WidgetHandle{
                         tc_ui_document_focused_widget(checked_document_handle(self))};
                   })
      .def(
          "set_focus",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_set_focus(checked_document_handle(self), handle.handle);
          },
          nb::arg("handle"))
      .def(
          "clear_focus",
          [](termin::gui_native::TcDocument &self, WidgetHandle handle) {
            return tc_ui_document_clear_focus(checked_document_handle(self), handle.handle);
          },
          nb::arg("handle"))
      .def("focus_next",
           [](termin::gui_native::TcDocument &self) { return tc_ui_document_focus_next(checked_document_handle(self)); })
      .def("focus_previous", [](termin::gui_native::TcDocument &self) {
        return tc_ui_document_focus_previous(checked_document_handle(self));
      });

  nb::class_<termin::gui_native::UiDrawListRenderer>(m, "DrawListRenderer")
      .def(nb::init<>())
      .def("set_default_font_path",
           &termin::gui_native::UiDrawListRenderer::set_default_font_path,
           nb::arg("path"), nb::arg("default_size_px") = 14)
      .def(
          "bind_text_measurer",
          [](termin::gui_native::UiDrawListRenderer &self, termin::gui_native::TcDocument &document) {
            self.bind_text_measurer(checked_document_handle(document));
          },
          nb::arg("document"), nb::keep_alive<2, 1>())
      .def(
          "sync_color_picker_surfaces",
          [](termin::gui_native::UiDrawListRenderer &self,
             tgfx::RenderContext2 &context, const ColorPickerRef &picker) {
            self.sync_color_picker_surfaces(context, picker.get());
          },
          nb::arg("context"), nb::arg("picker"))
      .def(
          "release_color_picker_surfaces",
          [](termin::gui_native::UiDrawListRenderer &self,
             const ColorPickerRef &picker) {
            self.release_color_picker_surfaces(picker.get());
          },
          nb::arg("picker"))
      .def(
          "render",
          [](termin::gui_native::UiDrawListRenderer &self,
             tgfx::RenderContext2 &context, const DrawList &draw_list,
             int width, int height) {
            self.render(context, draw_list.get(), width, height);
          },
          nb::arg("context"), nb::arg("draw_list"), nb::arg("width"),
          nb::arg("height"))
      .def("release_gpu", &termin::gui_native::UiDrawListRenderer::release_gpu);
}
