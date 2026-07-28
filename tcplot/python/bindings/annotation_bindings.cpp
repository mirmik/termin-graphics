#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include "tcplot/plot_annotations2d.hpp"

namespace nb = nanobind;

namespace tcplot_bindings {

void bind_annotations(nb::module_& m) {
    nb::class_<tcplot::PlotAnnotationHandle>(m, "PlotAnnotationHandle")
        .def(nb::init<>())
        .def_ro("layer_id", &tcplot::PlotAnnotationHandle::layer_id)
        .def_ro("index", &tcplot::PlotAnnotationHandle::index)
        .def_ro("generation", &tcplot::PlotAnnotationHandle::generation)
        .def("__bool__", &tcplot::PlotAnnotationHandle::valid)
        .def("__eq__", [](const tcplot::PlotAnnotationHandle& self,
                          const tcplot::PlotAnnotationHandle& other) {
            return self == other;
        });

    nb::class_<tcplot::PlotDataMarker2D>(m, "PlotDataMarker2D")
        .def(nb::init<>())
        .def_prop_rw(
            "x",
            [](const tcplot::PlotDataMarker2D& self) {
                return self.data_position.x;
            },
            [](tcplot::PlotDataMarker2D& self, double value) {
                self.data_position.x = value;
            })
        .def_prop_rw(
            "y",
            [](const tcplot::PlotDataMarker2D& self) {
                return self.data_position.y;
            },
            [](tcplot::PlotDataMarker2D& self, double value) {
                self.data_position.y = value;
            })
        .def_rw("text", &tcplot::PlotDataMarker2D::text)
        .def_prop_rw(
            "callout_offset_x",
            [](const tcplot::PlotDataMarker2D& self) {
                return self.callout_offset.x;
            },
            [](tcplot::PlotDataMarker2D& self, float value) {
                self.callout_offset.x = value;
            })
        .def_prop_rw(
            "callout_offset_y",
            [](const tcplot::PlotDataMarker2D& self) {
                return self.callout_offset.y;
            },
            [](tcplot::PlotDataMarker2D& self, float value) {
                self.callout_offset.y = value;
            })
        .def_rw("callout_width", &tcplot::PlotDataMarker2D::callout_width)
        .def_rw("callout_height", &tcplot::PlotDataMarker2D::callout_height)
        .def_rw("anchor_radius", &tcplot::PlotDataMarker2D::anchor_radius)
        .def_rw("text_size", &tcplot::PlotDataMarker2D::text_size)
        .def_rw("close_button", &tcplot::PlotDataMarker2D::close_button);

    nb::class_<tcplot::PlotDataMarkerSnapshot2D>(
        m, "PlotDataMarkerSnapshot2D")
        .def_ro(
            "annotation",
            &tcplot::PlotDataMarkerSnapshot2D::annotation)
        .def_ro("marker", &tcplot::PlotDataMarkerSnapshot2D::marker)
        .def_ro("hovered", &tcplot::PlotDataMarkerSnapshot2D::hovered)
        .def_ro("dragging", &tcplot::PlotDataMarkerSnapshot2D::dragging);

    nb::class_<tcplot::PlotAnnotationAction2D>(
        m, "PlotAnnotationAction2D")
        .def_ro("annotation", &tcplot::PlotAnnotationAction2D::annotation)
        .def_ro("visual_index", &tcplot::PlotAnnotationAction2D::visual_index)
        .def_ro("pointer", &tcplot::PlotAnnotationAction2D::pointer)
        .def_ro("action", &tcplot::PlotAnnotationAction2D::action);
}

}  // namespace tcplot_bindings
