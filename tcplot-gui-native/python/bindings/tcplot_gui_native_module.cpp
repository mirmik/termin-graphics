#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include <stdexcept>

#include <tcplot/gui_native/plot3d.hpp>
#include <tcplot/gui_native/widget_registration.hpp>
#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widget.hpp>

namespace nb = nanobind;

namespace {

    using Array = nb::ndarray<double, nb::c_contig, nb::device::cpu>;

    class Plot3DAccess {
    public:
        Plot3DAccess(termin::gui_native::TcDocument document, uint32_t index, uint32_t generation)
            : document_(document),
              handle_{index, generation} {
            (void)get();
        }

        tcplot::gui_native::Plot3D& get() const {
            tc_widget* widget = tc_ui_document_resolve_widget(document_.handle(), handle_);
            if (!widget || widget->native_language != TC_LANGUAGE_CXX || !widget->body) {
                throw std::runtime_error("tcplot Plot3D access refers to a stale or incompatible widget");
            }
            auto* base = static_cast<termin::gui_native::Widget*>(widget->body);
            auto* plot = dynamic_cast<tcplot::gui_native::Plot3D*>(base);
            if (!plot) {
                throw std::runtime_error("native widget is not termin.gui.Plot3D");
            }
            return *plot;
        }

        nb::tuple add_line(Array x,
                           Array y,
                           Array z,
                           float r,
                           float g,
                           float b,
                           float a,
                           float thickness) const {
            const tc_line_item3d_style style{r, g, b, a, thickness};
            return item_tuple(get().add_line(array_span(x), array_span(y), array_span(z), style));
        }

        nb::tuple add_scatter(Array x,
                              Array y,
                              Array z,
                              float r,
                              float g,
                              float b,
                              float a,
                              float size) const {
            const tc_scatter_item3d_style style{r, g, b, a, size};
            return item_tuple(get().add_scatter(array_span(x), array_span(y), array_span(z), style));
        }

        nb::tuple add_surface(Array x,
                              Array y,
                              Array z,
                              uint32_t rows,
                              uint32_t columns,
                              float r,
                              float g,
                              float b,
                              float a,
                              uint32_t colormap,
                              bool wireframe) const {
            tc_surface_item3d_style style{};
            style.color_r = r;
            style.color_g = g;
            style.color_b = b;
            style.color_a = a;
            style.colormap = colormap;
            style.wireframe = wireframe ? 1u : 0u;
            style.surface_grid_row_step = 8;
            style.surface_grid_col_step = 8;
            style.surface_grid_width_px = 1.5f;
            style.surface_grid_r = 0.05f;
            style.surface_grid_g = 0.05f;
            style.surface_grid_b = 0.05f;
            style.surface_grid_a = 1.0f;
            return item_tuple(
                get().add_surface(array_span(x), array_span(y), array_span(z), rows, columns, style));
        }

        bool destroy_item(uint64_t scene_id, uint32_t index, uint32_t generation) const {
            return get().destroy_item({scene_id, index, generation});
        }

    private:
        static std::span<const double> array_span(const Array& value) {
            return {value.data(), value.size()};
        }

        static nb::tuple item_tuple(tc_plot_item3d_handle item) {
            return nb::make_tuple(item.scene_id, item.index, item.generation);
        }

        termin::gui_native::TcDocument document_;
        tc_widget_handle handle_{};
    };

} // namespace

NB_MODULE(_tcplot_gui_native, module) {
    nb::module_::import_("termin.gui_native._gui_native");
    if (!tcplot::gui_native::register_plot_widget_types()) {
        throw std::runtime_error("failed to register tcplot native widget types");
    }

    nb::class_<Plot3DAccess>(module, "Plot3DAccess")
        .def(nb::init<termin::gui_native::TcDocument, uint32_t, uint32_t>())
        .def("add_line", &Plot3DAccess::add_line)
        .def("add_scatter", &Plot3DAccess::add_scatter)
        .def("add_surface", &Plot3DAccess::add_surface)
        .def("destroy_item", &Plot3DAccess::destroy_item)
        .def("clear", [](const Plot3DAccess& self) { self.get().clear(); })
        .def("set_axis_labels",
             [](const Plot3DAccess& self, const std::string& x, const std::string& y, const std::string& z) {
                 self.get().set_axis_labels(x.c_str(), y.c_str(), z.c_str());
             })
        .def("set_axis_scale", [](const Plot3DAccess& self, float x, float y, float z) {
            if (!self.get().set_axis_scale(x, y, z))
                throw std::runtime_error("tcplot Plot3D rejected axis scale");
        })
        .def("set_surface_shading", [](const Plot3DAccess& self, bool enabled, float strength) {
            if (!self.get().set_surface_shading(enabled, strength))
                throw std::runtime_error("tcplot Plot3D rejected surface shading");
        }, nb::arg("enabled"), nb::arg("strength") = 0.35f)
        .def("set_light_direction", [](const Plot3DAccess& self, float x, float y, float z) {
            if (!self.get().set_light_direction(x, y, z))
                throw std::runtime_error("tcplot Plot3D rejected light direction");
        })
        .def("fit_camera", [](const Plot3DAccess& self) { self.get().fit_camera(); })
        .def("reset_camera", [](const Plot3DAccess& self) { self.get().reset_camera(); })
        .def_prop_ro("scene_id", [](const Plot3DAccess& self) { return self.get().scene_id(); })
        .def_prop_ro("item_count", [](const Plot3DAccess& self) { return self.get().item_count(); })
        .def_prop_ro("texture_id", [](const Plot3DAccess& self) { return self.get().texture_id(); });
}
