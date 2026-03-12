#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/make_iterator.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include <termin/geom/mat44.hpp>
#include <termin/entity/component.hpp>
#include <termin/entity/entity.hpp>
#include <termin/render/frame_graph_debugger_core.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_context.hpp>
#include <termin/render/resource_spec.hpp>
#include <termin/render/tc_pass.hpp>
#include <termin/render/execute_context.hpp>
#include <core/tc_component.h>

#include <tgfx/graphics_backend.hpp>

#include <termin/tc_scene.hpp>

namespace nb = nanobind;

namespace termin {

void bind_render_framework(nb::module_& m) {
    nb::enum_<TextureFilter>(m, "TextureFilter")
        .value("LINEAR", TextureFilter::LINEAR)
        .value("NEAREST", TextureFilter::NEAREST);

    nb::class_<ResourceSpec>(m, "ResourceSpec")
        .def(nb::init<>())
        .def(nb::init<std::string, std::string>(),
             nb::arg("resource"),
             nb::arg("resource_type") = "fbo")
        .def("__init__", [](ResourceSpec* self,
            const std::string& resource,
            const std::string& resource_type,
            nb::object size,
            nb::object clear_color,
            nb::object clear_depth,
            nb::object format,
            int samples,
            const std::string& viewport_name,
            float scale,
            TextureFilter filter
        ) {
            new (self) ResourceSpec();
            self->resource = resource;
            self->resource_type = resource_type;
            self->samples = samples;
            self->viewport_name = viewport_name;
            self->scale = scale;
            self->filter = filter;

            if (!size.is_none()) {
                nb::tuple t = nb::cast<nb::tuple>(size);
                self->size = std::make_pair(nb::cast<int>(t[0]), nb::cast<int>(t[1]));
            }
            if (!clear_color.is_none()) {
                nb::tuple t = nb::cast<nb::tuple>(clear_color);
                self->clear_color = std::array<double, 4>{
                    nb::cast<double>(t[0]), nb::cast<double>(t[1]),
                    nb::cast<double>(t[2]), nb::cast<double>(t[3])
                };
            }
            if (!clear_depth.is_none()) {
                self->clear_depth = nb::cast<float>(clear_depth);
            }
            if (!format.is_none()) {
                self->format = nb::cast<std::string>(format);
            }
        },
            nb::arg("resource"),
            nb::arg("resource_type") = "fbo",
            nb::arg("size") = nb::none(),
            nb::arg("clear_color") = nb::none(),
            nb::arg("clear_depth") = nb::none(),
            nb::arg("format") = nb::none(),
            nb::arg("samples") = 1,
            nb::arg("viewport_name") = "",
            nb::arg("scale") = 1.0f,
            nb::arg("filter") = TextureFilter::LINEAR)
        .def_rw("resource", &ResourceSpec::resource)
        .def_rw("resource_type", &ResourceSpec::resource_type)
        .def_rw("samples", &ResourceSpec::samples)
        .def_rw("viewport_name", &ResourceSpec::viewport_name)
        .def_rw("scale", &ResourceSpec::scale)
        .def_rw("filter", &ResourceSpec::filter)
        .def_prop_rw("size",
            [](const ResourceSpec& self) -> nb::object {
                if (self.size) {
                    return nb::make_tuple(self.size->first, self.size->second);
                }
                return nb::none();
            },
            [](ResourceSpec& self, nb::object val) {
                if (val.is_none()) {
                    self.size = std::nullopt;
                } else {
                    nb::tuple t = nb::cast<nb::tuple>(val);
                    self.size = std::make_pair(nb::cast<int>(t[0]), nb::cast<int>(t[1]));
                }
            })
        .def_prop_rw("clear_color",
            [](const ResourceSpec& self) -> nb::object {
                if (self.clear_color) {
                    auto& c = *self.clear_color;
                    return nb::make_tuple(c[0], c[1], c[2], c[3]);
                }
                return nb::none();
            },
            [](ResourceSpec& self, nb::object val) {
                if (val.is_none()) {
                    self.clear_color = std::nullopt;
                } else {
                    nb::tuple t = nb::cast<nb::tuple>(val);
                    self.clear_color = std::array<double, 4>{
                        nb::cast<double>(t[0]), nb::cast<double>(t[1]),
                        nb::cast<double>(t[2]), nb::cast<double>(t[3])
                    };
                }
            })
        .def_prop_rw("clear_depth",
            [](const ResourceSpec& self) -> nb::object {
                if (self.clear_depth) {
                    return nb::cast(*self.clear_depth);
                }
                return nb::none();
            },
            [](ResourceSpec& self, nb::object val) {
                if (val.is_none()) {
                    self.clear_depth = std::nullopt;
                } else {
                    self.clear_depth = nb::cast<float>(val);
                }
            })
        .def_prop_rw("format",
            [](const ResourceSpec& self) -> nb::object {
                if (self.format) {
                    return nb::cast(*self.format);
                }
                return nb::none();
            },
            [](ResourceSpec& self, nb::object val) {
                if (val.is_none()) {
                    self.format = std::nullopt;
                } else {
                    self.format = nb::cast<std::string>(val);
                }
            });

    nb::class_<InternalSymbolTiming>(m, "InternalSymbolTiming")
        .def(nb::init<>())
        .def_rw("name", &InternalSymbolTiming::name)
        .def_rw("cpu_time_ms", &InternalSymbolTiming::cpu_time_ms)
        .def_rw("gpu_time_ms", &InternalSymbolTiming::gpu_time_ms);

    nb::class_<Rect4i>(m, "Rect4i")
        .def(nb::init<>())
        .def(nb::init<int, int, int, int>(),
             nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))
        .def_rw("x", &Rect4i::x)
        .def_rw("y", &Rect4i::y)
        .def_rw("width", &Rect4i::width)
        .def_rw("height", &Rect4i::height)
        .def("__len__", [](const Rect4i&) {
            return 4;
        })
        .def("__getitem__", [](const Rect4i& self, size_t index) {
            switch (index) {
                case 0: return self.x;
                case 1: return self.y;
                case 2: return self.width;
                case 3: return self.height;
                default: throw nb::index_error();
            }
        })
        .def("__iter__", [](const Rect4i& self) {
            return nb::make_iterator(
                nb::type<Rect4i>(),
                "rect4i_iter",
                &self.x,
                &self.x + 4
            );
        }, nb::keep_alive<0, 1>())
        .def("__repr__", [](const Rect4i& self) {
            return "Rect4i(" +
                std::to_string(self.x) + ", " +
                std::to_string(self.y) + ", " +
                std::to_string(self.width) + ", " +
                std::to_string(self.height) + ")";
        });

    nb::class_<FrameGraphResource>(m, "FrameGraphResource")
        .def("resource_type", &FrameGraphResource::resource_type);

    nb::class_<TcPassRef>(m, "TcPassRef")
        .def(nb::init<>())
        .def("valid", &TcPassRef::valid)
        .def_prop_rw("pass_name", &TcPassRef::pass_name, &TcPassRef::set_pass_name)
        .def_prop_ro("type_name", &TcPassRef::type_name)
        .def_prop_rw("enabled", &TcPassRef::enabled, &TcPassRef::set_enabled)
        .def_prop_rw("passthrough", &TcPassRef::passthrough, &TcPassRef::set_passthrough)
        .def("is_inplace", &TcPassRef::is_inplace)
        .def_prop_ro("inplace", &TcPassRef::is_inplace)
        .def("get_internal_symbols_with_timing", [](TcPassRef& self) {
            std::vector<InternalSymbolTiming> result;
            tc_pass* p = self.ptr();
            if (p && p->kind == TC_NATIVE_PASS) {
                CxxFramePass* fp = CxxFramePass::from_tc(p);
                if (fp) {
                    return fp->get_internal_symbols_with_timing();
                }
            }
            return result;
        });

    auto dict_to_resource_map = [](nb::dict py_dict) -> ResourceMap {
        ResourceMap result;
        for (auto item : py_dict) {
            std::string key = nb::cast<std::string>(nb::str(item.first));
            nb::object val = nb::borrow<nb::object>(item.second);
            if (!val.is_none()) {
                try {
                    result[key] = nb::cast<FramebufferHandle*>(val);
                    continue;
                } catch (const nb::cast_error&) {
                }
            }
        }
        return result;
    };

    nb::class_<ExecuteContext>(m, "ExecuteContext")
        .def(nb::init<>())
        .def("__init__", [dict_to_resource_map](ExecuteContext* self, nb::kwargs kwargs) {
            new (self) ExecuteContext();
            if (kwargs.contains("graphics")) {
                nb::object g = nb::borrow<nb::object>(kwargs["graphics"]);
                if (!g.is_none()) {
                    self->graphics = nb::cast<GraphicsBackend*>(g);
                }
            }
            if (kwargs.contains("reads_fbos")) {
                self->reads_fbos = dict_to_resource_map(nb::cast<nb::dict>(kwargs["reads_fbos"]));
            }
            if (kwargs.contains("writes_fbos")) {
                self->writes_fbos = dict_to_resource_map(nb::cast<nb::dict>(kwargs["writes_fbos"]));
            }
            if (kwargs.contains("rect")) {
                nb::tuple t = nb::cast<nb::tuple>(kwargs["rect"]);
                self->rect.x = nb::cast<int>(t[0]);
                self->rect.y = nb::cast<int>(t[1]);
                self->rect.width = nb::cast<int>(t[2]);
                self->rect.height = nb::cast<int>(t[3]);
            }
            if (kwargs.contains("scene")) {
                nb::object s = nb::borrow<nb::object>(kwargs["scene"]);
                if (!s.is_none()) {
                    if (nb::isinstance<TcSceneRef>(s)) {
                        self->scene = nb::cast<TcSceneRef>(s);
                    } else if (nb::hasattr(s, "scene_ref")) {
                        self->scene = nb::cast<TcSceneRef>(s.attr("scene_ref")());
                    }
                }
            }
            if (kwargs.contains("viewport_name")) {
                self->viewport_name = nb::cast<std::string>(kwargs["viewport_name"]);
            }
            if (kwargs.contains("internal_entities")) {
                nb::object ent = nb::borrow<nb::object>(kwargs["internal_entities"]);
                if (!ent.is_none()) {
                    self->internal_entities = nb::cast<Entity>(ent).handle();
                }
            }
            if (kwargs.contains("lights")) {
                nb::object l = nb::borrow<nb::object>(kwargs["lights"]);
                if (!l.is_none()) {
                    self->lights = nb::cast<std::vector<Light>>(l);
                }
            }
            if (kwargs.contains("layer_mask")) {
                self->layer_mask = nb::cast<uint64_t>(kwargs["layer_mask"]);
            }
        })
        .def_prop_rw("graphics",
            [](const ExecuteContext& ctx) { return ctx.graphics; },
            [](ExecuteContext& ctx, GraphicsBackend* g) { ctx.graphics = g; },
            nb::rv_policy::reference)
        .def_prop_rw("reads_fbos",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.reads_fbos) {
                    if (auto* fbo = dynamic_cast<FramebufferHandle*>(val)) {
                        result[nb::str(key.c_str())] = nb::cast(fbo, nb::rv_policy::reference);
                    }
                }
                return result;
            },
            [dict_to_resource_map](ExecuteContext& ctx, nb::dict py_dict) {
                ctx.reads_fbos = dict_to_resource_map(py_dict);
            })
        .def_prop_rw("writes_fbos",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.writes_fbos) {
                    if (auto* fbo = dynamic_cast<FramebufferHandle*>(val)) {
                        result[nb::str(key.c_str())] = nb::cast(fbo, nb::rv_policy::reference);
                    }
                }
                return result;
            },
            [dict_to_resource_map](ExecuteContext& ctx, nb::dict py_dict) {
                ctx.writes_fbos = dict_to_resource_map(py_dict);
            })
        .def_prop_rw("camera",
            [](const ExecuteContext& ctx) -> nb::object {
                if (ctx.camera == nullptr) {
                    return nb::none();
                }
                nb::module_ render_components =
                    nb::module_::import_("termin.render_components._components_render_native");
                nb::object camera_class = render_components.attr("CameraComponent");
                uintptr_t ptr = reinterpret_cast<uintptr_t>(ctx.camera);
                return camera_class.attr("_from_cxx_component_ptr")(ptr);
            },
            [](ExecuteContext& ctx, nb::object camera_obj) {
                if (camera_obj.is_none()) {
                    ctx.camera = nullptr;
                    return;
                }
                uintptr_t ptr = nb::cast<uintptr_t>(camera_obj.attr("_cxx_component_ptr")());
                ctx.camera = reinterpret_cast<CameraComponent*>(ptr);
            })
        .def_rw("viewport_name", &ExecuteContext::viewport_name)
        .def_prop_rw("internal_entities",
            [](const ExecuteContext& ctx) -> nb::object {
                if (!tc_entity_handle_valid(ctx.internal_entities)) {
                    return nb::none();
                }
                return nb::cast(Entity(ctx.internal_entities));
            },
            [](ExecuteContext& ctx, nb::object entity_obj) {
                if (entity_obj.is_none()) {
                    ctx.internal_entities = TC_ENTITY_HANDLE_INVALID;
                    return;
                }
                ctx.internal_entities = nb::cast<Entity>(entity_obj).handle();
            })
        .def_rw("rect", &ExecuteContext::rect)
        .def_rw("scene", &ExecuteContext::scene)
        .def_rw("lights", &ExecuteContext::lights)
        .def_rw("layer_mask", &ExecuteContext::layer_mask);

    nb::class_<CxxFramePass>(m, "FramePass")
        .def_prop_rw("pass_name",
            [](CxxFramePass& p) { return p.get_pass_name(); },
            [](CxxFramePass& p, const std::string& n) { p.set_pass_name(n); })
        .def("compute_reads", [](CxxFramePass& p) {
            auto reads = p.compute_reads();
            std::set<std::string> result;
            for (const char* r : reads) {
                if (r) {
                    result.insert(r);
                }
            }
            return result;
        })
        .def("compute_writes", [](CxxFramePass& p) {
            auto writes = p.compute_writes();
            std::set<std::string> result;
            for (const char* w : writes) {
                if (w) {
                    result.insert(w);
                }
            }
            return result;
        })
        .def_prop_rw("enabled",
            [](CxxFramePass& p) { return p.get_enabled(); },
            [](CxxFramePass& p, bool v) { p.set_enabled(v); })
        .def_prop_rw("passthrough",
            [](CxxFramePass& p) { return p._c.passthrough; },
            [](CxxFramePass& p, bool v) { p._c.passthrough = v; })
        .def_prop_rw("viewport_name",
            [](CxxFramePass& p) { return p.get_viewport_name(); },
            [](CxxFramePass& p, const std::string& n) { p.set_viewport_name(n); })
        .def("get_inplace_aliases", &CxxFramePass::get_inplace_aliases)
        .def("is_inplace", &CxxFramePass::is_inplace)
        .def_prop_ro("inplace", &CxxFramePass::is_inplace)
        .def("get_internal_symbols", &CxxFramePass::get_internal_symbols)
        .def("get_internal_symbols_with_timing", &CxxFramePass::get_internal_symbols_with_timing)
        .def("get_resource_specs", &CxxFramePass::get_resource_specs)
        .def("set_debug_internal_point", &CxxFramePass::set_debug_internal_point)
        .def("clear_debug_internal_point", &CxxFramePass::clear_debug_internal_point)
        .def("get_debug_internal_point", &CxxFramePass::get_debug_internal_point)
        .def("required_resources", &CxxFramePass::required_resources)
        .def("destroy", &CxxFramePass::destroy)
        .def_prop_ro("_tc_pass",
            [](CxxFramePass& p) {
                return TcPassRef(p.tc_pass_ptr());
            });

    nb::class_<RenderContext>(m, "RenderContext")
        .def(nb::init<>())
        .def("__init__", [](RenderContext* self, nb::kwargs kwargs) {
            new (self) RenderContext();

            if (kwargs.contains("phase")) {
                self->phase = nb::cast<std::string>(kwargs["phase"]);
            }
            if (kwargs.contains("scene")) {
                nb::object s = nb::borrow<nb::object>(kwargs["scene"]);
                if (!s.is_none()) {
                    if (nb::isinstance<TcSceneRef>(s)) {
                        self->scene = nb::cast<TcSceneRef>(s);
                    } else if (nb::hasattr(s, "scene_ref")) {
                        self->scene = nb::cast<TcSceneRef>(s.attr("scene_ref")());
                    }
                }
            }
            if (kwargs.contains("layer_mask")) {
                self->layer_mask = nb::cast<uint64_t>(kwargs["layer_mask"]);
            }
            if (kwargs.contains("graphics")) {
                nb::object g_obj = nb::borrow<nb::object>(kwargs["graphics"]);
                if (!g_obj.is_none()) {
                    self->graphics = nb::cast<GraphicsBackend*>(g_obj);
                }
            }
            if (kwargs.contains("view")) {
                nb::object v = nb::borrow<nb::object>(kwargs["view"]);
                if (nb::isinstance<Mat44>(v)) {
                    self->view = nb::cast<Mat44>(v).to_float();
                } else {
                    auto arr = nb::cast<nb::ndarray<nb::numpy, float, nb::shape<4, 4>>>(v);
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 4; ++col) {
                            self->view.data[col * 4 + row] = arr(row, col);
                        }
                    }
                }
            }
            if (kwargs.contains("projection")) {
                nb::object p = nb::borrow<nb::object>(kwargs["projection"]);
                if (nb::isinstance<Mat44>(p)) {
                    self->projection = nb::cast<Mat44>(p).to_float();
                } else {
                    auto arr = nb::cast<nb::ndarray<nb::numpy, float, nb::shape<4, 4>>>(p);
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 4; ++col) {
                            self->projection.data[col * 4 + row] = arr(row, col);
                        }
                    }
                }
            }
            if (kwargs.contains("model")) {
                nb::object model = nb::borrow<nb::object>(kwargs["model"]);
                if (nb::isinstance<Mat44>(model)) {
                    self->model = nb::cast<Mat44>(model).to_float();
                } else {
                    auto arr = nb::cast<nb::ndarray<nb::numpy, float, nb::shape<4, 4>>>(model);
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 4; ++col) {
                            self->model.data[col * 4 + row] = arr(row, col);
                        }
                    }
                }
            }
        })
        .def_rw("phase", &RenderContext::phase)
        .def_rw("scene", &RenderContext::scene)
        .def_rw("layer_mask", &RenderContext::layer_mask)
        .def_prop_rw("graphics",
            [](const RenderContext& self) -> GraphicsBackend* { return self.graphics; },
            [](RenderContext& self, GraphicsBackend* g) { self.graphics = g; },
            nb::rv_policy::reference)
        .def_rw("view", &RenderContext::view)
        .def_rw("projection", &RenderContext::projection)
        .def_rw("model", &RenderContext::model)
        .def("mvp", &RenderContext::mvp);

    nb::class_<HDRStats>(m, "HDRStats")
        .def(nb::init<>())
        .def_ro("min_r", &HDRStats::min_r)
        .def_ro("max_r", &HDRStats::max_r)
        .def_ro("avg_r", &HDRStats::avg_r)
        .def_ro("min_g", &HDRStats::min_g)
        .def_ro("max_g", &HDRStats::max_g)
        .def_ro("avg_g", &HDRStats::avg_g)
        .def_ro("min_b", &HDRStats::min_b)
        .def_ro("max_b", &HDRStats::max_b)
        .def_ro("avg_b", &HDRStats::avg_b)
        .def_ro("hdr_pixel_count", &HDRStats::hdr_pixel_count)
        .def_ro("total_pixels", &HDRStats::total_pixels)
        .def_ro("hdr_percent", &HDRStats::hdr_percent)
        .def_ro("max_value", &HDRStats::max_value);

    nb::class_<FBOInfo>(m, "FBOInfo")
        .def(nb::init<>())
        .def_ro("type_name", &FBOInfo::type_name)
        .def_ro("width", &FBOInfo::width)
        .def_ro("height", &FBOInfo::height)
        .def_ro("samples", &FBOInfo::samples)
        .def_ro("is_msaa", &FBOInfo::is_msaa)
        .def_ro("format", &FBOInfo::format)
        .def_ro("fbo_id", &FBOInfo::fbo_id)
        .def_ro("gl_format", &FBOInfo::gl_format)
        .def_ro("gl_width", &FBOInfo::gl_width)
        .def_ro("gl_height", &FBOInfo::gl_height)
        .def_ro("gl_samples", &FBOInfo::gl_samples)
        .def_ro("filter", &FBOInfo::filter)
        .def_ro("gl_filter", &FBOInfo::gl_filter);

    nb::class_<FrameGraphCapture>(m, "FrameGraphCapture")
        .def(nb::init<>())
        .def("set_target", [](FrameGraphCapture& self, CxxFramePass* pass) {
            self.set_target(pass);
        }, nb::arg("pass"), nb::rv_policy::reference)
        .def("clear_target", &FrameGraphCapture::clear_target)
        .def("capture", &FrameGraphCapture::capture,
             nb::arg("caller"), nb::arg("src"), nb::arg("graphics"))
        .def("capture_direct", &FrameGraphCapture::capture_direct,
             nb::arg("src"), nb::arg("graphics"))
        .def("has_capture", &FrameGraphCapture::has_capture)
        .def("reset_capture", &FrameGraphCapture::reset_capture)
        .def_prop_ro("capture_fbo", &FrameGraphCapture::capture_fbo,
                     nb::rv_policy::reference);

    nb::class_<FrameGraphPresenter>(m, "FrameGraphPresenter")
        .def(nb::init<>())
        .def("render", &FrameGraphPresenter::render,
             nb::arg("graphics"), nb::arg("capture_fbo"),
             nb::arg("dst_w"), nb::arg("dst_h"),
             nb::arg("channel_mode"), nb::arg("highlight_hdr"))
        .def("compute_hdr_stats", &FrameGraphPresenter::compute_hdr_stats,
             nb::arg("graphics"), nb::arg("fbo"))
        .def_static("get_fbo_info", &FrameGraphPresenter::get_fbo_info,
                     nb::arg("fbo"));

    nb::class_<FrameGraphDebuggerCore>(m, "FrameGraphDebuggerCore")
        .def(nb::init<>())
        .def_prop_ro("capture_fbo", &FrameGraphDebuggerCore::capture_fbo,
                     nb::rv_policy::reference)
        .def_prop_ro("capture", [](FrameGraphDebuggerCore& self) -> FrameGraphCapture& {
            return self.capture;
        }, nb::rv_policy::reference_internal)
        .def_prop_ro("presenter", [](FrameGraphDebuggerCore& self) -> FrameGraphPresenter& {
            return self.presenter;
        }, nb::rv_policy::reference_internal);
}

} // namespace termin

NB_MODULE(_render_framework_native, m) {
    m.doc() = "Native render framework bindings";

    nb::module_::import_("tgfx._tgfx_native");
    nb::module_::import_("termin.geombase._geom_native");
    nb::module_::import_("termin.entity._entity_native");
    nb::module_::import_("termin.lighting._lighting_native");

    termin::bind_render_framework(m);
}
