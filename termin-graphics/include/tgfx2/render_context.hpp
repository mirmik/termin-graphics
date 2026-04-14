// render_context.hpp - Mid-level rendering abstraction over tgfx2.
// Bridges the state-machine pattern (set_depth_test, bind_texture, draw)
// to the pipeline + command buffer model.
//
// Usage:
//   RenderContext2 ctx(device, pipeline_cache);
//   ctx.begin_frame();
//   ctx.begin_pass(color_tex, depth_tex);
//   ctx.set_depth_test(false);
//   ctx.set_blend(false);
//   ctx.bind_shader(vs, fs);
//   ctx.bind_texture(0, tex);
//   ctx.set_viewport(0, 0, w, h);
//   ctx.draw_fullscreen_quad();
//   ctx.end_pass();
//   ctx.end_frame();
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/render_state.hpp"
#include "tgfx2/vertex_layout.hpp"
#include "tgfx2/descriptors.hpp"

namespace tgfx2 {

class IRenderDevice;
class ICommandList;
class PipelineCache;

class TGFX2_API RenderContext2 {
public:
    RenderContext2(IRenderDevice& device, PipelineCache& cache);
    ~RenderContext2();

    // Non-copyable
    RenderContext2(const RenderContext2&) = delete;
    RenderContext2& operator=(const RenderContext2&) = delete;

    // --- Frame lifecycle ---
    void begin_frame();
    void end_frame();   // submits command list

    // --- Render pass ---
    // Begin render pass with color attachment (and optional depth).
    // clear_color = nullptr means LoadOp::Load for the color attachment.
    // Pass `color = {}` (id=0) for a depth-only pass (e.g. shadow maps);
    // the backend will set glDrawBuffer/ReadBuffer = NONE.
    // `clear_depth_enabled` controls depth LoadOp independently of color.
    void begin_pass(TextureHandle color, TextureHandle depth = {},
                    const float* clear_color = nullptr,
                    float clear_depth = 1.0f,
                    bool clear_depth_enabled = true);
    void end_pass();

    // --- Mutable render state (applied at draw time) ---
    void set_depth_test(bool enabled);
    void set_depth_write(bool enabled);
    void set_depth_func(CompareOp op);
    void set_blend(bool enabled);
    void set_blend_func(BlendFactor src, BlendFactor dst);
    void set_cull(CullMode mode);
    void set_polygon_mode(PolygonMode mode);
    void set_color_mask(bool r, bool g, bool b, bool a);

    // --- Shader binding ---
    void bind_shader(ShaderHandle vs, ShaderHandle fs,
                     ShaderHandle gs = {});

    // --- Vertex layout ---
    void set_vertex_layout(const VertexBufferLayout& layout);
    void set_topology(PrimitiveTopology topo);

    // --- Resource bindings (UBOs, textures, samplers) ---
    // Register a uniform buffer at the given binding slot. The buffer is
    // resolved into a ResourceSet lazily at draw time; call-sites do not
    // manage ResourceSetHandle lifecycles.
    // Passing range=0 means "bind whole buffer" (backend uses glBindBufferBase).
    void bind_uniform_buffer(uint32_t binding, BufferHandle buffer,
                             uint64_t offset = 0, uint64_t range = 0);

    // Set per-draw push constants. Payload becomes visible to the next
    // draw call at binding slot TGFX2_PUSH_CONSTANTS_BINDING (GL) or via
    // `layout(push_constant)` on Vulkan. Max payload is
    // TGFX2_PUSH_CONSTANTS_MAX_BYTES (128 bytes, Vulkan-compat). Typical
    // use: model matrix and other per-object data that changes every
    // draw — avoids the churn of uploading/rebinding a full UBO.
    void set_push_constants(const void* data, uint32_t size);

    // Queue a handle for destruction at the end of the current frame.
    // Used by pass code that wraps legacy GL resources as non-owning
    // tgfx2 handles (register_external_texture / register_external_buffer)
    // and needs them alive only for the draws in this frame. The
    // underlying GL object is preserved; only the tgfx2 HandlePool entry
    // is removed.
    void defer_destroy(TextureHandle handle);
    void defer_destroy(BufferHandle handle);

    // --- Transitional legacy-uniform setters ---
    //
    // These exist to bridge pre-existing .shader files that still declare
    // plain `uniform mat4 u_view` / `uniform sampler2D u_shadow_map_0`
    // etc. onto the tgfx2 draw path. They flush the pending pipeline
    // (so the shader program is actually bound in GL) and then route
    // through glUniform*/glUniformMatrix*/glUniform1i on the currently-
    // bound program.
    //
    // Vulkan has no equivalent — Vulkan shaders must use UBOs or push
    // constants. On a Vulkan backend these methods would be no-ops or
    // assertions. Treat them as *temporary*: every caller is a candidate
    // for migration to bind_uniform_buffer / set_push_constants.
    void set_uniform_int(const char* name, int value);
    void set_uniform_float(const char* name, float value);
    void set_uniform_mat4(const char* name, const float* data,
                          bool transpose = false);

    // Link a `layout(std140) uniform NAME { ... }` block declared in
    // the currently bound program to a binding slot. On Vulkan the
    // binding is compiled into the shader via SPIR-V; this call is
    // GL-only transitional, wrapping glUniformBlockBinding on the
    // current program.
    void set_block_binding(const char* block_name, uint32_t binding_slot);

    // Register a sampled texture at the given binding slot. The sampler is
    // optional; if omitted, the backend uses the texture's default sampling
    // parameters (useful for GL 3.3 style shaders without separate samplers).
    void bind_sampled_texture(uint32_t binding, TextureHandle tex,
                              SamplerHandle sampler = {});

    // Drop all pending resource bindings — next draw starts from an empty
    // resource set.
    void clear_resource_bindings();

    // --- Target format info (for pipeline cache key) ---
    void set_color_format(PixelFormat fmt);
    void set_depth_format(PixelFormat fmt);
    void set_sample_count(uint32_t samples);

    // --- Viewport / Scissor ---
    void set_viewport(int x, int y, int w, int h);
    void set_scissor(int x, int y, int w, int h);
    void clear_scissor();

    // --- Drawing ---
    // Fullscreen quad (built-in, for post-processing passes).
    void draw_fullscreen_quad();

    // Draw with bound vertex/index buffers.
    void draw(BufferHandle vbo, BufferHandle ibo,
              uint32_t index_count, IndexType idx_type = IndexType::Uint32);

    // Draw non-indexed.
    void draw_arrays(BufferHandle vbo, uint32_t vertex_count);

    // --- Immediate drawing (creates temp buffers) ---
    // Lines: vertex data is [x,y,z, r,g,b,a] per vertex.
    void draw_immediate_lines(const float* data, uint32_t vertex_count);

    // Triangles: same vertex format.
    void draw_immediate_triangles(const float* data, uint32_t vertex_count);

    // --- Blit ---
    void blit(TextureHandle src, TextureHandle dst);

    // --- Access ---
    IRenderDevice& device() { return device_; }
    ICommandList* cmd() { return cmd_.get(); }

    // Force pending render state to be resolved into an active pipeline.
    // Normally called internally from draw*() methods; exposed publicly as
    // an escape hatch for Phase 2 pass migration where a pass needs to set
    // uniforms or bind textures on the underlying GL program BEFORE issuing
    // a draw call — via glGetIntegerv(GL_CURRENT_PROGRAM) + glUniform*.
    // After this method returns, the backend-specific pipeline (e.g. GL
    // program) is bound and ready for state setting.
    void flush_pipeline();

    // Return the built-in fullscreen-quad vertex shader, lazily creating it
    // and the FSQ VBO/IBO on first access. Exposed so Phase 2 passes can
    // bind_shader(fsq_vertex_shader(), their_fs) explicitly and then
    // flush_pipeline() before setting uniforms via raw GL, avoiding the
    // "Pipeline requires valid vertex and fragment shaders" error that
    // would otherwise fire when flush_pipeline() runs while bound_vs_ is
    // still empty (the VS substitution inside draw_fullscreen_quad() only
    // happens at the start of that method, too late for a pre-draw uniform
    // set).
    ShaderHandle fsq_vertex_shader();

private:

    IRenderDevice& device_;
    PipelineCache& cache_;
    std::unique_ptr<ICommandList> cmd_;

    // --- Pending state ---
    RasterState raster_;
    DepthStencilState depth_stencil_;
    BlendState blend_;
    ColorMask color_mask_;

    ShaderHandle bound_vs_, bound_fs_, bound_gs_;
    VertexBufferLayout vertex_layout_;
    PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;

    PixelFormat color_format_ = PixelFormat::RGBA8_UNorm;
    PixelFormat depth_format_ = PixelFormat::D32F;
    uint32_t sample_count_ = 1;

    bool in_pass_ = false;
    bool pipeline_dirty_ = true;

    // Pending resource bindings, rebuilt into a ResourceSet on dirty.
    std::vector<ResourceBinding> pending_bindings_;
    bool bindings_dirty_ = true;
    ResourceSetHandle current_resource_set_;

    // Per-frame deferred-destruction list for non-owning external
    // wrappers (register_external_texture / register_external_buffer)
    // created and used inside a single frame. Drained in end_frame().
    std::vector<TextureHandle> deferred_destroy_textures_;
    std::vector<BufferHandle>  deferred_destroy_buffers_;

    // Fullscreen quad resources (created on first use)
    BufferHandle fsq_vbo_;
    BufferHandle fsq_ibo_;
    ShaderHandle fsq_vs_;

    void ensure_fsq_resources();
    void flush_resource_set();
};

} // namespace tgfx2
