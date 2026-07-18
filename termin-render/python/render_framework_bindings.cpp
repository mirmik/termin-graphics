#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/make_iterator.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include <tcbase/tc_log.hpp>

#include <termin/geom/mat44.hpp>
#include <termin/entity/entity.hpp>
#include <termin/render/render_camera.hpp>
#include <termin/render/frame_debug_capture_pass.hpp>
#include <termin/render/frame_graph_debugger_core.hpp>
#include <termin/render/graph_alias_pass.hpp>
#include <termin/render/render_pipeline.hpp>
#include <termin/render/shader_usage_collector.hpp>
#include <tgfx2/i_render_device.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_context.hpp>
#include <termin/render/resource_spec.hpp>
#include <termin/render/tc_pass.hpp>
#include <termin/render/execute_context.hpp>
#include <core/tc_component.h>

#include <tgfx2/render_context.hpp>

#include <termin/tc_scene.hpp>

namespace nb = nanobind;

namespace termin {

template<typename T>
void init_render_pass_from_python(T* self, const char* type_name) {
    self->link_to_type_registry(type_name);
    nb::object wrapper = nb::cast(self, nb::rv_policy::reference);
    self->set_language_body(wrapper.ptr(), TC_LANGUAGE_PYTHON);
}

void bind_tc_pass_runtime(nb::module_& m);
void bind_scene_pipeline_template(nb::module_& m);
void bind_render_engine(nb::module_& m);
void bind_render_pipeline(nb::module_& m);

static Mat44f mat44f_from_buffer_compatible_object(nb::object value, const char* field_name) {
    try {
        auto arr = nb::cast<nb::ndarray<float, nb::shape<4, 4>, nb::c_contig, nb::device::cpu>>(value);
        Mat44f matrix;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                matrix.data[col * 4 + row] = arr(row, col);
            }
        }
        return matrix;
    } catch (const std::exception& e) {
        tc::Log::error(
            "RenderContext %s expects a 4x4 C-contiguous float32 buffer-compatible matrix: %s",
            field_name,
            e.what());
        throw std::runtime_error(
            std::string("RenderContext ") + field_name +
            " expects a 4x4 C-contiguous float32 buffer-compatible matrix");
    }
}

} // namespace termin

void bind_tc_render_target(nb::module_& m);

namespace termin {

void bind_render_framework(nb::module_& m) {
    // Force-import _tgfx_native so that its nb::class_<tgfx::RenderContext2>
    // and other tgfx2 handle registrations are live before we start
    // binding properties that return those types. Without this, the
    // first access of ctx.ctx2 from Python crashes with "Unable to
    // convert function return value to a Python type!" — the global
    // nanobind type map for RenderContext2 is still empty because
    // _tgfx_native hasn't been loaded yet at that moment.
    try {
        nb::module_::import_("tgfx._tgfx_native");
    } catch (const std::exception& e) {
        // If tgfx isn't available (minimal builds), ctx2 property will
        // degrade but everything else still works.
        tc::Log::debug("[render_framework] tgfx._tgfx_native import failed (minimal build?): %s", e.what());
    }

    nb::enum_<TextureFilter>(m, "TextureFilter")
        .value("LINEAR", TextureFilter::LINEAR)
        .value("NEAREST", TextureFilter::NEAREST);

    nb::class_<RenderCamera>(m, "RenderCamera")
        .def(nb::init<>())
        .def_rw("view", &RenderCamera::view)
        .def_rw("projection", &RenderCamera::projection)
        .def_rw("position", &RenderCamera::position)
        .def_rw("near_clip", &RenderCamera::near_clip)
        .def_rw("far_clip", &RenderCamera::far_clip)
        .def("get_view_matrix", &RenderCamera::get_view_matrix)
        .def("get_projection_matrix", &RenderCamera::get_projection_matrix)
        .def("view_matrix", &RenderCamera::view_matrix)
        .def("projection_matrix", &RenderCamera::projection_matrix)
        .def("get_position", &RenderCamera::get_position);

    m.def("collect_scene_shader_usages", &collect_scene_shader_usages,
          nb::arg("scene"),
          "Collect tc_shader usages declared by drawable components in a scene.");
    m.def("collect_shader_usages_for_pipeline", &collect_shader_usages_for_pipeline,
          nb::arg("scene"),
          nb::arg("pipeline"),
          "Collect tc_shader usages required by enabled passes in a render pipeline.");

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

    nb::class_<Rect2i>(m, "Rect2i")
        .def(nb::init<>())
        .def(nb::init<int, int, int, int>(),
             nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))
        .def_rw("x", &Rect2i::x)
        .def_rw("y", &Rect2i::y)
        .def_rw("width", &Rect2i::width)
        .def_rw("height", &Rect2i::height)
        .def("__len__", [](const Rect2i&) {
            return 4;
        })
        .def("__getitem__", [](const Rect2i& self, size_t index) {
            switch (index) {
                case 0: return self.x;
                case 1: return self.y;
                case 2: return self.width;
                case 3: return self.height;
                default: throw nb::index_error();
            }
        })
        .def("__iter__", [](const Rect2i& self) {
            return nb::make_iterator(
                nb::type<Rect2i>(),
                "rect2i_iter",
                &self.x,
                &self.x + 4
            );
        }, nb::keep_alive<0, 1>())
        .def("__repr__", [](const Rect2i& self) {
            return "Rect2i(" +
                std::to_string(self.x) + ", " +
                std::to_string(self.y) + ", " +
                std::to_string(self.width) + ", " +
                std::to_string(self.height) + ")";
        });

    nb::class_<FrameGraphResource>(m, "FrameGraphResource")
        .def("resource_type", &FrameGraphResource::resource_type);

    nb::class_<ExecuteContext>(m, "ExecuteContext")
        .def(nb::init<>())
        .def("__init__", [](ExecuteContext* self, nb::kwargs kwargs) {
            new (self) ExecuteContext();
            if (kwargs.contains("render_rect")) {
                nb::tuple t = nb::cast<nb::tuple>(kwargs["render_rect"]);
                self->render_rect.x = nb::cast<int>(t[0]);
                self->render_rect.y = nb::cast<int>(t[1]);
                self->render_rect.width = nb::cast<int>(t[2]);
                self->render_rect.height = nb::cast<int>(t[3]);
            }
            if (kwargs.contains("scene")) {
                nb::object s = nb::borrow<nb::object>(kwargs["scene"]);
                if (!s.is_none()) {
                    if (nb::isinstance<TcSceneRef>(s)) {
                        self->scene = nb::cast<TcSceneRef>(s);
                    } else {
                        self->scene = nb::cast<TcSceneRef>(s);
                    }
                }
            }
            if (kwargs.contains("render_target_name")) {
                self->render_target_name = nb::cast<std::string>(kwargs["render_target_name"]);
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
            if (kwargs.contains("render_category_mask")) {
                self->render_category_mask = nb::cast<uint64_t>(kwargs["render_category_mask"]);
            }
        })
        .def_prop_rw("camera",
            [](const ExecuteContext& ctx) -> RenderCamera* { return ctx.camera; },
            [](ExecuteContext& ctx, RenderCamera* camera) { ctx.camera = camera; },
            nb::rv_policy::reference)
        .def_rw("render_target_name", &ExecuteContext::render_target_name)
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
        .def_rw("render_rect", &ExecuteContext::render_rect)
        .def_rw("scene", &ExecuteContext::scene)
        .def_rw("lights", &ExecuteContext::lights)
        .def_rw("layer_mask", &ExecuteContext::layer_mask)
        .def_rw("render_category_mask", &ExecuteContext::render_category_mask)
        // Stage 7: expose the tgfx2 RenderContext2 pointer so Python
        // passes can open a ctx2 render pass, bind shaders/textures,
        // and dispatch draws without going through the legacy
        // GraphicsBackend. None if the frame was set up without tgfx2
        // (e.g., TERMIN_DISABLE_TGFX2=1 escape hatch).
        .def_prop_ro("ctx2",
            [](const ExecuteContext& ctx) -> tgfx::RenderContext2* {
                return ctx.ctx2;
            },
            nb::rv_policy::reference)
        // Stage 8.3: tgfx2 texture maps for render pass inputs/outputs,
        // parallel to reads_fbos/writes_fbos. New Python passes read from
        // these directly and call ctx2 methods; no FBO wrapping.
        .def_prop_ro("tex2_reads",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.tex2_reads) {
                    result[nb::str(key.c_str())] = nb::cast(val);
                }
                return result;
            })
        .def_prop_ro("tex2_writes",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.tex2_writes) {
                    result[nb::str(key.c_str())] = nb::cast(val);
                }
                return result;
            })
        .def_prop_ro("tex2_depth_reads",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.tex2_depth_reads) {
                    result[nb::str(key.c_str())] = nb::cast(val);
                }
                return result;
            })
        .def_prop_ro("tex2_depth_writes",
            [](const ExecuteContext& ctx) -> nb::dict {
                nb::dict result;
                for (const auto& [key, val] : ctx.tex2_depth_writes) {
                    result[nb::str(key.c_str())] = nb::cast(val);
                }
                return result;
            });

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

    nb::class_<GraphAliasPass, CxxFramePass>(m, "GraphAliasPass")
        .def("__init__", [](GraphAliasPass* self,
            std::vector<std::string> read_resources,
            std::vector<std::string> write_resources,
            std::vector<std::string> alias_resources,
            const std::string& pass_name
        ) {
            new (self) GraphAliasPass(
                std::move(read_resources),
                std::move(write_resources),
                std::move(alias_resources),
                pass_name
            );
            init_render_pass_from_python(self, "GraphAliasPass");
        },
            nb::arg("read_resources") = std::vector<std::string>{},
            nb::arg("write_resources") = std::vector<std::string>{},
            nb::arg("alias_resources") = std::vector<std::string>{},
            nb::arg("pass_name") = "GraphAliasPass")
        .def_rw("read_resources", &GraphAliasPass::read_resources)
        .def_rw("write_resources", &GraphAliasPass::write_resources)
        .def_rw("alias_resources", &GraphAliasPass::alias_resources)
        .def_prop_ro("reads", &GraphAliasPass::compute_reads)
        .def_prop_ro("writes", &GraphAliasPass::compute_writes)
        .def("get_inplace_aliases", &GraphAliasPass::get_inplace_aliases);

    nb::class_<FrameDebugCapturePass, CxxFramePass>(m, "FrameDebugCapturePass")
        .def("__init__", [](FrameDebugCapturePass* self, const std::string& pass_name) {
            new (self) FrameDebugCapturePass(pass_name);
            init_render_pass_from_python(self, "FrameDebugCapturePass");
        }, nb::arg("pass_name") = "FrameDebugger")
        .def_rw("source_resource", &FrameDebugCapturePass::source_resource)
        .def_rw("source_type", &FrameDebugCapturePass::source_type)
        .def_rw("paused", &FrameDebugCapturePass::paused)
        .def("set_source_resource", &FrameDebugCapturePass::set_source_resource)
        .def("set_source_type", &FrameDebugCapturePass::set_source_type)
        .def("set_paused", &FrameDebugCapturePass::set_paused)
        .def("set_capture", &FrameDebugCapturePass::set_capture, nb::arg("capture"))
        .def("set_depth_capture", &FrameDebugCapturePass::set_depth_capture, nb::arg("capture"))
        .def_prop_ro("reads", [](FrameDebugCapturePass& p) {
            auto reads = p.compute_reads();
            std::set<std::string> result;
            for (const char* r : reads) {
                if (r) {
                    result.insert(r);
                }
            }
            return result;
        })
        .def_prop_ro("writes", [](FrameDebugCapturePass& p) {
            auto writes = p.compute_writes();
            std::set<std::string> result;
            for (const char* w : writes) {
                if (w) {
                    result.insert(w);
                }
            }
            return result;
        });

    {
        m.attr("GraphAliasPass").attr("category") = "Graph";
        m.attr("GraphAliasPass").attr("node_inputs") = nb::make_tuple();
        m.attr("GraphAliasPass").attr("node_outputs") = nb::make_tuple();
        m.attr("GraphAliasPass").attr("node_inplace_pairs") = nb::make_tuple();
    }

    nb::class_<RenderContext>(m, "RenderContext")
        .def(nb::init<>())
        .def("__init__", [](RenderContext* self, nb::kwargs kwargs) {
            new (self) RenderContext();

            if (kwargs.contains("phase")) {
                nb::handle value = kwargs["phase"];
                if (nb::isinstance<nb::str>(value)) {
                    const std::string name = nb::cast<std::string>(value);
                    const tc_phase_mask phase = tc_phase_find(name.c_str());
                    if (phase == TC_PHASE_NONE) {
                        throw nb::value_error(
                            ("RenderContext phase '" + name
                             + "' is not present in the project render-phase registry")
                                .c_str());
                    }
                    self->phase = phase;
                } else {
                    self->phase = nb::cast<tc_phase_mask>(value);
                }
            }
            if (kwargs.contains("scene")) {
                nb::object s = nb::borrow<nb::object>(kwargs["scene"]);
                if (!s.is_none()) {
                    if (nb::isinstance<TcSceneRef>(s)) {
                        self->scene = nb::cast<TcSceneRef>(s);
                    } else {
                        self->scene = nb::cast<TcSceneRef>(s);
                    }
                }
            }
            if (kwargs.contains("layer_mask")) {
                self->layer_mask = nb::cast<uint64_t>(kwargs["layer_mask"]);
            }
            if (kwargs.contains("render_category_mask")) {
                self->render_category_mask = nb::cast<uint64_t>(kwargs["render_category_mask"]);
            }
            if (kwargs.contains("view")) {
                nb::object v = nb::borrow<nb::object>(kwargs["view"]);
                if (nb::isinstance<Mat44>(v)) {
                    self->view = nb::cast<Mat44>(v).to_float();
                } else {
                    self->view = mat44f_from_buffer_compatible_object(v, "view");
                }
            }
            if (kwargs.contains("projection")) {
                nb::object p = nb::borrow<nb::object>(kwargs["projection"]);
                if (nb::isinstance<Mat44>(p)) {
                    self->projection = nb::cast<Mat44>(p).to_float();
                } else {
                    self->projection = mat44f_from_buffer_compatible_object(p, "projection");
                }
            }
            if (kwargs.contains("model")) {
                nb::object model = nb::borrow<nb::object>(kwargs["model"]);
                if (nb::isinstance<Mat44>(model)) {
                    self->model = nb::cast<Mat44>(model).to_float();
                } else {
                    self->model = mat44f_from_buffer_compatible_object(model, "model");
                }
            }
            if (kwargs.contains("camera")) {
                nb::object camera = nb::borrow<nb::object>(kwargs["camera"]);
                if (!camera.is_none()) {
                    self->camera = nb::cast<RenderCamera*>(camera);
                }
            }
        })
        .def_rw("phase", &RenderContext::phase)
        .def_rw("scene", &RenderContext::scene)
        .def_rw("layer_mask", &RenderContext::layer_mask)
        .def_rw("render_category_mask", &RenderContext::render_category_mask)
        .def_rw("view", &RenderContext::view)
        .def_rw("projection", &RenderContext::projection)
        .def_rw("model", &RenderContext::model)
        .def_prop_rw("camera",
            [](const RenderContext& self) -> RenderCamera* { return self.camera; },
            [](RenderContext& self, RenderCamera* camera) { self.camera = camera; },
            nb::rv_policy::reference)
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

    nb::class_<TextureInfo>(m, "TextureInfo")
        .def(nb::init<>())
        .def_ro("width", &TextureInfo::width)
        .def_ro("height", &TextureInfo::height)
        .def_ro("samples", &TextureInfo::samples)
        .def_ro("is_msaa", &TextureInfo::is_msaa)
        .def_ro("format", &TextureInfo::format)
        .def_ro("format_name", &TextureInfo::format_name);

    nb::class_<FrameGraphCapture>(m, "FrameGraphCapture")
        .def(nb::init<>())
        .def("set_target", [](FrameGraphCapture& self, CxxFramePass* pass) {
            self.set_target(pass);
        }, nb::arg("pass"), nb::rv_policy::reference)
        .def("clear_target", &FrameGraphCapture::clear_target)
        .def("capture_direct_via_ctx2",
             &FrameGraphCapture::capture_direct_via_ctx2,
             nb::arg("ctx2"), nb::arg("src_tex"),
             nb::arg("width"), nb::arg("height"),
             nb::arg("format") = tgfx::PixelFormat::RGBA8_UNorm)
        .def("has_capture", &FrameGraphCapture::has_capture)
        .def("reset_capture", &FrameGraphCapture::reset_capture)
        .def_prop_ro("capture_tex", &FrameGraphCapture::capture_tex)
        .def_prop_ro("width", &FrameGraphCapture::width)
        .def_prop_ro("height", &FrameGraphCapture::height)
        .def_prop_ro("format", &FrameGraphCapture::format)
        .def_prop_ro("is_depth", &FrameGraphCapture::is_depth);

    nb::class_<FrameGraphPresenter>(m, "FrameGraphPresenter")
        .def(nb::init<>())
        .def("render",
             [](FrameGraphPresenter& self,
                tgfx::RenderContext2* ctx2,
                tgfx::TextureHandle capture_tex,
                tgfx::TextureHandle target_tex,
                int dst_x,
                int dst_y,
                int dst_w,
                int dst_h,
                int channel_mode,
                bool highlight_hdr) {
                 FrameGraphPresenterDraw draw;
                 draw.capture_tex = capture_tex;
                 draw.dst_rect = Rect2i{dst_x, dst_y, dst_w, dst_h};
                 draw.options.channel_mode = channel_mode;
                 draw.options.highlight_hdr = highlight_hdr;
                 self.render(ctx2, target_tex, draw);
             },
             nb::arg("ctx2"), nb::arg("capture_tex"), nb::arg("target_tex"),
             nb::arg("dst_x"), nb::arg("dst_y"),
             nb::arg("dst_w"), nb::arg("dst_h"),
             nb::arg("channel_mode"), nb::arg("highlight_hdr"))
        .def("render_in_current_pass",
             [](FrameGraphPresenter& self,
                tgfx::RenderContext2* ctx2,
                tgfx::TextureHandle capture_tex,
                int dst_x,
                int dst_y,
                int dst_w,
                int dst_h,
                int channel_mode,
                bool highlight_hdr) {
                 FrameGraphPresenterDraw draw;
                 draw.capture_tex = capture_tex;
                 draw.dst_rect = Rect2i{dst_x, dst_y, dst_w, dst_h};
                 draw.options.channel_mode = channel_mode;
                 draw.options.highlight_hdr = highlight_hdr;
                 self.render_in_current_pass(ctx2, draw);
             },
             nb::arg("ctx2"), nb::arg("capture_tex"),
             nb::arg("dst_x"), nb::arg("dst_y"),
             nb::arg("dst_w"), nb::arg("dst_h"),
             nb::arg("channel_mode"), nb::arg("highlight_hdr"))
        .def("compute_hdr_stats", &FrameGraphPresenter::compute_hdr_stats,
             nb::arg("device"), nb::arg("tex"))
        .def("read_depth_normalized",
             [](FrameGraphPresenter& self, tgfx::IRenderDevice* device,
                tgfx::TextureHandle tex) -> nb::object {
                 int w = 0, h = 0;
                 auto data = self.read_depth_normalized(device, tex, &w, &h);
                 if (data.empty()) {
                     return nb::none();
                 }
                 nb::bytes bytes_obj(
                     reinterpret_cast<const char*>(data.data()),
                     data.size());
                 return nb::make_tuple(bytes_obj, w, h);
             },
             nb::arg("device"), nb::arg("tex"))
        .def_static("get_texture_info",
             &FrameGraphPresenter::get_texture_info,
             nb::arg("device"), nb::arg("tex"));

    nb::class_<FrameGraphDebuggerCore>(m, "FrameGraphDebuggerCore")
        .def(nb::init<>())
        .def_prop_ro("capture_tex", &FrameGraphDebuggerCore::capture_tex)
        .def_prop_ro("depth_capture_tex", &FrameGraphDebuggerCore::depth_capture_tex)
        .def_prop_ro("capture", [](FrameGraphDebuggerCore& self) -> FrameGraphCapture& {
            return self.capture;
        }, nb::rv_policy::reference_internal)
        .def_prop_ro("depth_capture", [](FrameGraphDebuggerCore& self) -> FrameGraphCapture& {
            return self.depth_capture;
        }, nb::rv_policy::reference_internal)
        .def_prop_ro("presenter", [](FrameGraphDebuggerCore& self) -> FrameGraphPresenter& {
            return self.presenter;
        }, nb::rv_policy::reference_internal);

    bind_tc_pass_runtime(m);
    bind_scene_pipeline_template(m);
}

} // namespace termin

NB_MODULE(_render_framework_native, m) {
    m.doc() = "Native render framework bindings";

    nb::module_::import_("tgfx._tgfx_native");
    nb::module_::import_("tcbase._geom_native");
    nb::module_::import_("termin.scene._scene_native");
    nb::module_::import_("termin.lighting._lighting_native");

    termin::bind_render_framework(m);
    termin::bind_render_engine(m);
    termin::bind_render_pipeline(m);
    bind_tc_render_target(m);
}
