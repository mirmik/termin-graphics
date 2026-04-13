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
    // clear_color = nullptr means LoadOp::Load (don't clear).
    void begin_pass(TextureHandle color, TextureHandle depth = {},
                    const float* clear_color = nullptr,
                    float clear_depth = 1.0f);
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

    // --- Texture binding (for legacy uniform-based passes) ---
    // These set pending texture slots. Actual binding depends on backend.
    void bind_texture(uint32_t unit, TextureHandle tex);

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

    // Fullscreen quad resources (created on first use)
    BufferHandle fsq_vbo_;
    BufferHandle fsq_ibo_;
    ShaderHandle fsq_vs_;

    void ensure_fsq_resources();
};

} // namespace tgfx2
