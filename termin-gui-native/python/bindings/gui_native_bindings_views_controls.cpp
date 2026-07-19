#include "gui_native_bindings_shared.hpp"

using namespace termin::gui_native::python_bindings;

void bind_gui_native_control_views(nb::module_ &m) {
  nb::class_<ComboBoxRef>(m, "ComboBox")
      .def_prop_ro("widget",
                   [](const ComboBoxRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const ComboBoxRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_ro(
          "item_count",
          [](const ComboBoxRef &self) { return self.get().item_count(); })
      .def_prop_rw(
          "selected_index",
          [](const ComboBoxRef &self) { return self.get().selected_index(); },
          [](const ComboBoxRef &self, int value) {
            self.get().set_selected_index(value);
            self.widget.throw_pending_exception();
          })
      .def_prop_ro(
          "selected_text",
          [](const ComboBoxRef &self) { return self.get().selected_text(); })
      .def_prop_ro("open",
                   [](const ComboBoxRef &self) { return self.get().open(); })
      .def(
          "add_item",
          [](const ComboBoxRef &self, const std::string &item) {
            self.get().add_item(item);
          },
          nb::arg("item"))
      .def(
          "item_text",
          [](const ComboBoxRef &self, size_t index) {
            return self.get().item_text(index);
          },
          nb::arg("index"))
      .def("clear", [](const ComboBoxRef &self) { self.get().clear_items(); })
      .def(
          "connect_changed",
          [](const ComboBoxRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().changed().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::ComboBox &, int index,
                    const std::string &text) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(index, text);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] ComboBox changed "
                                 "callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<IconButtonRef>(m, "IconButton")
      .def_prop_ro("widget",
                   [](const IconButtonRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const IconButtonRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "active",
          [](const IconButtonRef &self) { return self.get().active(); },
          [](const IconButtonRef &self, bool value) {
            self.get().set_active(value);
          })
      .def(
          "set_icon",
          [](const IconButtonRef &self, const std::string &icon) {
            self.get().set_icon(icon);
          },
          nb::arg("icon"))
      .def(
          "set_texture",
          [](const IconButtonRef &self, tgfx::TextureHandle texture) {
            self.get().set_texture(texture.id);
          },
          nb::arg("texture"))
      .def(
          "connect_clicked",
          [](const IconButtonRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().clicked().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::IconButton &) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback();
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] IconButton click "
                                 "callback failed");
                  }
                });
          },
          nb::arg("callback"));

  nb::class_<ProgressBarRef>(m, "ProgressBar")
      .def_prop_ro("widget",
                   [](const ProgressBarRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const ProgressBarRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_rw(
          "value",
          [](const ProgressBarRef &self) { return self.get().value(); },
          [](const ProgressBarRef &self, float value) {
            self.get().set_value(value);
          });

  nb::class_<ImageWidgetRef>(m, "ImageWidget")
      .def_prop_ro("widget",
                   [](const ImageWidgetRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const ImageWidgetRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_ro("intrinsic_size",
                   [](const ImageWidgetRef &self) {
                     return self.get().intrinsic_size();
                   })
      .def(
          "set_texture",
          [](const ImageWidgetRef &self, tgfx::TextureHandle texture,
             std::optional<tc_ui_size> intrinsic_size) {
            self.get().set_texture(texture.id,
                                   intrinsic_size.value_or(tc_ui_size{}));
          },
          nb::arg("texture"), nb::arg("intrinsic_size").none() = nb::none())
      .def("clear_texture",
           [](const ImageWidgetRef &self) { self.get().clear_texture(); })
      .def(
          "set_tint",
          [](const ImageWidgetRef &self, tc_ui_color tint) {
            self.get().set_tint(
                termin::gui_native::Color{tint.r, tint.g, tint.b, tint.a});
          },
          nb::arg("tint"))
      .def_prop_rw(
          "fit",
          [](const ImageWidgetRef &self) { return self.get().fit(); },
          [](const ImageWidgetRef &self, termin::gui_native::ImageFit fit) {
            self.get().set_fit(fit);
          })
      .def(
          "set_preserve_aspect",
          [](const ImageWidgetRef &self, bool preserve) {
            self.get().set_preserve_aspect(preserve);
          },
          nb::arg("preserve"));

  nb::class_<CanvasRef>(m, "Canvas")
      .def_prop_ro("widget", [](const CanvasRef &self) { return self.widget; })
      .def_prop_ro("handle",
                   [](const CanvasRef &self) {
                     return WidgetHandle{self.widget.handle};
                   })
      .def_prop_ro("zoom",
                   [](const CanvasRef &self) { return self.get().zoom(); })
      .def_prop_ro("fit_mode",
                   [](const CanvasRef &self) { return self.get().fit_mode(); })
      .def_prop_rw(
          "texture_sampling",
          [](const CanvasRef &self) { return self.get().texture_sampling(); },
          [](const CanvasRef &self, tc_ui_texture_sampling sampling) {
            self.get().set_texture_sampling(sampling);
          })
      .def(
          "set_texture",
          [](const CanvasRef &self, tgfx::TextureHandle texture,
             std::optional<tc_ui_size> image_size) {
            self.get().set_texture(texture.id,
                                   image_size.value_or(tc_ui_size{}));
          },
          nb::arg("texture"), nb::arg("image_size").none() = nb::none())
      .def("clear_texture",
           [](const CanvasRef &self) { self.get().clear_texture(); })
      .def(
          "set_overlay_texture",
          [](const CanvasRef &self, tgfx::TextureHandle texture) {
            self.get().set_overlay_texture(texture.id);
          },
          nb::arg("texture"))
      .def(
          "set_zoom",
          [](const CanvasRef &self, float zoom, tc_ui_point anchor) {
            self.get().set_zoom(zoom, anchor);
          },
          nb::arg("zoom"), nb::arg("anchor"))
      .def("fit_in_view",
           [](const CanvasRef &self) { self.get().fit_in_view(); })
      .def(
          "widget_to_image",
          [](const CanvasRef &self, tc_ui_point point) {
            return self.get().widget_to_image(point);
          },
          nb::arg("point"))
      .def(
          "image_to_widget",
          [](const CanvasRef &self, tc_ui_point point) {
            return self.get().image_to_widget(point);
          },
          nb::arg("point"))
      .def(
          "connect_zoom_changed",
          [](const CanvasRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().zoom_changed().connect([state,
                                                      callback =
                                                          std::move(callback)](
                                                         termin::gui_native::
                                                             Canvas &,
                                                         float zoom) {
              try {
                nb::gil_scoped_acquire gil;
                callback(zoom);
              } catch (...) {
                if (state && !state->pending_exception)
                  state->pending_exception = std::current_exception();
                tc_log_error(
                    "[termin-gui-native/python] Canvas zoom callback failed");
              }
            });
          },
          nb::arg("callback"))
      .def(
          "connect_pointer_input",
          [](const CanvasRef &self, nb::object callback) {
            auto state = self.widget.state;
            return self.get().pointer_input().connect(
                [state, callback = std::move(callback)](
                    termin::gui_native::Canvas &, tc_ui_point image_point,
                    const tc_ui_pointer_event &event) {
                  try {
                    nb::gil_scoped_acquire gil;
                    callback(image_point, event);
                  } catch (...) {
                    if (state && !state->pending_exception)
                      state->pending_exception = std::current_exception();
                    tc_log_error("[termin-gui-native/python] Canvas pointer "
                                 "callback failed");
                  }
                });
          },
          nb::arg("callback"))
      .def(
          "set_paint_callback",
          [](const CanvasRef &self, nb::object callback) {
            if (callback.is_none()) {
              self.get().set_paint_callback({});
              return;
            }
            auto state = self.widget.state;
            self.get().set_paint_callback([state,
                                           callback = std::move(callback)](
                                              termin::gui_native::Canvas &,
                                              tc_ui_paint_context *context) {
              try {
                nb::gil_scoped_acquire gil;
                PaintContext borrowed(context, false);
                callback(std::move(borrowed));
              } catch (...) {
                if (state && !state->pending_exception)
                  state->pending_exception = std::current_exception();
                tc_log_error(
                    "[termin-gui-native/python] Canvas paint callback failed");
              }
            });
          },
          nb::arg("callback"));

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
}
