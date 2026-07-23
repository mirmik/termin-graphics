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

  nb::class_<Document>(m, "Document")
      .def(nb::init<>())
      .def("close", &Document::close)
      .def("__enter__", [](Document &self) -> Document & { return self; },
           nb::rv_policy::reference_internal)
      .def("__exit__",
           [](Document &self, nb::handle, nb::handle, nb::handle) {
             self.close();
             return false;
           })
      .def_prop_ro("closed", &Document::is_closed)
      .def_prop_rw(
          "debug_name",
          [](const Document &self) {
            const char *name = tc_ui_document_debug_name(self.get());
            return std::string(name ? name : "");
          },
          [](Document &self, const std::string &name) {
            if (!tc_ui_document_set_debug_name(self.get(), name.c_str()))
              throw std::runtime_error("failed to set native UI document debug name");
          })
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
          [](Document &self, nb::object widget, const std::string &debug_name) {
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
          [](Document &self, nb::object widget, const std::string &debug_name) {
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
            if (self.is_closed())
              return false;
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
      .def("set_cursor_changed_handler", &Document::set_cursor_changed_handler,
           nb::arg("handler").none())
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
          "create_group_box",
          [](Document &self, const std::string &title,
             const std::string &debug_name) {
            return GroupBoxRef{
                self.make_native<termin::gui_native::GroupBox>(
                    title, debug_name.c_str())};
          },
          nb::arg("title") = "", nb::arg("debug_name") = "GroupBox")
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
          [](Document &self, bool horizontal, const std::string &debug_name) {
            return SplitterRef{self.make_native<termin::gui_native::Splitter>(
                horizontal ? termin::gui_native::Orientation::Horizontal
                           : termin::gui_native::Orientation::Vertical,
                debug_name.c_str())};
          },
          nb::arg("horizontal") = true, nb::arg("debug_name") = "Splitter")
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
      .def(
          "create_frame_timeline",
          [](Document &self,
             std::shared_ptr<termin::gui_native::FrameTimelineModel> model) {
            return FrameTimelineWidgetRef{
                self.make_native<termin::gui_native::FrameTimelineWidget>(
                    std::move(model))};
          },
          nb::arg("model") = nullptr)
      .def("create_viewport3d",
           [](Document &self) {
             return Viewport3DRef{
                 self.make_native<termin::gui_native::Viewport3D>()};
           })
      .def(
          "create_overlay_layout",
          [](Document &self, const std::string &debug_name) {
            return OverlayLayoutRef{
                self.make_native<termin::gui_native::OverlayLayout>(
                    debug_name.c_str())};
          },
          nb::arg("debug_name") = "OverlayLayout")
      .def(
          "create_scene_view",
          [](Document &self,
             std::shared_ptr<termin::gui_native::GraphicsScene> scene) {
            return SceneViewRef{self.make_native<termin::gui_native::SceneView>(
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
             return MenuBarRef{self.make_native<termin::gui_native::MenuBar>()};
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
                self.make_native<termin::gui_native::MessageBox>(title, message,
                                                                 kind)};
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
                self.make_native<termin::gui_native::FileDialogOverlay>(mode)};
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
          [](Document &self, std::optional<tc_ui_color> initial,
             bool show_alpha, const std::string &title) {
            const tc_ui_color resolved_initial =
                initial.value_or(tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f});
            return ColorDialogRef{
                self.make_native<termin::gui_native::ColorDialog>(
                    termin::gui_native::Color{
                        resolved_initial.r, resolved_initial.g,
                        resolved_initial.b, resolved_initial.a},
                    show_alpha, title)};
          },
          nb::arg("initial").none() = nb::none(), nb::arg("show_alpha") = true,
          nb::arg("title") = "Color Picker")
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
          "create_tree_table_widget",
          [](Document &self,
             std::shared_ptr<termin::gui_native::TreeTableModel> model,
             std::shared_ptr<termin::gui_native::TableColumnModel> columns,
             std::shared_ptr<termin::gui_native::TreeExpansionModel>
                 expansion) {
            return TreeTableWidgetRef{
                self.make_native<termin::gui_native::TreeTableWidget>(
                    std::move(model), std::move(columns),
                    std::move(expansion))};
          },
          nb::arg("model") = nullptr, nb::arg("columns") = nullptr,
          nb::arg("expansion") = nullptr)
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
      .def(
          "create_progress_bar",
          [](Document &self, float value) {
            return ProgressBarRef{
                self.make_native<termin::gui_native::ProgressBar>(value)};
          },
          nb::arg("value") = 0.0f)
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
            tc_widget_handle handle = tc_ui_document_hit_test(self.get(), x, y);
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
      .def_prop_ro("cursor_intent",
                   [](const Document &self) {
                     return tc_ui_document_cursor_intent(self.get());
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
           [](Document &self) { return tc_ui_document_focus_next(self.get()); })
      .def("focus_previous", [](Document &self) {
        return tc_ui_document_focus_previous(self.get());
      });

  nb::class_<termin::gui_native::UiDrawListRenderer>(m, "DrawListRenderer")
      .def(nb::init<>())
      .def("set_default_font_path",
           &termin::gui_native::UiDrawListRenderer::set_default_font_path,
           nb::arg("path"), nb::arg("default_size_px") = 14)
      .def(
          "bind_text_measurer",
          [](termin::gui_native::UiDrawListRenderer &self, Document &document) {
            self.bind_text_measurer(document.get());
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
