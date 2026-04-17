// graphics_bindings.cpp - GraphicsBackend, GPU handles, render types
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/array.h>
#include <nanobind/ndarray.h>

#include "tgfx/render_state.hpp"
#include "tgfx/types.hpp"
#include "tgfx/opengl/opengl_backend.hpp"
#include "tgfx/opengl/opengl_mesh.hpp"
#include <tcbase/tc_log.hpp>

extern "C" {
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tgfx_resource_gpu.h>
#include <tgfx/resources/tc_mesh.h>
}

namespace nb = nanobind;

using namespace termin;

namespace tgfx_bindings {

void bind_types(nb::module_& m) {
    // Color4
    nb::class_<Color4>(m, "Color4")
        .def(nb::init<>())
        .def(nb::init<float, float, float, float>(),
            nb::arg("r"), nb::arg("g"), nb::arg("b"), nb::arg("a") = 1.0f)
        .def("__init__", [](Color4* self, nb::tuple t) {
            if (t.size() < 3) throw std::runtime_error("Color tuple must have at least 3 elements");
            float a = t.size() >= 4 ? nb::cast<float>(t[3]) : 1.0f;
            new (self) Color4(nb::cast<float>(t[0]), nb::cast<float>(t[1]), nb::cast<float>(t[2]), a);
        })
        .def_rw("r", &Color4::r)
        .def_rw("g", &Color4::g)
        .def_rw("b", &Color4::b)
        .def_rw("a", &Color4::a)
        .def_static("black", &Color4::black)
        .def_static("white", &Color4::white)
        .def_static("red", &Color4::red)
        .def_static("green", &Color4::green)
        .def_static("blue", &Color4::blue)
        .def_static("transparent", &Color4::transparent)
        .def("__iter__", [](const Color4& c) {
            return nb::iter(nb::make_tuple(c.r, c.g, c.b, c.a));
        })
        .def("__getitem__", [](const Color4& c, int i) {
            if (i < 0 || i > 3) throw nb::index_error();
            return (&c.r)[i];
        });

    // Size2i
    nb::class_<Size2i>(m, "Size2i")
        .def(nb::init<>())
        .def(nb::init<int, int>(), nb::arg("width"), nb::arg("height"))
        .def("__init__", [](Size2i* self, nb::tuple t) {
            if (t.size() != 2) throw std::runtime_error("Size tuple must have 2 elements");
            new (self) Size2i(nb::cast<int>(t[0]), nb::cast<int>(t[1]));
        })
        .def_rw("width", &Size2i::width)
        .def_rw("height", &Size2i::height)
        .def("__iter__", [](const Size2i& s) {
            return nb::iter(nb::make_tuple(s.width, s.height));
        })
        .def("__getitem__", [](const Size2i& s, int i) {
            if (i == 0) return s.width;
            if (i == 1) return s.height;
            throw nb::index_error();
        })
        .def("__eq__", &Size2i::operator==)
        .def("__ne__", &Size2i::operator!=);

    // Rect2i
    nb::class_<Rect2i>(m, "Rect2i")
        .def(nb::init<>())
        .def(nb::init<int, int, int, int>(),
            nb::arg("x0"), nb::arg("y0"), nb::arg("x1"), nb::arg("y1"))
        .def("__init__", [](Rect2i* self, nb::tuple t) {
            if (t.size() != 4) throw std::runtime_error("Rect tuple must have 4 elements");
            new (self) Rect2i(nb::cast<int>(t[0]), nb::cast<int>(t[1]), nb::cast<int>(t[2]), nb::cast<int>(t[3]));
        })
        .def_rw("x0", &Rect2i::x0)
        .def_rw("y0", &Rect2i::y0)
        .def_rw("x1", &Rect2i::x1)
        .def_rw("y1", &Rect2i::y1)
        .def("width", &Rect2i::width)
        .def("height", &Rect2i::height)
        .def_static("from_size", nb::overload_cast<int, int>(&Rect2i::from_size))
        .def_static("from_size", nb::overload_cast<Size2i>(&Rect2i::from_size))
        .def("__iter__", [](const Rect2i& r) {
            return nb::iter(nb::make_tuple(r.x0, r.y0, r.x1, r.y1));
        })
        .def("__getitem__", [](const Rect2i& r, int i) {
            if (i < 0 || i > 3) throw nb::index_error();
            return (&r.x0)[i];
        });

    // Implicit conversions from Python tuples
    nb::implicitly_convertible<nb::tuple, Color4>();
    nb::implicitly_convertible<nb::tuple, Size2i>();
    nb::implicitly_convertible<nb::tuple, Rect2i>();
}

void bind_render_state(nb::module_& m) {
    nb::enum_<PolygonMode>(m, "PolygonMode")
        .value("Fill", PolygonMode::Fill)
        .value("Line", PolygonMode::Line);

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
        .def("__init__", [](RenderState* self,
            const std::string& polygon_mode,
            bool cull,
            bool depth_test,
            bool depth_write,
            bool blend,
            const std::string& blend_src,
            const std::string& blend_dst
        ) {
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
        .def_prop_rw("polygon_mode",
            [](const RenderState& s) { return polygon_mode_to_string(s.polygon_mode); },
            [](RenderState& s, const std::string& v) { s.polygon_mode = polygon_mode_from_string(v); })
        .def_prop_rw("blend_src",
            [](const RenderState& s) { return blend_factor_to_string(s.blend_src); },
            [](RenderState& s, const std::string& v) { s.blend_src = blend_factor_from_string(v); })
        .def_prop_rw("blend_dst",
            [](const RenderState& s) { return blend_factor_to_string(s.blend_dst); },
            [](RenderState& s, const std::string& v) { s.blend_dst = blend_factor_from_string(v); })
        .def_static("opaque", &RenderState::opaque)
        .def_static("transparent", &RenderState::transparent)
        .def_static("wireframe", &RenderState::wireframe);
}

void bind_gpu_handles(nb::module_& m) {
    // Register OpenGL GPU ops for tc_mesh (draw/upload/delete)
    static const tc_mesh_gpu_ops gl_mesh_ops = {
        tgfx_mesh_draw_gpu,
        tgfx_mesh_upload_gpu,
        tgfx_mesh_delete_gpu,
    };
    tc_mesh_set_gpu_ops(&gl_mesh_ops);

    // ShaderHandle
    nb::class_<ShaderHandle>(m, "ShaderHandle")
        .def("use", &ShaderHandle::use)
        .def("stop", &ShaderHandle::stop)
        .def("release", &ShaderHandle::release)
        .def("set_uniform_int", &ShaderHandle::set_uniform_int)
        .def("set_uniform_float", &ShaderHandle::set_uniform_float)
        .def("set_uniform_vec2", &ShaderHandle::set_uniform_vec2)
        .def("set_uniform_vec2", [](ShaderHandle& self, const char* name, nb::ndarray<float, nb::c_contig, nb::device::cpu> v) {
            const float* ptr = v.data();
            self.set_uniform_vec2(name, ptr[0], ptr[1]);
        })
        .def("set_uniform_vec2", [](ShaderHandle& self, const char* name, nb::tuple t) {
            self.set_uniform_vec2(name, nb::cast<float>(t[0]), nb::cast<float>(t[1]));
        })
        .def("set_uniform_vec3", &ShaderHandle::set_uniform_vec3)
        .def("set_uniform_vec3", [](ShaderHandle& self, const char* name, nb::ndarray<float, nb::c_contig, nb::device::cpu> v) {
            const float* ptr = v.data();
            self.set_uniform_vec3(name, ptr[0], ptr[1], ptr[2]);
        })
        .def("set_uniform_vec3", [](ShaderHandle& self, const char* name, nb::tuple t) {
            self.set_uniform_vec3(name, nb::cast<float>(t[0]), nb::cast<float>(t[1]), nb::cast<float>(t[2]));
        })
        .def("set_uniform_vec4", &ShaderHandle::set_uniform_vec4)
        .def("set_uniform_vec4", [](ShaderHandle& self, const char* name, nb::ndarray<float, nb::c_contig, nb::device::cpu> v) {
            const float* ptr = v.data();
            self.set_uniform_vec4(name, ptr[0], ptr[1], ptr[2], ptr[3]);
        })
        .def("set_uniform_vec4", [](ShaderHandle& self, const char* name, nb::tuple t) {
            self.set_uniform_vec4(name, nb::cast<float>(t[0]), nb::cast<float>(t[1]), nb::cast<float>(t[2]), nb::cast<float>(t[3]));
        })
        .def("set_uniform_matrix4", [](ShaderHandle& self, const char* name, nb::ndarray<float, nb::c_contig, nb::device::cpu> matrix, bool transpose) {
            if (matrix.size() < 16) {
                throw std::runtime_error("Matrix must have at least 16 elements");
            }
            self.set_uniform_matrix4(name, const_cast<float*>(matrix.data()), transpose);
        }, nb::arg("name"), nb::arg("matrix"), nb::arg("transpose") = true)
        .def("set_uniform_matrix4_array", [](ShaderHandle& self, const char* name, nb::ndarray<float, nb::c_contig, nb::device::cpu> matrices, int count, bool transpose) {
            self.set_uniform_matrix4_array(name, const_cast<float*>(matrices.data()), count, transpose);
        }, nb::arg("name"), nb::arg("matrices"), nb::arg("count"), nb::arg("transpose") = true);

    // GPUMeshHandle
    nb::class_<GPUMeshHandle>(m, "GPUMeshHandle")
        .def("draw", &GPUMeshHandle::draw)
        .def("release", &GPUMeshHandle::release)
        .def("delete", &GPUMeshHandle::release);

    // GPUTextureHandle
    nb::class_<GPUTextureHandle>(m, "GPUTextureHandle")
        .def("bind", &GPUTextureHandle::bind, nb::arg("unit") = 0)
        .def("release", &GPUTextureHandle::release)
        .def("delete", &GPUTextureHandle::release)
        .def("get_id", &GPUTextureHandle::get_id)
        .def("get_width", &GPUTextureHandle::get_width)
        .def("get_height", &GPUTextureHandle::get_height)
        .def("is_valid", &GPUTextureHandle::is_valid);

    // FramebufferHandle
}

void bind_graphics_backend(nb::module_& m) {
    nb::enum_<DrawMode>(m, "DrawMode")
        .value("Triangles", DrawMode::Triangles)
        .value("Lines", DrawMode::Lines);


    // init_opengl function
    m.def("init_opengl", &init_opengl, "Initialize OpenGL via glad. Call after context creation.");

    // GPU context management (opaque handles as uintptr_t)
    m.def("tc_gpu_context_new", [](uintptr_t key, uintptr_t share_group_ptr) -> uintptr_t {
        tc_gpu_share_group* group = (tc_gpu_share_group*)share_group_ptr;
        tc_gpu_context* ctx = tc_gpu_context_new(key, group);
        return (uintptr_t)ctx;
    }, nb::arg("key"), nb::arg("share_group_ptr"));

    m.def("tc_gpu_set_context", [](uintptr_t ctx_ptr) {
        tc_gpu_set_context((tc_gpu_context*)ctx_ptr);
    }, nb::arg("ctx_ptr"));

    m.def("tc_gpu_get_context", []() -> uintptr_t {
        return (uintptr_t)tc_gpu_get_context();
    });

    m.def("tc_gpu_context_set_name", [](uintptr_t ctx_ptr, const char* name) {
        tc_gpu_context_set_name((tc_gpu_context*)ctx_ptr, name);
    }, nb::arg("ctx_ptr"), nb::arg("name"));

    m.def("tc_gpu_context_free", [](uintptr_t ctx_ptr) {
        tc_gpu_context_free((tc_gpu_context*)ctx_ptr);
    }, nb::arg("ctx_ptr"));

    m.def("tc_gpu_share_group_get_or_create", [](uintptr_t key) -> uintptr_t {
        return (uintptr_t)tc_gpu_share_group_get_or_create(key);
    }, nb::arg("key"));

    m.def("tc_gpu_share_group_unref", [](uintptr_t group_ptr) {
        tc_gpu_share_group_unref((tc_gpu_share_group*)group_ptr);
    }, nb::arg("group_ptr"));
}

} // namespace tgfx_bindings
