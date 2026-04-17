// tgfx2_bindings.cpp - Python bindings for tgfx2 types used by
// migrated Python render passes (PostProcessPass, posteffects/*, etc.)
//
// Scope: the minimum surface a Python pass needs to drive a draw through
// RenderContext2 — open a pass, bind a shader compiled from TcShader,
// bind sampled textures (wrapped from FramebufferHandle), set plain
// uniforms, and dispatch draw_fullscreen_quad / draw. This is NOT a
// general-purpose tgfx2 binding — most of the API surface (pipeline
// cache internals, command list, device factory, vulkan support) stays
// C++ only.

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/ndarray.h>

#include <memory>

#include <tgfx2/render_context.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/vertex_layout.hpp>
#include <tgfx2/tc_shader_bridge.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/text2d_renderer.hpp>
#include <tgfx2/text3d_renderer.hpp>

#include <tgfx/tgfx_shader_handle.hpp>
#include <tgfx/tgfx_mesh_handle.hpp>
#include <tgfx/tgfx2_interop.h>
#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tc_gpu_share_group.h>
#include <tgfx/tgfx_types.h>

namespace nb = nanobind;

namespace tgfx_bindings {

void bind_tgfx2(nb::module_& m) {
    // --- Opaque handle wrappers ---
    //
    // HandlePool entries in tgfx2 are value-typed (32-bit id). We wrap
    // them in lightweight Python objects so they have identity and
    // can be passed between Python calls without copies. The handles
    // themselves are POD structs with a single uint32_t member.

    nb::class_<tgfx::TextureHandle>(m, "Tgfx2TextureHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx::TextureHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx::TextureHandle& h) { return static_cast<bool>(h); });

    nb::class_<tgfx::BufferHandle>(m, "Tgfx2BufferHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx::BufferHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx::BufferHandle& h) { return static_cast<bool>(h); });

    nb::class_<tgfx::ShaderHandle>(m, "Tgfx2ShaderHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx::ShaderHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx::ShaderHandle& h) { return static_cast<bool>(h); });

    // Paired (vs, fs) return type for tc_shader_ensure_tgfx2 — easier
    // to return one object than two out parameters.
    struct ShaderPair {
        tgfx::ShaderHandle vs;
        tgfx::ShaderHandle fs;
    };
    nb::class_<ShaderPair>(m, "Tgfx2ShaderPair")
        .def_ro("vs", &ShaderPair::vs)
        .def_ro("fs", &ShaderPair::fs);

    // IRenderDevice — opaque handle exposed so other native modules
    // (render_framework) can accept a pointer to it from Python.
    // The device is owned by whoever created it (RenderEngine,
    // Tgfx2ContextHolder). Python code only passes the pointer around.
    nb::class_<tgfx::IRenderDevice>(m, "Tgfx2Device");

    // BlendFactor enum — exposed so Python callers can request
    // premultiplied / additive / standard blending via set_blend_func.
    nb::enum_<tgfx::BlendFactor>(m, "Tgfx2BlendFactor", nb::is_arithmetic())
        .value("Zero",              tgfx::BlendFactor::Zero)
        .value("One",               tgfx::BlendFactor::One)
        .value("SrcAlpha",          tgfx::BlendFactor::SrcAlpha)
        .value("OneMinusSrcAlpha",  tgfx::BlendFactor::OneMinusSrcAlpha)
        .value("DstAlpha",          tgfx::BlendFactor::DstAlpha)
        .value("OneMinusDstAlpha",  tgfx::BlendFactor::OneMinusDstAlpha)
        .value("SrcColor",          tgfx::BlendFactor::SrcColor)
        .value("OneMinusSrcColor",  tgfx::BlendFactor::OneMinusSrcColor)
        .value("DstColor",          tgfx::BlendFactor::DstColor)
        .value("OneMinusDstColor",  tgfx::BlendFactor::OneMinusDstColor)
        .export_values();

    // Register PixelFormat as an nb::enum_ so other native modules can
    // bind functions with `tgfx::PixelFormat` parameters and defaults.
    // The `is_arithmetic` flag lets Python int callers pass values via
    // the legacy `PIXEL_*` module-level int constants below.
    // Must be registered BEFORE any binding that uses PixelFormat as
    // an argument type or default value (otherwise nanobind's
    // enum_from_cpp() throws std::bad_cast at module init).
    nb::enum_<tgfx::PixelFormat>(m, "Tgfx2PixelFormat", nb::is_arithmetic())
        .value("R8_UNorm",          tgfx::PixelFormat::R8_UNorm)
        .value("RG8_UNorm",         tgfx::PixelFormat::RG8_UNorm)
        .value("RGB8_UNorm",        tgfx::PixelFormat::RGB8_UNorm)
        .value("RGBA8_UNorm",       tgfx::PixelFormat::RGBA8_UNorm)
        .value("BGRA8_UNorm",       tgfx::PixelFormat::BGRA8_UNorm)
        .value("R16F",              tgfx::PixelFormat::R16F)
        .value("RG16F",             tgfx::PixelFormat::RG16F)
        .value("RGBA16F",           tgfx::PixelFormat::RGBA16F)
        .value("R32F",              tgfx::PixelFormat::R32F)
        .value("RG32F",             tgfx::PixelFormat::RG32F)
        .value("RGBA32F",           tgfx::PixelFormat::RGBA32F)
        .value("D24_UNorm",         tgfx::PixelFormat::D24_UNorm)
        .value("D24_UNorm_S8_UInt", tgfx::PixelFormat::D24_UNorm_S8_UInt)
        .value("D32F",              tgfx::PixelFormat::D32F)
        .export_values();

    // --- RenderContext2 ---
    //
    // Only the methods Python passes actually need are exposed. The
    // rest (bind_uniform_buffer, set_vertex_layout, push constants, ...)
    // stay C++-only because their Python form would require binding
    // descriptor structs that Python code doesn't need.
    nb::class_<tgfx::RenderContext2>(m, "Tgfx2RenderContext")
        .def("begin_pass",
             [](tgfx::RenderContext2& self,
                tgfx::TextureHandle color,
                tgfx::TextureHandle depth,
                bool clear_color_enabled,
                float r, float g, float b, float a,
                float clear_depth,
                bool clear_depth_enabled) {
                 float clear_rgba[4] = {r, g, b, a};
                 self.begin_pass(color, depth,
                                 clear_color_enabled ? clear_rgba : nullptr,
                                 clear_depth,
                                 clear_depth_enabled);
             },
             nb::arg("color"),
             nb::arg("depth") = tgfx::TextureHandle{},
             nb::arg("clear_color_enabled") = false,
             nb::arg("r") = 0.0f, nb::arg("g") = 0.0f,
             nb::arg("b") = 0.0f, nb::arg("a") = 1.0f,
             nb::arg("clear_depth") = 1.0f,
             nb::arg("clear_depth_enabled") = false)
        .def("end_pass", &tgfx::RenderContext2::end_pass)

        // Frame lifecycle — standalone Python hosts (SDL window, tests)
        // must call these manually once per frame. Inside the engine
        // render loop the C++ frame graph handles this already.
        .def("begin_frame", &tgfx::RenderContext2::begin_frame)
        .def("end_frame", &tgfx::RenderContext2::end_frame)

        // State
        .def("set_depth_test", &tgfx::RenderContext2::set_depth_test)
        .def("set_depth_write", &tgfx::RenderContext2::set_depth_write)
        .def("set_blend", &tgfx::RenderContext2::set_blend)
        .def("set_blend_func", &tgfx::RenderContext2::set_blend_func,
             nb::arg("src"), nb::arg("dst"))
        .def("set_cull",
             [](tgfx::RenderContext2& self, int mode) {
                 self.set_cull(static_cast<tgfx::CullMode>(mode));
             })
        .def("set_color_format",
             [](tgfx::RenderContext2& self, int fmt) {
                 self.set_color_format(static_cast<tgfx::PixelFormat>(fmt));
             })
        .def("set_color_mask", &tgfx::RenderContext2::set_color_mask)
        .def("set_viewport", &tgfx::RenderContext2::set_viewport)
        .def("set_scissor", &tgfx::RenderContext2::set_scissor)
        .def("clear_scissor", &tgfx::RenderContext2::clear_scissor)

        // Blit (src → dst texture).
        .def("blit", &tgfx::RenderContext2::blit)

        // Wrap a raw GL texture id as a non-owning tgfx2 TextureHandle
        // using this context's device. Used by Python debugger code
        // that needs to turn a host-owned GL texture (e.g. tcgui
        // FBOSurface's color attachment) into a texture handle the
        // presenter can render into.
        .def("wrap_gl_texture",
             [](tgfx::RenderContext2& self, uint32_t gl_id,
                uint32_t w, uint32_t h, int format_int) -> tgfx::TextureHandle {
                 tgfx::TextureDesc desc;
                 desc.width = w;
                 desc.height = h;
                 desc.format = static_cast<tgfx::PixelFormat>(format_int);
                 desc.usage = tgfx::TextureUsage::Sampled |
                              tgfx::TextureUsage::ColorAttachment;
                 // Throws on non-GL backends; the `wrap_gl_texture` name
                 // advertises the GL-specific semantics.
                 return self.device().register_external_texture(
                     static_cast<uintptr_t>(gl_id), desc);
             },
             nb::arg("gl_id"), nb::arg("width"), nb::arg("height"),
             nb::arg("format"))

        // Destroy a texture handle on this context's device. For
        // external wraps this just frees the HandlePool entry; the
        // GL texture stays owned by the caller.
        .def("destroy_texture",
             [](tgfx::RenderContext2& self, tgfx::TextureHandle h) {
                 if (h) self.device().destroy(h);
             })

        // Create an offscreen color attachment via the context's
        // device. Mirrors Tgfx2Context.create_color_attachment for
        // callers that only hold a RenderContext2 (effects running
        // inside a pipeline pass, etc.).
        .def("create_color_attachment",
             [](tgfx::RenderContext2& self, uint32_t w, uint32_t h,
                tgfx::PixelFormat fmt) -> tgfx::TextureHandle {
                 tgfx::TextureDesc desc;
                 desc.width = w;
                 desc.height = h;
                 desc.format = fmt;
                 desc.usage = tgfx::TextureUsage::Sampled |
                              tgfx::TextureUsage::ColorAttachment |
                              tgfx::TextureUsage::CopyDst;
                 return self.device().create_texture(desc);
             },
             nb::arg("width"), nb::arg("height"),
             nb::arg("format") = tgfx::PixelFormat::RGBA8_UNorm)

        // Present a texture to an externally-owned GL FBO (0 = host
        // window default framebuffer). Used by legacy Qt debugger
        // window to composite the debugger capture onto its SDL
        // debug window.
        .def("blit_to_external_fbo",
             [](tgfx::RenderContext2& self, uint32_t dst_fbo_id,
                tgfx::TextureHandle src,
                int src_x, int src_y, int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h) {
                 self.device().blit_to_external_target(
                     static_cast<uintptr_t>(dst_fbo_id), src,
                     src_x, src_y, src_w, src_h,
                     dst_x, dst_y, dst_w, dst_h);
             },
             nb::arg("dst_fbo_id"), nb::arg("src"),
             nb::arg("src_x"), nb::arg("src_y"),
             nb::arg("src_w"), nb::arg("src_h"),
             nb::arg("dst_x"), nb::arg("dst_y"),
             nb::arg("dst_w"), nb::arg("dst_h"))

        // Bind an externally-owned GL FBO (0 = default window FBO)
        // and clear it to the given colour / depth. Host window code
        // uses this to paint the background before compositing UI.
        .def("clear_external_fbo",
             [](tgfx::RenderContext2& self, uint32_t dst_fbo_id,
                float r, float g, float b, float a, float depth,
                int viewport_x, int viewport_y,
                int viewport_w, int viewport_h) {
                 self.device().clear_external_target(
                     static_cast<uintptr_t>(dst_fbo_id), r, g, b, a, depth,
                     viewport_x, viewport_y, viewport_w, viewport_h);
             },
             nb::arg("dst_fbo_id"),
             nb::arg("r"), nb::arg("g"), nb::arg("b"), nb::arg("a"),
             nb::arg("depth"),
             nb::arg("viewport_x"), nb::arg("viewport_y"),
             nb::arg("viewport_w"), nb::arg("viewport_h"))

        // Diagnostic: glGetError() wrapper. glad lives inside
        // termin_graphics2.dll and is initialised by the host, so
        // this call is always safe from the binding side where a
        // direct glGetError would crash on a null function pointer.
        .def("last_gl_error", &tgfx::RenderContext2::last_gl_error)

        // Shader
        .def("bind_shader",
             [](tgfx::RenderContext2& self,
                tgfx::ShaderHandle vs, tgfx::ShaderHandle fs) {
                 self.bind_shader(vs, fs, {});
             })

        // Resource bindings
        .def("bind_sampled_texture",
             [](tgfx::RenderContext2& self, uint32_t slot, tgfx::TextureHandle tex) {
                 self.bind_sampled_texture(slot, tex, {});
             })

        // Transitional plain-uniform setters
        .def("set_uniform_int",
             [](tgfx::RenderContext2& self, const std::string& name, int value) {
                 self.set_uniform_int(name.c_str(), value);
             })
        .def("set_uniform_float",
             [](tgfx::RenderContext2& self, const std::string& name, float value) {
                 self.set_uniform_float(name.c_str(), value);
             })
        .def("set_uniform_vec2",
             [](tgfx::RenderContext2& self, const std::string& name,
                float x, float y) {
                 self.set_uniform_vec2(name.c_str(), x, y);
             })
        .def("set_uniform_vec3",
             [](tgfx::RenderContext2& self, const std::string& name,
                float x, float y, float z) {
                 self.set_uniform_vec3(name.c_str(), x, y, z);
             })
        .def("set_uniform_vec4",
             [](tgfx::RenderContext2& self, const std::string& name,
                float x, float y, float z, float w) {
                 self.set_uniform_vec4(name.c_str(), x, y, z, w);
             })
        .def("set_uniform_mat4",
             [](tgfx::RenderContext2& self, const std::string& name,
                nb::handle data, bool transpose) {
                 // Accept any buffer-like object (Mat44.data, list of 16 floats,
                 // numpy array). nanobind's nb::ndarray would be cleanest but
                 // requires extra includes — simpler: iterate and cast.
                 float m[16];
                 auto seq = nb::cast<nb::sequence>(data);
                 int i = 0;
                 for (auto v : seq) {
                     if (i >= 16) break;
                     m[i++] = nb::cast<float>(v);
                 }
                 if (i == 16) {
                     self.set_uniform_mat4(name.c_str(), m, transpose);
                 }
             },
             nb::arg("name"), nb::arg("data"), nb::arg("transpose") = false)
        .def("set_block_binding",
             [](tgfx::RenderContext2& self, const std::string& name, uint32_t slot) {
                 self.set_block_binding(name.c_str(), slot);
             })

        // Draw
        .def("draw_fullscreen_quad", &tgfx::RenderContext2::draw_fullscreen_quad)

        // Immediate drawing — creates a throwaway VBO, draws, destroys.
        // Vertex format is fixed to 7 floats per vertex:
        //   [x, y, z,  r, g, b, a]
        // At the GL level this is loc 0 = vec3 (position) and
        // loc 1 = vec4 (color). Consumers are free to reinterpret loc 1
        // inside their shader (e.g. pack offset.xy + uv.xy into a vec4
        // for billboard text) as long as the stride and attribute sizes
        // match.
        // The currently bound shader (via bind_shader) is used.
        .def("draw_immediate_triangles",
             [](tgfx::RenderContext2& self,
                nb::ndarray<float, nb::c_contig, nb::device::cpu> verts,
                uint32_t vertex_count) {
                 self.draw_immediate_triangles(verts.data(), vertex_count);
             },
             nb::arg("verts"), nb::arg("vertex_count"))
        .def("draw_immediate_lines",
             [](tgfx::RenderContext2& self,
                nb::ndarray<float, nb::c_contig, nb::device::cpu> verts,
                uint32_t vertex_count) {
                 self.draw_immediate_lines(verts.data(), vertex_count);
             },
             nb::arg("verts"), nb::arg("vertex_count"));

    // --- Tgfx2Context holder ---
    //
    // Owns a tgfx::OpenGLRenderDevice + PipelineCache + RenderContext2
    // triple. Mirrors what RenderEngine::ensure_tgfx2 does in C++:
    // device is created over the current GL context (no explicit GL
    // handle — it just assumes the context is current when the first
    // resource is created), cache wraps the device, ctx wraps both.
    //
    // Standalone Python hosts (SDL demos, tests, the migrated tcgui
    // UIRenderer) construct one of these once after the GL context is
    // made current and keep it alive for the lifetime of the window.
    //
    // Destruction order is declaration-reverse: ctx, then cache, then
    // device — which is the correct dependency order.
    struct Tgfx2ContextHolder {
        std::unique_ptr<tgfx::IRenderDevice> device;
        std::unique_ptr<tgfx::PipelineCache> cache;
        std::unique_ptr<tgfx::RenderContext2> ctx;

        Tgfx2ContextHolder() {
            // Backend selected by TERMIN_BACKEND env-var (default OpenGL).
            device = tgfx::create_device(tgfx::default_backend_from_env());
            cache = std::make_unique<tgfx::PipelineCache>(*device);
            ctx = std::make_unique<tgfx::RenderContext2>(*device, *cache);
            // Ensure a default tc_gpu_context exists and is current so
            // tc_shader_ensure_tgfx2 / TcShader compile can find a GPU
            // slot to cache into. Standalone Python hosts that skip the
            // legacy GraphicsBackend.ensure_ready() need this here.
            tc_ensure_default_gpu_context();
            // Install the tgfx2-backed gpu_ops vtable so legacy
            // tc_shader_compile_gpu / tc_mesh_upload_gpu /
            // tc_texture_upload_gpu resource upload paths route through
            // this device. The legacy gpu_ops forwarder reads back raw
            // GL ids from tgfx2 handles for backward-compat with
            // code that still speaks GL (tmesh widgets, TcShader.use).
            // It is GL-specific — skip for non-GL backends, those hosts
            // must avoid the legacy tc_mesh/tc_texture upload paths.
            if (dynamic_cast<tgfx::OpenGLRenderDevice*>(device.get())) {
                tgfx2_interop_set_device(device.get());
                tgfx2_gpu_ops_register();
            }
        }
    };

    nb::class_<Tgfx2ContextHolder>(m, "Tgfx2Context")
        .def(nb::init<>())

        // The underlying RenderContext2. Lifetime is tied to the
        // holder; the reference stays valid until the holder is
        // destroyed.
        .def_prop_ro("context",
            [](Tgfx2ContextHolder& self) -> tgfx::RenderContext2& {
                return *self.ctx;
            },
            nb::rv_policy::reference_internal)

        // Switch the tgfx1 resource system (tc_shader_compile_gpu,
        // tc_mesh_upload_gpu, tc_texture_upload_gpu) onto this tgfx2
        // device. After this call, legacy tgfx1-style resource creation
        // routes through IRenderDevice, and the GL ids extracted from
        // tgfx2 handles are what legacy shader.use() / mesh.draw_gpu()
        // see. Required for mixed tgfx1/tgfx2 frames where tgfx1 code
        // still compiles shaders or uploads meshes alongside tgfx2
        // draws.
        //
        // Call once per process. Idempotent in practice — re-registering
        // the same device is a no-op on the vtable install.
        .def("register_interop",
            [](Tgfx2ContextHolder& self) {
                // Legacy gpu_ops forwarder is GL-specific. No-op on
                // other backends — the host must avoid legacy
                // tc_mesh_upload_gpu / tc_texture_upload_gpu entirely.
                if (dynamic_cast<tgfx::OpenGLRenderDevice*>(self.device.get())) {
                    tgfx2_interop_set_device(self.device.get());
                    tgfx2_gpu_ops_register();
                }
            })

        // --- Texture helpers (narrow API; the full IRenderDevice is
        // not exposed to Python) ---

        // Create an R8 texture (typical use: font atlas). `data` is
        // width*height uint8 bytes; pass None-like empty array to skip
        // upload. Returned handle is Sampled | CopyDst.
        .def("create_texture_r8",
            [](Tgfx2ContextHolder& self, uint32_t w, uint32_t h,
               nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data)
            -> tgfx::TextureHandle {
                tgfx::TextureDesc desc;
                desc.width = w;
                desc.height = h;
                desc.format = tgfx::PixelFormat::R8_UNorm;
                desc.usage = tgfx::TextureUsage::Sampled |
                             tgfx::TextureUsage::CopyDst;
                auto handle = self.device->create_texture(desc);
                if (data.size() > 0) {
                    self.device->upload_texture(handle,
                        std::span<const uint8_t>(data.data(), data.size()));
                }
                return handle;
            },
            nb::arg("width"), nb::arg("height"), nb::arg("data"))

        // Create an RGBA8 texture (typical use: UI images). Layout is
        // tightly packed 4-bytes-per-pixel row-major.
        .def("create_texture_rgba8",
            [](Tgfx2ContextHolder& self, uint32_t w, uint32_t h,
               nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data)
            -> tgfx::TextureHandle {
                tgfx::TextureDesc desc;
                desc.width = w;
                desc.height = h;
                desc.format = tgfx::PixelFormat::RGBA8_UNorm;
                desc.usage = tgfx::TextureUsage::Sampled |
                             tgfx::TextureUsage::CopyDst;
                auto handle = self.device->create_texture(desc);
                if (data.size() > 0) {
                    self.device->upload_texture(handle,
                        std::span<const uint8_t>(data.data(), data.size()));
                }
                return handle;
            },
            nb::arg("width"), nb::arg("height"), nb::arg("data"))

        // Full-texture re-upload.
        .def("upload_texture",
            [](Tgfx2ContextHolder& self, tgfx::TextureHandle handle,
               nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data) {
                self.device->upload_texture(handle,
                    std::span<const uint8_t>(data.data(), data.size()));
            },
            nb::arg("handle"), nb::arg("data"))

        // Upload a rectangular sub-region of a texture. ``data`` is a
        // tightly packed ``w * h * bytes_per_pixel`` buffer. Used by
        // incremental update paths (e.g. Canvas overlay stroke).
        .def("upload_texture_region",
            [](Tgfx2ContextHolder& self, tgfx::TextureHandle handle,
               uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> data) {
                self.device->upload_texture_region(
                    handle, x, y, w, h,
                    std::span<const uint8_t>(data.data(), data.size()));
            },
            nb::arg("handle"),
            nb::arg("x"), nb::arg("y"),
            nb::arg("w"), nb::arg("h"),
            nb::arg("data"))

        // Destroy a texture owned by this device.
        .def("destroy_texture",
            [](Tgfx2ContextHolder& self, tgfx::TextureHandle handle) {
                self.device->destroy(handle);
            },
            nb::arg("handle"))

        // Return the raw GL texture id for a handle in this device.
        // Used when a caller needs to hand the texture off to a
        // different Tgfx2Context (e.g. GPUCompositor → UIRenderer) —
        // the receiving holder can wrap the GL id as a non-owning
        // external handle in its own device.
        .def("get_gl_id",
            [](Tgfx2ContextHolder& self, tgfx::TextureHandle handle) -> uint32_t {
                // Returns 0 on non-GL backends or on unknown handle.
                return static_cast<uint32_t>(self.device->native_texture_handle(handle));
            },
            nb::arg("handle"))

        // Create an offscreen color attachment. Usage is
        // Sampled|ColorAttachment|CopyDst — safe for passes that read
        // the texture back and for blits (e.g. final composite onto
        // the default framebuffer via blit_to_external_fbo).
        .def("create_color_attachment",
            [](Tgfx2ContextHolder& self, uint32_t w, uint32_t h,
               tgfx::PixelFormat fmt) -> tgfx::TextureHandle {
                tgfx::TextureDesc desc;
                desc.width = w;
                desc.height = h;
                desc.format = fmt;
                desc.usage = tgfx::TextureUsage::Sampled |
                             tgfx::TextureUsage::ColorAttachment |
                             tgfx::TextureUsage::CopyDst;
                return self.device->create_texture(desc);
            },
            nb::arg("width"), nb::arg("height"),
            nb::arg("format") = tgfx::PixelFormat::RGBA8_UNorm)

        // Create an offscreen depth attachment. Usage is
        // DepthStencilAttachment|Sampled so passes can both write
        // depth and sample it (shadow maps, depth-based effects).
        .def("create_depth_attachment",
            [](Tgfx2ContextHolder& self, uint32_t w, uint32_t h,
               tgfx::PixelFormat fmt) -> tgfx::TextureHandle {
                tgfx::TextureDesc desc;
                desc.width = w;
                desc.height = h;
                desc.format = fmt;
                desc.usage = tgfx::TextureUsage::DepthStencilAttachment |
                             tgfx::TextureUsage::Sampled;
                return self.device->create_texture(desc);
            },
            nb::arg("width"), nb::arg("height"),
            nb::arg("format") = tgfx::PixelFormat::D24_UNorm);

    // Wrap an existing GL texture id as a non-owning tgfx2 handle.
    // Useful during the tgfx1→tgfx2 transition: a tgfx1 FontTextureAtlas
    // or similar can keep its GL texture and hand its id to a tgfx2
    // pass. The wrapper is non-owning; destroying it (or letting the
    // holder die) does NOT delete the GL texture.
    // Blit a tgfx2 color texture into an externally-owned GL FBO
    // (id=0 means the default window framebuffer). Thin wrapper
    // around OpenGLRenderDevice::blit_to_external_fbo for Python
    // callers that need to present a debug capture onto a host
    // window (Qt framegraph debugger, SDL debug window).
    m.def("ctx2_blit_to_external_fbo",
        [](Tgfx2ContextHolder& holder, uint32_t dst_fbo_id,
           tgfx::TextureHandle src,
           int src_x, int src_y, int src_w, int src_h,
           int dst_x, int dst_y, int dst_w, int dst_h) {
            holder.device->blit_to_external_target(
                static_cast<uintptr_t>(dst_fbo_id), src,
                src_x, src_y, src_w, src_h,
                dst_x, dst_y, dst_w, dst_h);
        },
        nb::arg("ctx"), nb::arg("dst_fbo_id"), nb::arg("src"),
        nb::arg("src_x"), nb::arg("src_y"),
        nb::arg("src_w"), nb::arg("src_h"),
        nb::arg("dst_x"), nb::arg("dst_y"),
        nb::arg("dst_w"), nb::arg("dst_h"));

    m.def("wrap_gl_texture_as_tgfx2",
        [](Tgfx2ContextHolder& holder, uint32_t gl_id,
           uint32_t w, uint32_t h, int format_int) -> tgfx::TextureHandle {
            tgfx::TextureDesc desc;
            desc.width = w;
            desc.height = h;
            desc.format = static_cast<tgfx::PixelFormat>(format_int);
            desc.usage = tgfx::TextureUsage::Sampled;
            // register_external_texture throws on backends without
            // external-handle support.
            return holder.device->register_external_texture(
                static_cast<uintptr_t>(gl_id), desc);
        },
        nb::arg("ctx"), nb::arg("gl_id"),
        nb::arg("width"), nb::arg("height"), nb::arg("format"));

    // --- Helpers: bridge legacy tgfx resources to tgfx2 handles ---
    // Compile a TcShader's GLSL sources into a tgfx2 VS/FS pair.
    // Uses the same tc_shader_ensure_tgfx2 bridge that C++ passes use,
    // so the result is cached on the tc_gpu_slot and shared across
    // Python/C++ callers. Returns (vs, fs) as a Tgfx2ShaderPair;
    // both handles are zero on compile failure.
    m.def("tc_shader_ensure_tgfx2",
          [](tgfx::RenderContext2& ctx, termin::TcShader shader) -> ShaderPair {
              ShaderPair out;
              tc_shader* raw = tc_shader_get(shader.handle);
              if (!raw) return out;
              termin::tc_shader_ensure_tgfx2(raw, &ctx.device(), &out.vs, &out.fs);
              return out;
          },
          nb::arg("ctx"), nb::arg("shader"));

    // CullMode values exposed as module ints.
    m.attr("CULL_NONE")  = static_cast<int>(tgfx::CullMode::None);
    m.attr("CULL_BACK")  = static_cast<int>(tgfx::CullMode::Back);
    m.attr("CULL_FRONT") = static_cast<int>(tgfx::CullMode::Front);

    m.attr("PIXEL_R8")      = static_cast<int>(tgfx::PixelFormat::R8_UNorm);
    m.attr("PIXEL_RGBA8")   = static_cast<int>(tgfx::PixelFormat::RGBA8_UNorm);
    m.attr("PIXEL_RGBA16F") = static_cast<int>(tgfx::PixelFormat::RGBA16F);
    m.attr("PIXEL_D32F")    = static_cast<int>(tgfx::PixelFormat::D32F);

    // --- Mesh draw helper ---
    //
    // draw_tc_mesh wraps a TcMesh's share-group VBO/EBO as tgfx2
    // external buffers, translates its tgfx_vertex_layout into a
    // tgfx::VertexBufferLayout, sets the pipeline's vertex layout +
    // topology + draws, and destroys the non-owning handles
    // afterwards. All in one call so Python callers don't need to
    // manage intermediate handles themselves.
    //
    // Used by migrated gizmo / immediate passes that want ctx2-native
    // draws without the legacy shader.use() + mesh_gpu.draw() path.
    m.def("draw_tc_mesh",
        [](tgfx::RenderContext2& ctx, termin::TcMesh& mesh_wrapper) {
            tc_mesh* mesh = tc_mesh_get(mesh_wrapper.handle);
            if (!mesh) return;

            auto* gl_dev = dynamic_cast<tgfx::OpenGLRenderDevice*>(&ctx.device());
            if (!gl_dev) return;

            // Materialize VBO/EBO via legacy upload path.
            if (tc_mesh_upload_gpu(mesh) == 0) return;

            tc_gpu_context* gctx = tc_gpu_get_context();
            if (!gctx || !gctx->share_group) return;
            tc_gpu_mesh_data_slot* slot = tc_gpu_share_group_mesh_data_slot(
                gctx->share_group, mesh->header.pool_index);
            if (!slot || slot->vbo == 0 || slot->ebo == 0) return;

            tgfx::BufferDesc vb_desc;
            vb_desc.size = static_cast<uint64_t>(mesh->vertex_count) *
                           static_cast<uint64_t>(mesh->layout.stride);
            vb_desc.usage = tgfx::BufferUsage::Vertex;
            tgfx::BufferHandle vbo = gl_dev->register_external_buffer(slot->vbo, vb_desc);

            tgfx::BufferDesc ib_desc;
            ib_desc.size = static_cast<uint64_t>(mesh->index_count) * sizeof(uint32_t);
            ib_desc.usage = tgfx::BufferUsage::Index;
            tgfx::BufferHandle ibo = gl_dev->register_external_buffer(slot->ebo, ib_desc);

            // Translate tgfx_vertex_layout → tgfx::VertexBufferLayout.
            tgfx::VertexBufferLayout layout;
            layout.stride = mesh->layout.stride;
            layout.attributes.reserve(mesh->layout.attrib_count);
            for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
                const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
                tgfx::VertexAttribute va;
                va.location = a.location;
                va.offset = a.offset;
                bool ok = true;
                switch (static_cast<tgfx_attrib_type>(a.type)) {
                    case TGFX_ATTRIB_FLOAT32:
                        switch (a.size) {
                            case 1: va.format = tgfx::VertexFormat::Float;  break;
                            case 2: va.format = tgfx::VertexFormat::Float2; break;
                            case 3: va.format = tgfx::VertexFormat::Float3; break;
                            case 4: va.format = tgfx::VertexFormat::Float4; break;
                            default: ok = false; break;
                        }
                        break;
                    case TGFX_ATTRIB_UINT8:
                        if (a.size == 4) va.format = tgfx::VertexFormat::UByte4;
                        else ok = false;
                        break;
                    case TGFX_ATTRIB_INT8:
                        if (a.size == 4) va.format = tgfx::VertexFormat::Byte4;
                        else ok = false;
                        break;
                    case TGFX_ATTRIB_UINT16:
                        switch (a.size) {
                            case 1: va.format = tgfx::VertexFormat::UShort;  break;
                            case 2: va.format = tgfx::VertexFormat::UShort2; break;
                            case 3: va.format = tgfx::VertexFormat::UShort3; break;
                            case 4: va.format = tgfx::VertexFormat::UShort4; break;
                            default: ok = false; break;
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }
                if (!ok) {
                    va.format = tgfx::VertexFormat::Float3;
                }
                layout.attributes.push_back(va);
            }

            tgfx::PrimitiveTopology topo = (mesh->draw_mode == TC_DRAW_LINES)
                ? tgfx::PrimitiveTopology::LineList
                : tgfx::PrimitiveTopology::TriangleList;

            ctx.set_vertex_layout(layout);
            ctx.set_topology(topo);
            ctx.draw(vbo, ibo, static_cast<uint32_t>(mesh->index_count),
                     tgfx::IndexType::Uint32);

            gl_dev->destroy(vbo);
            gl_dev->destroy(ibo);
        },
        nb::arg("ctx"), nb::arg("mesh"));

    // --- FontAtlas ---
    //
    // Public C++ FontAtlas (tgfx2/font_atlas.hpp) exposed under the
    // historical Python name "FontTextureAtlas". The class replaces
    // the hand-written tgfx/font.py atlas; Python callers that
    // previously held a FontTextureAtlas instance see the same
    // interface shape (ensure_glyphs / ensure_texture / measure_text)
    // — with RenderContext2 references in place of the prior
    // Tgfx2Context holder.
    nb::class_<tgfx::FontAtlas::Size2f>(m, "FontMeasure")
        .def_ro("width", &tgfx::FontAtlas::Size2f::width)
        .def_ro("height", &tgfx::FontAtlas::Size2f::height);

    nb::class_<tgfx::FontAtlas>(m, "FontTextureAtlas")
        .def(nb::init<const std::string&, int, int, int>(),
             nb::arg("path"),
             nb::arg("size") = 32,
             nb::arg("atlas_width") = 2048,
             nb::arg("atlas_height") = 2048)

        // Rasterise every glyph in `text` (UTF-8). If `ctx` is given
        // and any new glyph was added, triggers a GPU re-upload so
        // the next draw sees the fresh atlas.
        .def("ensure_glyphs",
             [](tgfx::FontAtlas& self,
                const std::string& text,
                tgfx::RenderContext2* ctx) {
                 self.ensure_glyphs(text, ctx);
             },
             nb::arg("text"),
             nb::arg("ctx").none() = nb::none())

        // Measure pixel (width, height) of `text` at display `size`.
        // Returns a tuple for Python ergonomics — callers typically
        // unpack as `w, h = font.measure_text(...)`.
        .def("measure_text",
             [](const tgfx::FontAtlas& self,
                const std::string& text,
                float size) {
                 auto m = self.measure_text(text, size);
                 return nb::make_tuple(m.width, m.height);
             },
             nb::arg("text"), nb::arg("size") = 14.0f)

        // Create or refresh the GPU atlas texture. Returns the cached
        // Tgfx2TextureHandle (same handle across calls with the same
        // ctx; dropped + recreated on ctx change).
        .def("ensure_texture",
             [](tgfx::FontAtlas& self, tgfx::RenderContext2* ctx)
             -> tgfx::TextureHandle {
                 return self.ensure_texture(ctx);
             },
             nb::arg("ctx"))

        // Look up one glyph's atlas entry. Returns a 6-tuple
        // (u0, v0, u1, v1, width_px, height_px) at the rasterise
        // size, or None if the glyph has not been rasterised.
        .def("get_glyph",
             [](const tgfx::FontAtlas& self, uint32_t codepoint) -> nb::object {
                 const auto* g = self.get_glyph(codepoint);
                 if (!g) return nb::none();
                 return nb::make_tuple(g->u0, g->v0, g->u1, g->v1,
                                       g->width_px, g->height_px);
             },
             nb::arg("codepoint"))

        // Drop the GPU texture. Safe when the underlying device has
        // already been torn down — no GL calls are issued.
        .def("release_gpu", &tgfx::FontAtlas::release_gpu)

        // --- Read-only metrics ---
        .def_prop_ro("size", &tgfx::FontAtlas::rasterize_size)
        .def_prop_ro("ascent", &tgfx::FontAtlas::ascent_px)
        .def_prop_ro("descent", &tgfx::FontAtlas::descent_px)
        .def_prop_ro("line_height", &tgfx::FontAtlas::line_height)
        .def_prop_ro("atlas_width", &tgfx::FontAtlas::atlas_width)
        .def_prop_ro("atlas_height", &tgfx::FontAtlas::atlas_height);

    // --- Text2DRenderer / Text3DRenderer ---
    //
    // Pixel-space and billboard text renderers backed by the C++
    // FontAtlas. API shape mirrors the prior Python Text2D / Text3D
    // classes so existing callers (UIRenderer, Text3DRenderer-using
    // tcplot code) need only replace `from tgfx.text2d import …` with
    // a re-export — the method signatures are intentionally identical
    // modulo `color` becoming a 4-tuple on the Python side.

    nb::enum_<tgfx::Text2DRenderer::Anchor>(m, "Text2DAnchor")
        .value("Left",   tgfx::Text2DRenderer::Anchor::Left)
        .value("Center", tgfx::Text2DRenderer::Anchor::Center)
        .value("Right",  tgfx::Text2DRenderer::Anchor::Right)
        .export_values();
    nb::enum_<tgfx::Text3DRenderer::Anchor>(m, "Text3DAnchor")
        .value("Left",   tgfx::Text3DRenderer::Anchor::Left)
        .value("Center", tgfx::Text3DRenderer::Anchor::Center)
        .value("Right",  tgfx::Text3DRenderer::Anchor::Right)
        .export_values();

    // Anchor resolution: accept both the enum values and the legacy
    // lower-case string form ("left"/"center"/"right") used by the
    // prior Python implementation — existing callers pass strings.
    auto resolve_text2d_anchor = [](nb::object obj) -> tgfx::Text2DRenderer::Anchor {
        if (nb::isinstance<nb::str>(obj)) {
            std::string s = nb::cast<std::string>(obj);
            if (s == "center") return tgfx::Text2DRenderer::Anchor::Center;
            if (s == "right")  return tgfx::Text2DRenderer::Anchor::Right;
            return tgfx::Text2DRenderer::Anchor::Left;
        }
        return nb::cast<tgfx::Text2DRenderer::Anchor>(obj);
    };
    auto resolve_text3d_anchor = [](nb::object obj) -> tgfx::Text3DRenderer::Anchor {
        if (nb::isinstance<nb::str>(obj)) {
            std::string s = nb::cast<std::string>(obj);
            if (s == "left")  return tgfx::Text3DRenderer::Anchor::Left;
            if (s == "right") return tgfx::Text3DRenderer::Anchor::Right;
            return tgfx::Text3DRenderer::Anchor::Center;
        }
        return nb::cast<tgfx::Text3DRenderer::Anchor>(obj);
    };

    nb::class_<tgfx::Text2DRenderer>(m, "Text2DRenderer")
        .def(nb::init<tgfx::FontAtlas*>(),
             nb::arg("font") = nullptr,
             nb::keep_alive<1, 2>())  // keep font alive while renderer lives

        // begin: (ctx, viewport_w, viewport_h, font=None).
        .def("begin",
             [](tgfx::Text2DRenderer& self,
                tgfx::RenderContext2* ctx,
                int viewport_w, int viewport_h,
                tgfx::FontAtlas* font) {
                 self.begin(ctx, viewport_w, viewport_h, font);
             },
             nb::arg("ctx"),
             nb::arg("viewport_w"),
             nb::arg("viewport_h"),
             nb::arg("font").none() = nb::none(),
             nb::keep_alive<1, 5>())

        .def("draw",
             [resolve_text2d_anchor](tgfx::Text2DRenderer& self,
                const std::string& text,
                float x, float y,
                std::tuple<float, float, float, float> color,
                float size,
                nb::object anchor) {
                 auto [r, g, b, a] = color;
                 self.draw(text, x, y, r, g, b, a, size,
                           resolve_text2d_anchor(anchor));
             },
             nb::arg("text"),
             nb::arg("x"), nb::arg("y"),
             nb::arg("color") = std::make_tuple(1.0f, 1.0f, 1.0f, 1.0f),
             nb::arg("size") = 14.0f,
             nb::arg("anchor") = "left")

        .def("measure",
             [](tgfx::Text2DRenderer& self, const std::string& text, float size) {
                 if (!self.font()) return std::make_tuple(0.0f, 0.0f);
                 auto m = self.font()->measure_text(text, size);
                 return std::make_tuple(m.width, m.height);
             },
             nb::arg("text"), nb::arg("size") = 14.0f)

        .def("end", &tgfx::Text2DRenderer::end)
        .def("release_gpu", &tgfx::Text2DRenderer::release_gpu);

    nb::class_<tgfx::Text3DRenderer>(m, "Text3DRenderer")
        .def(nb::init<tgfx::FontAtlas*>(),
             nb::arg("font") = nullptr,
             nb::keep_alive<1, 2>())

        // begin takes flat mvp[16] + cam_right[3] + cam_up[3] as
        // numpy arrays. Callers that have a Python-side camera object
        // should extract these themselves — the renderer is now
        // decoupled from any specific camera interface.
        .def("begin",
             [](tgfx::Text3DRenderer& self,
                tgfx::RenderContext2* ctx,
                nb::ndarray<float, nb::c_contig, nb::device::cpu> mvp,
                nb::ndarray<float, nb::c_contig, nb::device::cpu> cam_right,
                nb::ndarray<float, nb::c_contig, nb::device::cpu> cam_up,
                tgfx::FontAtlas* font) {
                 if (mvp.size() < 16 || cam_right.size() < 3 || cam_up.size() < 3) {
                     throw std::invalid_argument(
                         "Text3DRenderer.begin: mvp needs 16 floats, "
                         "cam_right/cam_up need 3 each");
                 }
                 self.begin(ctx, mvp.data(), cam_right.data(), cam_up.data(), font);
             },
             nb::arg("ctx"),
             nb::arg("mvp"),
             nb::arg("cam_right"),
             nb::arg("cam_up"),
             nb::arg("font").none() = nb::none(),
             nb::keep_alive<1, 6>())

        .def("draw",
             [resolve_text3d_anchor](tgfx::Text3DRenderer& self,
                const std::string& text,
                std::tuple<float, float, float> position,
                std::tuple<float, float, float, float> color,
                float size,
                nb::object anchor) {
                 auto [px, py, pz] = position;
                 float pos[3] = {px, py, pz};
                 auto [r, g, b, a] = color;
                 self.draw(text, pos, r, g, b, a, size,
                           resolve_text3d_anchor(anchor));
             },
             nb::arg("text"),
             nb::arg("position"),
             nb::arg("color") = std::make_tuple(1.0f, 1.0f, 1.0f, 1.0f),
             nb::arg("size") = 0.05f,
             nb::arg("anchor") = "center")

        .def("end", &tgfx::Text3DRenderer::end)
        .def("release_gpu", &tgfx::Text3DRenderer::release_gpu);
}

} // namespace tgfx_bindings
