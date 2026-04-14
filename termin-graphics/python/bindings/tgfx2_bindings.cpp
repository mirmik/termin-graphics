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

#include <tgfx2/render_context.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/vertex_layout.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

#include <tgfx/opengl/opengl_framebuffer.hpp>
#include <tgfx/tgfx_shader_handle.hpp>
#include <tgfx/tgfx_mesh_handle.hpp>
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

    nb::class_<tgfx2::TextureHandle>(m, "Tgfx2TextureHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx2::TextureHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx2::TextureHandle& h) { return static_cast<bool>(h); });

    nb::class_<tgfx2::BufferHandle>(m, "Tgfx2BufferHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx2::BufferHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx2::BufferHandle& h) { return static_cast<bool>(h); });

    nb::class_<tgfx2::ShaderHandle>(m, "Tgfx2ShaderHandle")
        .def(nb::init<>())
        .def_prop_ro("id", [](const tgfx2::ShaderHandle& h) { return h.id; })
        .def("__bool__", [](const tgfx2::ShaderHandle& h) { return static_cast<bool>(h); });

    // Paired (vs, fs) return type for tc_shader_ensure_tgfx2 — easier
    // to return one object than two out parameters.
    struct ShaderPair {
        tgfx2::ShaderHandle vs;
        tgfx2::ShaderHandle fs;
    };
    nb::class_<ShaderPair>(m, "Tgfx2ShaderPair")
        .def_ro("vs", &ShaderPair::vs)
        .def_ro("fs", &ShaderPair::fs);

    // --- RenderContext2 ---
    //
    // Only the methods Python passes actually need are exposed. The
    // rest (bind_uniform_buffer, set_vertex_layout, push constants, ...)
    // stay C++-only because their Python form would require binding
    // descriptor structs that Python code doesn't need.
    nb::class_<tgfx2::RenderContext2>(m, "Tgfx2RenderContext")
        .def("begin_pass",
             [](tgfx2::RenderContext2& self,
                tgfx2::TextureHandle color,
                tgfx2::TextureHandle depth,
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
             nb::arg("depth") = tgfx2::TextureHandle{},
             nb::arg("clear_color_enabled") = false,
             nb::arg("r") = 0.0f, nb::arg("g") = 0.0f,
             nb::arg("b") = 0.0f, nb::arg("a") = 1.0f,
             nb::arg("clear_depth") = 1.0f,
             nb::arg("clear_depth_enabled") = false)
        .def("end_pass", &tgfx2::RenderContext2::end_pass)

        // State
        .def("set_depth_test", &tgfx2::RenderContext2::set_depth_test)
        .def("set_depth_write", &tgfx2::RenderContext2::set_depth_write)
        .def("set_blend", &tgfx2::RenderContext2::set_blend)
        .def("set_cull",
             [](tgfx2::RenderContext2& self, int mode) {
                 self.set_cull(static_cast<tgfx2::CullMode>(mode));
             })
        .def("set_color_format",
             [](tgfx2::RenderContext2& self, int fmt) {
                 self.set_color_format(static_cast<tgfx2::PixelFormat>(fmt));
             })
        .def("set_color_mask", &tgfx2::RenderContext2::set_color_mask)
        .def("set_viewport", &tgfx2::RenderContext2::set_viewport)
        .def("clear_scissor", &tgfx2::RenderContext2::clear_scissor)

        // Shader
        .def("bind_shader",
             [](tgfx2::RenderContext2& self,
                tgfx2::ShaderHandle vs, tgfx2::ShaderHandle fs) {
                 self.bind_shader(vs, fs, {});
             })

        // Resource bindings
        .def("bind_sampled_texture",
             [](tgfx2::RenderContext2& self, uint32_t slot, tgfx2::TextureHandle tex) {
                 self.bind_sampled_texture(slot, tex, {});
             })

        // Transitional plain-uniform setters
        .def("set_uniform_int",
             [](tgfx2::RenderContext2& self, const std::string& name, int value) {
                 self.set_uniform_int(name.c_str(), value);
             })
        .def("set_uniform_float",
             [](tgfx2::RenderContext2& self, const std::string& name, float value) {
                 self.set_uniform_float(name.c_str(), value);
             })
        .def("set_uniform_vec2",
             [](tgfx2::RenderContext2& self, const std::string& name,
                float x, float y) {
                 self.set_uniform_vec2(name.c_str(), x, y);
             })
        .def("set_uniform_vec3",
             [](tgfx2::RenderContext2& self, const std::string& name,
                float x, float y, float z) {
                 self.set_uniform_vec3(name.c_str(), x, y, z);
             })
        .def("set_uniform_vec4",
             [](tgfx2::RenderContext2& self, const std::string& name,
                float x, float y, float z, float w) {
                 self.set_uniform_vec4(name.c_str(), x, y, z, w);
             })
        .def("set_uniform_mat4",
             [](tgfx2::RenderContext2& self, const std::string& name,
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
             [](tgfx2::RenderContext2& self, const std::string& name, uint32_t slot) {
                 self.set_block_binding(name.c_str(), slot);
             })

        // Draw
        .def("draw_fullscreen_quad", &tgfx2::RenderContext2::draw_fullscreen_quad);

    // --- Helpers: bridge legacy tgfx resources to tgfx2 handles ---
    //
    // Wrap a legacy FramebufferHandle's color attachment as a tgfx2
    // sampled texture. The wrapper is non-owning — the FBO keeps owning
    // the underlying GL texture. Caller uses the handle for a single
    // draw then drops it (or hands it to RenderContext2::defer_destroy
    // to release at end_frame).
    m.def("wrap_fbo_color_as_tgfx2",
          [](tgfx2::RenderContext2& ctx, termin::FramebufferHandle* fbo)
              -> tgfx2::TextureHandle {
              if (!fbo) return {};
              auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(&ctx.device());
              if (!gl_dev) return {};
              auto* color = fbo->color_texture();
              if (!color) return {};

              tgfx2::TextureDesc desc;
              desc.width = static_cast<uint32_t>(fbo->get_width());
              desc.height = static_cast<uint32_t>(fbo->get_height());
              desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
              desc.usage = tgfx2::TextureUsage::Sampled;
              desc.sample_count = static_cast<uint32_t>(fbo->get_samples());
              return gl_dev->register_external_texture(
                  static_cast<GLuint>(color->get_id()), desc);
          },
          nb::arg("ctx"), nb::arg("fbo"));

    // Compile a TcShader's GLSL sources into a tgfx2 VS/FS pair.
    // Uses the same tc_shader_ensure_tgfx2 bridge that C++ passes use,
    // so the result is cached on the tc_gpu_slot and shared across
    // Python/C++ callers. Returns (vs, fs) as a Tgfx2ShaderPair;
    // both handles are zero on compile failure.
    m.def("tc_shader_ensure_tgfx2",
          [](tgfx2::RenderContext2& ctx, termin::TcShader shader) -> ShaderPair {
              ShaderPair out;
              tc_shader* raw = tc_shader_get(shader.handle);
              if (!raw) return out;
              termin::tc_shader_ensure_tgfx2(raw, &ctx.device(), &out.vs, &out.fs);
              return out;
          },
          nb::arg("ctx"), nb::arg("shader"));

    // CullMode values exposed as module ints.
    m.attr("CULL_NONE")  = static_cast<int>(tgfx2::CullMode::None);
    m.attr("CULL_BACK")  = static_cast<int>(tgfx2::CullMode::Back);
    m.attr("CULL_FRONT") = static_cast<int>(tgfx2::CullMode::Front);

    m.attr("PIXEL_RGBA8")   = static_cast<int>(tgfx2::PixelFormat::RGBA8_UNorm);
    m.attr("PIXEL_RGBA16F") = static_cast<int>(tgfx2::PixelFormat::RGBA16F);
    m.attr("PIXEL_D32F")    = static_cast<int>(tgfx2::PixelFormat::D32F);

    // --- Mesh draw helper ---
    //
    // draw_tc_mesh wraps a TcMesh's share-group VBO/EBO as tgfx2
    // external buffers, translates its tgfx_vertex_layout into a
    // tgfx2::VertexBufferLayout, sets the pipeline's vertex layout +
    // topology + draws, and destroys the non-owning handles
    // afterwards. All in one call so Python callers don't need to
    // manage intermediate handles themselves.
    //
    // Used by migrated gizmo / immediate passes that want ctx2-native
    // draws without the legacy shader.use() + mesh_gpu.draw() path.
    m.def("draw_tc_mesh",
        [](tgfx2::RenderContext2& ctx, termin::TcMesh& mesh_wrapper) {
            tc_mesh* mesh = tc_mesh_get(mesh_wrapper.handle);
            if (!mesh) return;

            auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(&ctx.device());
            if (!gl_dev) return;

            // Materialize VBO/EBO via legacy upload path.
            if (tc_mesh_upload_gpu(mesh) == 0) return;

            tc_gpu_context* gctx = tc_gpu_get_context();
            if (!gctx || !gctx->share_group) return;
            tc_gpu_mesh_data_slot* slot = tc_gpu_share_group_mesh_data_slot(
                gctx->share_group, mesh->header.pool_index);
            if (!slot || slot->vbo == 0 || slot->ebo == 0) return;

            tgfx2::BufferDesc vb_desc;
            vb_desc.size = static_cast<uint64_t>(mesh->vertex_count) *
                           static_cast<uint64_t>(mesh->layout.stride);
            vb_desc.usage = tgfx2::BufferUsage::Vertex;
            tgfx2::BufferHandle vbo = gl_dev->register_external_buffer(slot->vbo, vb_desc);

            tgfx2::BufferDesc ib_desc;
            ib_desc.size = static_cast<uint64_t>(mesh->index_count) * sizeof(uint32_t);
            ib_desc.usage = tgfx2::BufferUsage::Index;
            tgfx2::BufferHandle ibo = gl_dev->register_external_buffer(slot->ebo, ib_desc);

            // Translate tgfx_vertex_layout → tgfx2::VertexBufferLayout.
            tgfx2::VertexBufferLayout layout;
            layout.stride = mesh->layout.stride;
            layout.attributes.reserve(mesh->layout.attrib_count);
            for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
                const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
                tgfx2::VertexAttribute va;
                va.location = a.location;
                va.offset = a.offset;
                bool ok = true;
                switch (static_cast<tgfx_attrib_type>(a.type)) {
                    case TGFX_ATTRIB_FLOAT32:
                        switch (a.size) {
                            case 1: va.format = tgfx2::VertexFormat::Float;  break;
                            case 2: va.format = tgfx2::VertexFormat::Float2; break;
                            case 3: va.format = tgfx2::VertexFormat::Float3; break;
                            case 4: va.format = tgfx2::VertexFormat::Float4; break;
                            default: ok = false; break;
                        }
                        break;
                    case TGFX_ATTRIB_UINT8:
                        if (a.size == 4) va.format = tgfx2::VertexFormat::UByte4;
                        else ok = false;
                        break;
                    case TGFX_ATTRIB_INT8:
                        if (a.size == 4) va.format = tgfx2::VertexFormat::Byte4;
                        else ok = false;
                        break;
                    case TGFX_ATTRIB_UINT16:
                        switch (a.size) {
                            case 1: va.format = tgfx2::VertexFormat::UShort;  break;
                            case 2: va.format = tgfx2::VertexFormat::UShort2; break;
                            case 3: va.format = tgfx2::VertexFormat::UShort3; break;
                            case 4: va.format = tgfx2::VertexFormat::UShort4; break;
                            default: ok = false; break;
                        }
                        break;
                    default:
                        ok = false;
                        break;
                }
                if (!ok) {
                    va.format = tgfx2::VertexFormat::Float3;
                }
                layout.attributes.push_back(va);
            }

            tgfx2::PrimitiveTopology topo = (mesh->draw_mode == TC_DRAW_LINES)
                ? tgfx2::PrimitiveTopology::LineList
                : tgfx2::PrimitiveTopology::TriangleList;

            ctx.set_vertex_layout(layout);
            ctx.set_topology(topo);
            ctx.draw(vbo, ibo, static_cast<uint32_t>(mesh->index_count),
                     tgfx2::IndexType::Uint32);

            gl_dev->destroy(vbo);
            gl_dev->destroy(ibo);
        },
        nb::arg("ctx"), nb::arg("mesh"));
}

} // namespace tgfx_bindings
