// graphics_bindings.cpp - Common graphics value types and render state
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "tgfx/render_state.hpp"
#include "tgfx/types.hpp"

namespace nb = nanobind;

using namespace termin;

namespace tgfx_bindings {

    void bind_types(nb::module_& m) {
        // Re-export the canonical base bindings. Registering the same C++
        // value type again in this extension would create a competing Python
        // identity and make cross-module typed APIs unreliable.
        nb::module_ geom = nb::module_::import_("tcbase._geom_native");
        m.attr("SrgbColor") = geom.attr("SrgbColor");
        m.attr("LinearColor") = geom.attr("LinearColor");

        // Size2i
        nb::class_<Size2i>(m, "Size2i")
            .def(nb::init<>())
            .def(nb::init<int, int>(), nb::arg("width"), nb::arg("height"))
            .def("__init__",
                 [](Size2i* self, nb::tuple t) {
                     if (t.size() != 2)
                         throw std::runtime_error("Size tuple must have 2 elements");
                     new (self) Size2i(nb::cast<int>(t[0]), nb::cast<int>(t[1]));
                 })
            .def_rw("width", &Size2i::width)
            .def_rw("height", &Size2i::height)
            .def("__iter__", [](const Size2i& s) { return nb::iter(nb::make_tuple(s.width, s.height)); })
            .def("__getitem__",
                 [](const Size2i& s, int i) {
                     if (i == 0)
                         return s.width;
                     if (i == 1)
                         return s.height;
                     throw nb::index_error();
                 })
            .def("__eq__", &Size2i::operator==)
            .def("__ne__", &Size2i::operator!=);

        // Bounds2i
        nb::class_<Bounds2i>(m, "Bounds2i")
            .def(nb::init<>())
            .def(nb::init<int, int, int, int>(), nb::arg("x0"), nb::arg("y0"), nb::arg("x1"), nb::arg("y1"))
            .def("__init__",
                 [](Bounds2i* self, nb::tuple t) {
                     if (t.size() != 4)
                         throw std::runtime_error("Rect tuple must have 4 elements");
                     new (self)
                         Bounds2i(nb::cast<int>(t[0]), nb::cast<int>(t[1]), nb::cast<int>(t[2]), nb::cast<int>(t[3]));
                 })
            .def_rw("x0", &Bounds2i::x0)
            .def_rw("y0", &Bounds2i::y0)
            .def_rw("x1", &Bounds2i::x1)
            .def_rw("y1", &Bounds2i::y1)
            .def("width", &Bounds2i::width)
            .def("height", &Bounds2i::height)
            .def_static("from_size", nb::overload_cast<int, int>(&Bounds2i::from_size))
            .def_static("from_size", nb::overload_cast<Size2i>(&Bounds2i::from_size))
            .def("__iter__", [](const Bounds2i& r) { return nb::iter(nb::make_tuple(r.x0, r.y0, r.x1, r.y1)); })
            .def("__getitem__", [](const Bounds2i& r, int i) {
                if (i < 0 || i > 3)
                    throw nb::index_error();
                return (&r.x0)[i];
            });

        // Geometry tuples remain supported for Size2i/Bounds2i only.
        nb::implicitly_convertible<nb::tuple, Size2i>();
        nb::implicitly_convertible<nb::tuple, Bounds2i>();
    }

    void bind_render_state(nb::module_& m) {
        nb::enum_<PolygonMode>(m, "PolygonMode").value("Fill", PolygonMode::Fill).value("Line", PolygonMode::Line);

        nb::enum_<BlendFactor>(m, "BlendFactor")
            .value("Zero", BlendFactor::Zero)
            .value("One", BlendFactor::One)
            .value("SrcAlpha", BlendFactor::SrcAlpha)
            .value("OneMinusSrcAlpha", BlendFactor::OneMinusSrcAlpha);

        nb::enum_<DepthFunc>(m, "DepthFunc")
            .value("Less", DepthFunc::Less)
            .value("LessEqual", DepthFunc::LessEqual)
            .value("Equal", DepthFunc::Equal)
            .value("Greater", DepthFunc::Greater)
            .value("GreaterEqual", DepthFunc::GreaterEqual)
            .value("NotEqual", DepthFunc::NotEqual)
            .value("Always", DepthFunc::Always)
            .value("Never", DepthFunc::Never);

        nb::class_<RenderState>(m, "RenderState")
            .def(nb::init<>())
            .def(nb::init<PolygonMode, bool, bool, bool, bool, BlendFactor, BlendFactor>())
            .def(
                "__init__",
                [](RenderState* self,
                   const std::string& polygon_mode,
                   bool cull,
                   bool depth_test,
                   bool depth_write,
                   bool blend,
                   const std::string& blend_src,
                   const std::string& blend_dst) {
                    new (self) RenderState();
                    self->polygon_mode = polygon_mode_from_string(polygon_mode);
                    self->cull = cull;
                    self->depth_test = depth_test;
                    self->depth_write = depth_write;
                    self->blend = blend;
                    self->blend_src = blend_factor_from_string(blend_src);
                    self->blend_dst = blend_factor_from_string(blend_dst);
                },
                nb::arg("polygon_mode") = "fill",
                nb::arg("cull") = true,
                nb::arg("depth_test") = true,
                nb::arg("depth_write") = true,
                nb::arg("blend") = false,
                nb::arg("blend_src") = "src_alpha",
                nb::arg("blend_dst") = "one_minus_src_alpha")
            .def_rw("cull", &RenderState::cull)
            .def_rw("depth_test", &RenderState::depth_test)
            .def_rw("depth_write", &RenderState::depth_write)
            .def_rw("blend", &RenderState::blend)
            .def_prop_rw(
                "polygon_mode",
                [](const RenderState& s) { return polygon_mode_to_string(s.polygon_mode); },
                [](RenderState& s, const std::string& v) { s.polygon_mode = polygon_mode_from_string(v); })
            .def_prop_rw(
                "blend_src",
                [](const RenderState& s) { return blend_factor_to_string(s.blend_src); },
                [](RenderState& s, const std::string& v) { s.blend_src = blend_factor_from_string(v); })
            .def_prop_rw(
                "blend_dst",
                [](const RenderState& s) { return blend_factor_to_string(s.blend_dst); },
                [](RenderState& s, const std::string& v) { s.blend_dst = blend_factor_from_string(v); })
            .def_static("opaque", &RenderState::opaque)
            .def_static("transparent", &RenderState::transparent)
            .def_static("wireframe", &RenderState::wireframe);
    }

} // namespace tgfx_bindings
