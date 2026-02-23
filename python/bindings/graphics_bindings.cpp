// graphics_bindings.cpp - GraphicsBackend, GPU handles, render types
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/array.h>
#include <nanobind/ndarray.h>

#include "tgfx/graphics_backend.hpp"
#include "tgfx/render_state.hpp"
#include "tgfx/types.hpp"
#include "tgfx/opengl/opengl_backend.hpp"
#include "tgfx/opengl/opengl_mesh.hpp"
#include <tcbase/tc_log.hpp>

extern "C" {
#include <tgfx/tc_gpu_context.h>
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
        .def("get_height", &GPUTextureHandle::get_height);

    // FramebufferHandle
    nb::class_<FramebufferHandle>(m, "FramebufferHandle")
        .def("resize", static_cast<void (FramebufferHandle::*)(int, int)>(&FramebufferHandle::resize))
        .def("resize", static_cast<void (FramebufferHandle::*)(Size2i)>(&FramebufferHandle::resize))
        .def("release", &FramebufferHandle::release)
        .def("delete", &FramebufferHandle::release)
        .def("get_fbo_id", &FramebufferHandle::get_fbo_id)
        .def("get_width", &FramebufferHandle::get_width)
        .def("get_height", &FramebufferHandle::get_height)
        .def("get_size", &FramebufferHandle::get_size)
        .def("get_samples", &FramebufferHandle::get_samples)
        .def("is_msaa", &FramebufferHandle::is_msaa)
        .def("get_format", &FramebufferHandle::get_format)
        .def("get_actual_gl_format", &FramebufferHandle::get_actual_gl_format)
        .def("get_actual_gl_width", &FramebufferHandle::get_actual_gl_width)
        .def("get_actual_gl_height", &FramebufferHandle::get_actual_gl_height)
        .def("get_actual_gl_samples", &FramebufferHandle::get_actual_gl_samples)
        .def("get_filter", &FramebufferHandle::get_filter)
        .def("get_actual_gl_filter", &FramebufferHandle::get_actual_gl_filter)
        .def("color_texture", &FramebufferHandle::color_texture, nb::rv_policy::reference)
        .def("depth_texture", &FramebufferHandle::depth_texture, nb::rv_policy::reference)
        .def("set_external_target", static_cast<void (FramebufferHandle::*)(uint32_t, int, int)>(&FramebufferHandle::set_external_target))
        .def("set_external_target", static_cast<void (FramebufferHandle::*)(uint32_t, Size2i)>(&FramebufferHandle::set_external_target));
}

void bind_graphics_backend(nb::module_& m) {
    nb::enum_<DrawMode>(m, "DrawMode")
        .value("Triangles", DrawMode::Triangles)
        .value("Lines", DrawMode::Lines);

    nb::class_<GraphicsBackend>(m, "GraphicsBackend")
        .def("ensure_ready", [](GraphicsBackend& self) {
            self.ensure_ready();
            tc_ensure_default_gpu_context();
        })
        .def("set_viewport", &GraphicsBackend::set_viewport)
        .def("enable_scissor", &GraphicsBackend::enable_scissor)
        .def("disable_scissor", &GraphicsBackend::disable_scissor)
        .def("clear_color_depth", static_cast<void (GraphicsBackend::*)(float, float, float, float)>(&GraphicsBackend::clear_color_depth))
        .def("clear_color_depth", static_cast<void (GraphicsBackend::*)(const Color4&)>(&GraphicsBackend::clear_color_depth))
        .def("clear_color_depth", [](GraphicsBackend& self, nb::tuple color) {
            float a = color.size() >= 4 ? nb::cast<float>(color[3]) : 1.0f;
            self.clear_color_depth(nb::cast<float>(color[0]), nb::cast<float>(color[1]), nb::cast<float>(color[2]), a);
        })
        .def("clear_color", static_cast<void (GraphicsBackend::*)(float, float, float, float)>(&GraphicsBackend::clear_color))
        .def("clear_color", static_cast<void (GraphicsBackend::*)(const Color4&)>(&GraphicsBackend::clear_color))
        .def("clear_color", [](GraphicsBackend& self, nb::tuple color) {
            float a = color.size() >= 4 ? nb::cast<float>(color[3]) : 1.0f;
            self.clear_color(nb::cast<float>(color[0]), nb::cast<float>(color[1]), nb::cast<float>(color[2]), a);
        })
        .def("clear_depth", &GraphicsBackend::clear_depth, nb::arg("value") = 1.0f)
        .def("set_color_mask", &GraphicsBackend::set_color_mask)
        .def("set_depth_test", &GraphicsBackend::set_depth_test)
        .def("set_depth_mask", &GraphicsBackend::set_depth_mask)
        .def("set_depth_func", &GraphicsBackend::set_depth_func)
        .def("set_depth_func", [](GraphicsBackend& self, const std::string& func) {
            self.set_depth_func(depth_func_from_string(func));
        })
        .def("set_cull_face", &GraphicsBackend::set_cull_face)
        .def("set_blend", &GraphicsBackend::set_blend)
        .def("set_blend_func", &GraphicsBackend::set_blend_func)
        .def("set_blend_func", [](GraphicsBackend& self, const std::string& src, const std::string& dst) {
            self.set_blend_func(blend_factor_from_string(src), blend_factor_from_string(dst));
        })
        .def("set_polygon_mode", &GraphicsBackend::set_polygon_mode)
        .def("set_polygon_mode", [](GraphicsBackend& self, const std::string& mode) {
            self.set_polygon_mode(polygon_mode_from_string(mode));
        })
        .def("reset_state", &GraphicsBackend::reset_state)
        .def("apply_render_state", &GraphicsBackend::apply_render_state)
        .def("set_cull_face_enabled", &GraphicsBackend::set_cull_face)
        .def("set_depth_test_enabled", &GraphicsBackend::set_depth_test)
        .def("set_depth_write_enabled", &GraphicsBackend::set_depth_mask)
        .def("bind_framebuffer", &GraphicsBackend::bind_framebuffer, nb::arg("fbo").none(true))
        .def("read_pixel", &GraphicsBackend::read_pixel)
        .def("read_depth_pixel", &GraphicsBackend::read_depth_pixel)
        .def("check_gl_error", &GraphicsBackend::check_gl_error)
        .def("read_depth_buffer", [](GraphicsBackend& self, FramebufferHandle* fbo) -> nb::object {
            if (fbo == nullptr) return nb::none();
            if (fbo->is_msaa()) return nb::none();

            int width = fbo->get_width();
            int height = fbo->get_height();
            if (width <= 0 || height <= 0) return nb::none();

            float* buf = new float[height * width];
            bool success = self.read_depth_buffer(fbo, buf);
            if (!success) {
                delete[] buf;
                return nb::none();
            }

            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[2] = {static_cast<size_t>(height), static_cast<size_t>(width)};
            return nb::cast(nb::ndarray<nb::numpy, float>(buf, 2, shape, owner));
        })
        .def("read_color_buffer_float", [](GraphicsBackend& self, FramebufferHandle* fbo) -> nb::object {
            if (fbo == nullptr) return nb::none();

            int width = fbo->get_width();
            int height = fbo->get_height();
            if (width <= 0 || height <= 0) return nb::none();

            float* buf = new float[width * height * 4];
            bool success = self.read_color_buffer_float(fbo, buf);
            if (!success) {
                delete[] buf;
                return nb::none();
            }

            nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<float*>(p); });
            size_t shape[3] = {static_cast<size_t>(height), static_cast<size_t>(width), 4};
            return nb::cast(nb::ndarray<nb::numpy, float>(buf, 3, shape, owner));
        });

    // OpenGLGraphicsBackend (singleton)
    nb::class_<OpenGLGraphicsBackend, GraphicsBackend>(m, "OpenGLGraphicsBackend")
        .def_static("get_instance", &OpenGLGraphicsBackend::get_instance, nb::rv_policy::reference)
        .def("ensure_ready", [](OpenGLGraphicsBackend& self) {
            self.ensure_ready();
            tc_ensure_default_gpu_context();
        })
        .def("create_shader", [](OpenGLGraphicsBackend& self, const std::string& vert, const std::string& frag, nb::object geom) {
            const char* geom_ptr = nullptr;
            std::string geom_str;
            if (!geom.is_none()) {
                geom_str = nb::cast<std::string>(geom);
                if (!geom_str.empty()) {
                    geom_ptr = geom_str.c_str();
                }
            }
            return self.create_shader(vert.c_str(), frag.c_str(), geom_ptr);
        }, nb::arg("vertex_source"), nb::arg("fragment_source"), nb::arg("geometry_source") = nb::none())
        .def("create_texture", [](OpenGLGraphicsBackend& self, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data, int width, int height, int channels, bool mipmap, bool clamp) {
            return self.create_texture(const_cast<uint8_t*>(data.data()), width, height, channels, mipmap, clamp);
        }, nb::arg("data"), nb::arg("width"), nb::arg("height"), nb::arg("channels") = 4, nb::arg("mipmap") = true, nb::arg("clamp") = false)
        .def("create_texture", [](OpenGLGraphicsBackend& self, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data, nb::tuple size, int channels, bool mipmap, bool clamp) {
            int width = nb::cast<int>(size[0]);
            int height = nb::cast<int>(size[1]);
            return self.create_texture(const_cast<uint8_t*>(data.data()), width, height, channels, mipmap, clamp);
        }, nb::arg("data"), nb::arg("size"), nb::arg("channels") = 4, nb::arg("mipmap") = true, nb::arg("clamp") = false)
        .def("create_framebuffer", [](OpenGLGraphicsBackend& self, int width, int height, int samples, const std::string& format) {
            return self.create_framebuffer(width, height, samples, format);
        }, nb::arg("width"), nb::arg("height"), nb::arg("samples") = 1, nb::arg("format") = "")
        .def("create_framebuffer", [](OpenGLGraphicsBackend& self, nb::tuple size, int samples, const std::string& format) {
            return self.create_framebuffer(nb::cast<int>(size[0]), nb::cast<int>(size[1]), samples, format);
        }, nb::arg("size"), nb::arg("samples") = 1, nb::arg("format") = "")
        .def("create_shadow_framebuffer", static_cast<FramebufferHandlePtr (OpenGLGraphicsBackend::*)(int, int)>(&OpenGLGraphicsBackend::create_shadow_framebuffer))
        .def("create_shadow_framebuffer", [](OpenGLGraphicsBackend& self, nb::tuple size) {
            return self.create_shadow_framebuffer(nb::cast<int>(size[0]), nb::cast<int>(size[1]));
        })
        .def("create_external_framebuffer", &OpenGLGraphicsBackend::create_external_framebuffer,
            nb::arg("fbo_id"), nb::arg("width"), nb::arg("height"))
        .def("create_external_framebuffer", [](OpenGLGraphicsBackend& self, uint32_t fbo_id, nb::tuple size) {
            return self.create_external_framebuffer(fbo_id, nb::cast<int>(size[0]), nb::cast<int>(size[1]));
        }, nb::arg("fbo_id"), nb::arg("size"))
        .def("blit_framebuffer", [](OpenGLGraphicsBackend& self, FramebufferHandle* src, FramebufferHandle* dst,
                                    nb::tuple src_rect, nb::tuple dst_rect,
                                    bool blit_color, bool blit_depth) {
            self.blit_framebuffer(src, dst,
                nb::cast<int>(src_rect[0]), nb::cast<int>(src_rect[1]), nb::cast<int>(src_rect[2]), nb::cast<int>(src_rect[3]),
                nb::cast<int>(dst_rect[0]), nb::cast<int>(dst_rect[1]), nb::cast<int>(dst_rect[2]), nb::cast<int>(dst_rect[3]),
                blit_color, blit_depth);
        }, nb::arg("src"), nb::arg("dst"), nb::arg("src_rect"), nb::arg("dst_rect"),
           nb::arg("blit_color") = true, nb::arg("blit_depth") = false)
        .def("draw_ui_vertices", [](OpenGLGraphicsBackend& self, nb::ndarray<float, nb::c_contig, nb::device::cpu> vertices) {
            int count = static_cast<int>(vertices.size() / 2);
            self.draw_ui_vertices(const_cast<float*>(vertices.data()), count);
        })
        .def("draw_ui_textured_quad", static_cast<void (OpenGLGraphicsBackend::*)()>(&OpenGLGraphicsBackend::draw_ui_textured_quad))
        .def("draw_ui_textured_quad", [](OpenGLGraphicsBackend& self, nb::ndarray<float, nb::c_contig, nb::device::cpu> vertices) {
            int count = static_cast<int>(vertices.size() / 4);
            self.draw_ui_textured_quad(const_cast<float*>(vertices.data()), count);
        })
        .def("create_mesh", [](OpenGLGraphicsBackend& self, nb::object mesh, DrawMode mode) -> std::unique_ptr<GPUMeshHandle> {
            nb::ndarray<float, nb::c_contig, nb::device::cpu> buffer = nb::cast<nb::ndarray<float, nb::c_contig, nb::device::cpu>>(mesh.attr("interleaved_buffer")());

            nb::object indices_arr = mesh.attr("indices");
            nb::object flattened = indices_arr.attr("flatten")();
            nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu> indices = nb::cast<nb::ndarray<uint32_t, nb::c_contig, nb::device::cpu>>(
                flattened.attr("astype")("uint32"));

            nb::object layout = mesh.attr("get_vertex_layout")();
            int stride = nb::cast<int>(layout.attr("stride"));

            nb::list attrs = nb::cast<nb::list>(layout.attr("attributes"));
            int position_offset = 0;
            int position_size = 3;
            bool has_normal = false;
            int normal_offset = 0;
            bool has_uv = false;
            int uv_offset = 0;
            bool has_joints = false;
            int joints_offset = 0;
            bool has_weights = false;
            int weights_offset = 0;

            for (auto attr : attrs) {
                std::string name = nb::cast<std::string>(attr.attr("name"));
                int offset = nb::cast<int>(attr.attr("offset"));
                int size = nb::cast<int>(attr.attr("size"));
                if (name == "position") {
                    position_offset = offset;
                    position_size = size;
                } else if (name == "normal") {
                    has_normal = true;
                    normal_offset = offset;
                } else if (name == "uv") {
                    has_uv = true;
                    uv_offset = offset;
                } else if (name == "joints") {
                    has_joints = true;
                    joints_offset = offset;
                } else if (name == "weights") {
                    has_weights = true;
                    weights_offset = offset;
                }
            }

            DrawMode actual_mode = mode;
            if (mode == DrawMode::Triangles && nb::cast<int>(indices_arr.attr("ndim")) == 2) {
                nb::tuple shape = nb::cast<nb::tuple>(indices_arr.attr("shape"));
                int cols = nb::cast<int>(shape[1]);
                if (cols == 2) {
                    actual_mode = DrawMode::Lines;
                }
            }

            return std::make_unique<OpenGLRawMeshHandle>(
                const_cast<float*>(buffer.data()), buffer.size() * sizeof(float),
                const_cast<uint32_t*>(indices.data()), indices.size(),
                stride,
                position_offset, position_size,
                has_normal, normal_offset,
                has_uv, uv_offset,
                has_joints, joints_offset,
                has_weights, weights_offset,
                actual_mode
            );
        }, nb::arg("mesh"), nb::arg("mode") = DrawMode::Triangles);

    // init_opengl function
    m.def("init_opengl", &init_opengl, "Initialize OpenGL via glad. Call after context creation.");
}

} // namespace tgfx_bindings
