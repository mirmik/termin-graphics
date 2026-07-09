#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include <tcbase/tc_log.h>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/tc_ui_document.h>
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

struct PythonWidget {
    uint32_t magic = PYTHON_WIDGET_MAGIC;
    tc_widget widget {};
    nb::object object;
    std::string debug_name;
    std::exception_ptr pending_exception;

    explicit PythonWidget(nb::object object_, std::string debug_name_)
        : object(std::move(object_)), debug_name(std::move(debug_name_)) {
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

    static const PythonWidget* from_widget_const(const tc_widget* widget) {
        if (!widget || !widget->body) {
            return nullptr;
        }
        auto* self = static_cast<const PythonWidget*>(widget->body);
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
        delete self;
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
            self->pending_exception = std::current_exception();
            tc_log_error(
                "[termin-gui-native/python] Python widget paint failed for '%s'",
                self->debug_name.empty() ? "<unnamed>" : self->debug_name.c_str()
            );
        }
    }

    static const tc_widget_vtable VTABLE;
};

const tc_widget_vtable PythonWidget::VTABLE {
    "PythonWidget",
    nullptr,
    nullptr,
    &PythonWidget::paint,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

class Document {
public:
    Document() : document_(tc_ui_document_create()) {
        if (!document_) {
            throw std::runtime_error("failed to create tc_ui_document");
        }
    }

    ~Document() {
        tc_ui_document_destroy(document_);
    }

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    tc_ui_document* get() const {
        return document_;
    }

    WidgetHandle adopt(nb::object object, const std::string& debug_name) {
        auto widget = std::make_unique<PythonWidget>(std::move(object), debug_name);
        tc_widget_handle handle = tc_ui_document_adopt_widget(document_, &widget->widget);
        if (tc_widget_handle_is_invalid(handle)) {
            throw std::runtime_error("failed to adopt Python widget");
        }
        widget.release();
        return WidgetHandle {handle};
    }

    void throw_pending_root_exception() {
        const size_t count = tc_ui_document_root_count(document_);
        for (size_t i = 0; i < count; ++i) {
            const tc_widget_handle handle = tc_ui_document_root_at(document_, i);
            const tc_widget* widget = tc_ui_document_resolve_widget_const(document_, handle);
            const PythonWidget* python_widget = PythonWidget::from_widget_const(widget);
            if (!python_widget || !python_widget->pending_exception) {
                continue;
            }
            std::exception_ptr exception = python_widget->pending_exception;
            const_cast<PythonWidget*>(python_widget)->pending_exception = nullptr;
            std::rethrow_exception(exception);
        }
    }

private:
    tc_ui_document* document_ = nullptr;
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
                throw std::runtime_error("failed to add Python widget root");
            }
            return handle;
        }, nb::arg("widget"), nb::arg("debug_name") = "")
        .def("remove_root", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_remove_root(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("destroy_widget", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_destroy_widget(self.get(), handle.handle);
        }, nb::arg("handle"))
        .def("destroy_widget_recursive", [](Document& self, WidgetHandle handle) {
            return tc_ui_document_destroy_widget_recursive(self.get(), handle.handle);
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
        .def("paint_roots", [](Document& self, PaintContext& context) {
            tc_ui_document_paint_roots(self.get(), context.get());
            self.throw_pending_root_exception();
        }, nb::arg("context"));

    nb::class_<termin::gui_native::UiDrawListRenderer>(m, "DrawListRenderer")
        .def(nb::init<>())
        .def("set_default_font_path", &termin::gui_native::UiDrawListRenderer::set_default_font_path,
             nb::arg("path"), nb::arg("default_size_px") = 14)
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
