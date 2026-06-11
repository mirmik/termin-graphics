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

// Forward declaration for C-level shader type (global namespace, extern "C").
struct tc_shader;

namespace tgfx {

class IRenderDevice;
class ICommandList;
class PipelineCache;

class TGFX2_TYPE_API RenderContext2 {
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
    std::vector<VertexBufferLayout> vertex_layouts_;
    // Hash of `vertex_layouts_`, recomputed only when set_vertex_layout* is
    // called. Fed into PipelineCacheKey so flush_pipeline's lookup skips
    // re-hashing the attributes vector on every draw.
    size_t vertex_layouts_hash_ = 0;
    PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;

    // Synced from begin_pass() with the actual attachment formats.
    // `Undefined` means "no attachment of this kind in the current
    // pass" — the pipeline cache builds a VkRenderPass with matching
    // attachment count, so vkCmdDraw's compatibility check passes.
    PixelFormat color_format_ = PixelFormat::Undefined;
    PixelFormat depth_format_ = PixelFormat::Undefined;
    uint32_t sample_count_ = 1;

    bool in_pass_ = false;
    bool pipeline_dirty_ = true;

    // Pending resource bindings, rebuilt into a ResourceSet on dirty.
    std::vector<ResourceBinding> pending_bindings_;
    bool bindings_dirty_ = true;
    ResourceSetHandle current_resource_set_;

    // Symbolic binding support — resolved in flush_resource_set().
    struct SymbolicBinding {
        std::string name;
        enum class Kind { Uniform, Texture } kind = Kind::Uniform;
        BufferHandle buffer;
        TextureHandle texture;
        SamplerHandle sampler;
        uint64_t offset = 0;
        uint64_t range = 0;
    };
    std::vector<SymbolicBinding> pending_symbolic_bindings_;
    // Shader resource layout for symbolic resolution. Set by
    // use_shader_resource_layout(), cleared when shader changes.
    const struct ::tc_shader* active_shader_layout_ = nullptr;

    // Queued push-constant bytes. Re-emitted after every flush_pipeline
    // so the data lands on the freshly-bound VkPipelineLayout (Vulkan)
    // or the current ring UBO offset (OpenGL). Cleared when the caller
    // passes an empty payload.
    std::vector<uint8_t> pending_push_constants_;
    // True when pending_push_constants_ hasn't been emitted into the cmd
    // buffer yet since the last set_push_constants() call. Cleared after
    // flush_pipeline() pushes them. Kills the double-emit that happened
    // when set_push_constants() pushed immediately *and* flush_pipeline
    // re-pushed after a state change — visible as pushC ~1.4 per draw.
    bool push_constants_dirty_ = false;

    // Per-frame deferred-destruction list for non-owning external
    // wrappers (register_external_texture / register_external_buffer)
    // created and used inside a single frame. Drained in end_frame().
    std::vector<TextureHandle> deferred_destroy_textures_;
    std::vector<BufferHandle>  deferred_destroy_buffers_;
    std::vector<ResourceSetHandle> deferred_destroy_resource_sets_;

    // Fullscreen quad resources (created on first use)
    BufferHandle fsq_vbo_;
    BufferHandle fsq_ibo_;
    ShaderHandle fsq_vs_;

    // Last vbo/ibo bound on the command list — lets draw() skip the
    // redundant vkCmdBindVertexBuffers / vkCmdBindIndexBuffer when the
    // next draw reuses the same mesh (chronosquad scenes hit the same
    // VBO for hundreds of instanced draws — thousand-scale reduction in
    // bind cmd recording). Reset to {} at begin_pass so a new pass
    // always re-binds.
    std::vector<BufferHandle> last_bound_vbos_;
    std::vector<uint64_t> last_bound_vbo_offsets_;
    BufferHandle last_bound_ibo_ = {};
    uint64_t last_bound_ibo_offset_ = 0;
    IndexType last_bound_index_type_ = IndexType::Uint32;
    // Last pipeline handle passed to cmd_->bind_pipeline(). Used by
    // flush_pipeline() to skip a redundant vkCmdBindPipeline when the
    // pipeline cache returned the same handle again (same state combo).
    PipelineHandle last_bound_pipeline_ = {};

public:
    RenderContext2(IRenderDevice& device, PipelineCache& cache);
    // Virtual so the compiler emits a vtable + typeinfo in a single
    // translation unit (render_context.cpp) and exports them from
    // libtermin_graphics2.so. Without this, each nanobind extension
    // module that includes render_context.hpp generates its own
    // hidden-visibility typeinfo, and cross-module
    // nb::class_<RenderContext2> lookups fail with
    // "Unable to convert function return value to a Python type".
    virtual ~RenderContext2();

    // Non-copyable
    RenderContext2(const RenderContext2&) = delete;
    RenderContext2& operator=(const RenderContext2&) = delete;

    // --- Frame lifecycle ---
    void begin_frame();
    void end_frame();   // submits command list
    // True between begin_frame() and end_frame() — i.e. command list
    // is live and draws/passes can be recorded. Used by guest renderers
    // (UIRenderer, Python frame passes) to decide whether they need to open a
    // frame themselves or the host already has one running.
    bool in_frame() const { return cmd_ != nullptr; }

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
    void set_depth_bias(bool enabled, float constant = 0.0f, float slope = 0.0f, float clamp = 0.0f);
    void set_color_mask(bool r, bool g, bool b, bool a);

    // --- Shader binding ---
    void bind_shader(ShaderHandle vs, ShaderHandle fs,
                     ShaderHandle gs = {});

    // --- Vertex layout ---
    void set_vertex_layout(const VertexBufferLayout& layout);
    void set_vertex_layouts(const std::vector<VertexBufferLayout>& layouts);
    void set_topology(PrimitiveTopology topo);

    // --- Resource bindings (UBOs, textures, samplers) ---
    // Legacy numeric uniform binding. The buffer is resolved into a
    // ResourceSet lazily at draw time; call-sites do not manage
    // ResourceSetHandle lifecycles. Migrated Slang paths should prefer
    // bind_uniform(name). The `set` parameter is currently flattened by the
    // Vulkan backend and kept for compatibility with future scoped layouts.
    // Passing range=0 means \"bind whole buffer\" (backend uses glBindBufferBase).
    void bind_uniform_buffer(uint32_t binding, BufferHandle buffer,
                             uint64_t offset = 0, uint64_t range = 0,
                             uint32_t set = 0);

    // Symbolic resource binding — resolved to backend placement from the
    // shader layout set via use_shader_resource_layout().
    // Falls back to a logged warning + no-op when the name is not
    // found in the active layout.
    void bind_uniform(std::string_view name, BufferHandle buffer,
                      uint64_t offset = 0, uint64_t range = 0);
    void bind_texture(std::string_view name, TextureHandle texture,
                      SamplerHandle sampler = {});
    // Symbolic uniform with inline data — resolves the name and writes
    // to the ring UBO at the resolved binding. Convenience for Python
    // passes that only need small per-draw uniform data without managing
    // a BufferHandle.
    void bind_uniform_data(std::string_view name, const void* data, uint32_t size);

    // Set the shader resource layout used for symbolic binding
    // resolution. Call once after bind_shader() when symbolic
    // bind_uniform / bind_texture will be used later in the pass.
    // Passing nullptr clears the layout (back to numeric-only mode).
    void use_shader_resource_layout(const struct ::tc_shader* shader);

    // Write `size` bytes of `data` into the backend's shared ring UBO
    // and bind it at `binding` in the given descriptor set. No
    // caller-managed BufferHandle, no per-draw upload_buffer, no
    // descriptor-set allocation for UBO-only differences between draws.
    // On Vulkan this becomes a dynamic descriptor offset; on OpenGL as a
    // `glBindBufferRange` into the ring buffer. `size` must be ≤ the
    // UBO block size declared by the shader.
    //
    // `binding` must be one of the layout's UNIFORM_BUFFER_DYNAMIC slots
    // (0..3, 16, 24). Other slots fall back to the classic bind_uniform_buffer
    // path and pay the old per-draw churn.
    // `set` is retained for compatibility with future scoped layouts; the
    // current Vulkan backend flattens migrated resources into set 0.
    void bind_uniform_buffer_ring(uint32_t binding, const void* data, uint32_t size,
                                  uint32_t set = 0);

    // Set per-draw push constants. Payload becomes visible to the next
    // draw call at binding slot TGFX2_PUSH_CONSTANTS_BINDING (GL) or via
    // `layout(push_constant)` on Vulkan. Max payload is
    // TGFX2_PUSH_CONSTANTS_MAX_BYTES (128 bytes, Vulkan-compat). Typical
    // use: model matrix and other per-object data that changes every
    // draw — avoids the churn of uploading/rebinding a full UBO.
    void set_push_constants(const void* data, uint32_t size);

    // Queue a handle for destruction at the end of the current frame.
    // Used by pass code that wraps externally owned GL resources as non-owning
    // tgfx2 handles (register_external_texture / register_external_buffer)
    // and needs them alive only for the draws in this frame. The
    // underlying GL object is preserved; only the tgfx2 HandlePool entry
    // is removed.
    void defer_destroy(TextureHandle handle);
    void defer_destroy(BufferHandle handle);

    // Register a sampled texture at the given binding slot. The sampler is
    // optional; if omitted, the backend uses the texture's default sampling
    // parameters (useful for GL 3.3 style shaders without separate samplers).
    void bind_sampled_texture(uint32_t binding, TextureHandle tex,
                              SamplerHandle sampler = {},
                              uint32_t set = 0);
    void bind_sampled_texture_array_element(
        uint32_t binding,
        uint32_t array_element,
        TextureHandle tex,
        SamplerHandle sampler = {},
        uint32_t set = 0
    );

    // Drop all pending resource bindings — next draw starts from an empty
    // resource set.
    void clear_resource_bindings();

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
    void draw_indexed_instanced(BufferHandle vertex_vbo,
                                BufferHandle index_buffer,
                                BufferHandle instance_vbo,
                                uint32_t index_count,
                                uint32_t instance_count,
                                IndexType idx_type = IndexType::Uint32);
    void draw_indexed_instanced(BufferHandle vertex_vbo,
                                uint64_t vertex_offset,
                                BufferHandle index_buffer,
                                uint64_t index_offset,
                                BufferHandle instance_vbo,
                                uint64_t instance_offset,
                                uint32_t index_count,
                                uint32_t instance_count,
                                IndexType idx_type = IndexType::Uint32);

    // Draw non-indexed.
    void draw_arrays(BufferHandle vbo, uint32_t vertex_count);
    void draw_arrays_instanced(BufferHandle vbo,
                               uint32_t vertex_count,
                               uint32_t instance_count);
    void draw_arrays_instanced(BufferHandle vertex_vbo,
                               BufferHandle instance_vbo,
                               uint32_t vertex_count,
                               uint32_t instance_count);
    void draw_arrays_instanced(BufferHandle vertex_vbo,
                               uint64_t vertex_offset,
                               BufferHandle instance_vbo,
                               uint64_t instance_offset,
                               uint32_t vertex_count,
                               uint32_t instance_count);

    // --- Immediate drawing ---
    // Fast-path for transient UI/debug streams: hands vertices to the
    // device's transient vertex ring (persistent VBO with sub-upload)
    // when available, falls back to per-draw buffer creation when the
    // backend doesn't provide a ring. Vertex layout is fixed:
    // [x,y,z, r,g,b,a] — 7 floats per vertex.
    void draw_immediate_lines(const float* data, uint32_t vertex_count);
    void draw_immediate_triangles(const float* data, uint32_t vertex_count);

    // --- Blit ---
    void blit(TextureHandle src, TextureHandle dst);

    // --- Access ---
    IRenderDevice& device() { return device_; }
    ICommandList* cmd() { return cmd_.get(); }

    // Force pending render state to be resolved into an active pipeline.
    // Normally called internally from draw*() methods; exposed for diagnostics
    // and low-level integration points that need the backend pipeline bound
    // before a draw call.
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
    // Shared body for draw_immediate_lines / draw_immediate_triangles.
    void draw_immediate_generic(const float* data, uint32_t vertex_count,
                                PrimitiveTopology topo);

    void ensure_fsq_resources();
    void flush_resource_set();
};

} // namespace tgfx
