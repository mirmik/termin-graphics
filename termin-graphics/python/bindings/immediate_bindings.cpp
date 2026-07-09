#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>
#include <nanobind/ndarray.h>

#include <tgfx2/immediate_renderer.hpp>
#include <tgfx2/render_context.hpp>

namespace nb = nanobind;

using namespace termin;

namespace tgfx_bindings {

void bind_immediate(nb::module_& m) {
    nb::class_<ImmediateRenderer>(m, "ImmediateRenderer")
        .def(nb::init<>())
        .def("begin", &ImmediateRenderer::begin,
             "Clear all accumulated primitives")
        // Basic primitives
        .def("line", &ImmediateRenderer::line,
             nb::arg("start"), nb::arg("end"), nb::arg("color"), nb::arg("depth_test") = false)
        .def("triangle", &ImmediateRenderer::triangle,
             nb::arg("p0"), nb::arg("p1"), nb::arg("p2"), nb::arg("color"), nb::arg("depth_test") = false)
        .def("quad", &ImmediateRenderer::quad,
             nb::arg("p0"), nb::arg("p1"), nb::arg("p2"), nb::arg("p3"), nb::arg("color"), nb::arg("depth_test") = false)
        // Batch triangles with per-vertex colors
        .def("triangles", [](ImmediateRenderer& self,
                             nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> vertices,
                             nb::ndarray<uint32_t, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> indices,
                             nb::ndarray<float, nb::shape<-1, 4>, nb::c_contig, nb::device::cpu> colors,
                             bool depth_test) {
            size_t vertex_count = vertices.shape(0);
            size_t triangle_count = indices.shape(0);
            if (colors.shape(0) != vertex_count) {
                throw std::runtime_error("ImmediateRenderer.triangles colors must have one RGBA row per vertex");
            }
            self.triangles(
                vertices.data(),
                vertex_count,
                indices.data(),
                triangle_count,
                colors.data(),
                depth_test
            );
        },
             nb::arg("vertices"), nb::arg("indices"), nb::arg("colors"), nb::arg("depth_test") = false,
             "Batch triangles from buffer-compatible arrays (vertices Nx3, indices Mx3, colors Nx4)")
        // Batch triangles with single color
        .def("triangles", [](ImmediateRenderer& self,
                             nb::ndarray<float, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> vertices,
                             nb::ndarray<uint32_t, nb::shape<-1, 3>, nb::c_contig, nb::device::cpu> indices,
                             const Color4& color,
                             bool depth_test) {
            size_t vertex_count = vertices.shape(0);
            size_t triangle_count = indices.shape(0);
            self.triangles(
                vertices.data(),
                vertex_count,
                indices.data(),
                triangle_count,
                color,
                depth_test
            );
        },
             nb::arg("vertices"), nb::arg("indices"), nb::arg("color"), nb::arg("depth_test") = false,
             "Batch triangles from buffer-compatible arrays with single color (vertices Nx3, indices Mx3)")
        // Wireframe
        .def("polyline", &ImmediateRenderer::polyline,
             nb::arg("points"), nb::arg("color"), nb::arg("closed") = false, nb::arg("depth_test") = false)
        .def("circle", &ImmediateRenderer::circle,
             nb::arg("center"), nb::arg("normal"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 32, nb::arg("depth_test") = false)
        .def("arrow", &ImmediateRenderer::arrow,
             nb::arg("origin"), nb::arg("direction"), nb::arg("length"), nb::arg("color"),
             nb::arg("head_length") = 0.2, nb::arg("head_width") = 0.1, nb::arg("depth_test") = false)
        .def("box", &ImmediateRenderer::box,
             nb::arg("min_pt"), nb::arg("max_pt"), nb::arg("color"), nb::arg("depth_test") = false)
        .def("cylinder_wireframe", &ImmediateRenderer::cylinder_wireframe,
             nb::arg("start"), nb::arg("end"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 16, nb::arg("depth_test") = false)
        .def("sphere_wireframe", &ImmediateRenderer::sphere_wireframe,
             nb::arg("center"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 16, nb::arg("depth_test") = false)
        .def("capsule_wireframe", &ImmediateRenderer::capsule_wireframe,
             nb::arg("start"), nb::arg("end"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 16, nb::arg("depth_test") = false)
        // Solid
        .def("cylinder_solid", &ImmediateRenderer::cylinder_solid,
             nb::arg("start"), nb::arg("end"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 16, nb::arg("caps") = true, nb::arg("depth_test") = false)
        .def("cone_solid", &ImmediateRenderer::cone_solid,
             nb::arg("base"), nb::arg("tip"), nb::arg("radius"), nb::arg("color"),
             nb::arg("segments") = 16, nb::arg("cap") = true, nb::arg("depth_test") = false)
        .def("torus_solid", [](ImmediateRenderer& self,
                                const Vec3& center,
                                const Vec3& axis,
                                double major_radius,
                                double minor_radius,
                                const Color4& color,
                                int major_segments,
                                int minor_segments,
                                bool depth_test) {
            self.torus_solid(
                TorusSolidSpec{
                    center,
                    axis,
                    major_radius,
                    minor_radius,
                    major_segments,
                    minor_segments
                },
                color,
                depth_test
            );
        },
             nb::arg("center"), nb::arg("axis"), nb::arg("major_radius"), nb::arg("minor_radius"),
             nb::arg("color"), nb::arg("major_segments") = 32, nb::arg("minor_segments") = 12,
             nb::arg("depth_test") = false)
        .def("arrow_solid", [](ImmediateRenderer& self,
                                const Vec3& origin,
                                const Vec3& direction,
                                double length,
                                const Color4& color,
                                double shaft_radius,
                                double head_radius,
                                double head_length_ratio,
                                int segments,
                                bool depth_test) {
            self.arrow_solid(
                ArrowSolidSpec{
                    origin,
                    direction,
                    length,
                    shaft_radius,
                    head_radius,
                    head_length_ratio,
                    segments
                },
                color,
                depth_test
            );
        },
             nb::arg("origin"), nb::arg("direction"), nb::arg("length"), nb::arg("color"),
             nb::arg("shaft_radius") = 0.03, nb::arg("head_radius") = 0.06,
             nb::arg("head_length_ratio") = 0.25, nb::arg("segments") = 16, nb::arg("depth_test") = false)
        // Rendering via tgfx2 ctx2 (Stage 8.1 migration)
        .def("flush", [](ImmediateRenderer& self,
                         tgfx::RenderContext2* ctx2,
                         nb::ndarray<double, nb::shape<4, 4>, nb::c_contig, nb::device::cpu> view,
                         nb::ndarray<double, nb::shape<4, 4>, nb::c_contig, nb::device::cpu> proj,
                         bool depth_test,
                         bool blend) {
            Mat44 view_mat, proj_mat;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    view_mat.data[col * 4 + row] = view(row, col);
                    proj_mat.data[col * 4 + row] = proj(row, col);
                }
            }
            self.flush(ctx2, view_mat, proj_mat, depth_test, blend);
        },
             nb::arg("ctx2"), nb::arg("view_matrix"), nb::arg("proj_matrix"),
             nb::arg("depth_test") = true, nb::arg("blend") = true)
        .def("flush", [](ImmediateRenderer& self,
                         tgfx::RenderContext2* ctx2,
                         const Mat44& view,
                         const Mat44& proj,
                         bool depth_test,
                         bool blend) {
            self.flush(ctx2, view, proj, depth_test, blend);
        },
             nb::arg("ctx2"), nb::arg("view_matrix"), nb::arg("proj_matrix"),
             nb::arg("depth_test") = true, nb::arg("blend") = true)
        .def("flush_depth", [](ImmediateRenderer& self,
                         tgfx::RenderContext2* ctx2,
                         nb::ndarray<double, nb::shape<4, 4>, nb::c_contig, nb::device::cpu> view,
                         nb::ndarray<double, nb::shape<4, 4>, nb::c_contig, nb::device::cpu> proj,
                         bool blend) {
            Mat44 view_mat, proj_mat;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    view_mat.data[col * 4 + row] = view(row, col);
                    proj_mat.data[col * 4 + row] = proj(row, col);
                }
            }
            self.flush_depth(ctx2, view_mat, proj_mat, blend);
        },
             nb::arg("ctx2"), nb::arg("view_matrix"), nb::arg("proj_matrix"),
             nb::arg("blend") = true)
        .def("flush_depth", [](ImmediateRenderer& self,
                         tgfx::RenderContext2* ctx2,
                         const Mat44& view,
                         const Mat44& proj,
                         bool blend) {
            self.flush_depth(ctx2, view, proj, blend);
        },
             nb::arg("ctx2"), nb::arg("view_matrix"), nb::arg("proj_matrix"),
             nb::arg("blend") = true)
        // Properties
        .def_prop_ro("line_count", &ImmediateRenderer::line_count)
        .def_prop_ro("triangle_count", &ImmediateRenderer::triangle_count)
        .def_prop_ro("line_count_depth", &ImmediateRenderer::line_count_depth)
        .def_prop_ro("triangle_count_depth", &ImmediateRenderer::triangle_count_depth);
}

} // namespace tgfx_bindings
