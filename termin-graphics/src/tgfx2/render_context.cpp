// render_context.cpp - Mid-level rendering abstraction over tgfx2.
#include "tgfx2/render_context.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"

#include <cstring>

namespace tgfx2 {

// ============================================================================
// Fullscreen quad shader (built-in, minimal)
// ============================================================================

static const char* FSQ_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

// Fullscreen quad geometry: two triangles covering [-1,1]
// Vertex format: [x, y, u, v]
static const float FSQ_VERTICES[] = {
    -1.f, -1.f,  0.f, 0.f,
     1.f, -1.f,  1.f, 0.f,
     1.f,  1.f,  1.f, 1.f,
    -1.f,  1.f,  0.f, 1.f,
};

static const uint32_t FSQ_INDICES[] = { 0, 1, 2, 0, 2, 3 };

// ============================================================================
// Construction / Destruction
// ============================================================================

RenderContext2::RenderContext2(IRenderDevice& device, PipelineCache& cache)
    : device_(device), cache_(cache) {}

RenderContext2::~RenderContext2() {
    if (fsq_vbo_) device_.destroy(fsq_vbo_);
    if (fsq_ibo_) device_.destroy(fsq_ibo_);
    if (fsq_vs_) device_.destroy(fsq_vs_);
}

// ============================================================================
// Frame lifecycle
// ============================================================================

void RenderContext2::begin_frame() {
    cmd_ = device_.create_command_list();
    cmd_->begin();
}

void RenderContext2::end_frame() {
    if (in_pass_) {
        end_pass();
    }
    cmd_->end();
    device_.submit(*cmd_);
    cmd_.reset();
}

// ============================================================================
// Render pass
// ============================================================================

void RenderContext2::begin_pass(
    TextureHandle color, TextureHandle depth,
    const float* clear_color, float clear_depth
) {
    if (in_pass_) {
        end_pass();
    }

    RenderPassDesc pass;

    ColorAttachmentDesc color_att;
    color_att.texture = color;
    if (clear_color) {
        color_att.load = LoadOp::Clear;
        memcpy(color_att.clear_color, clear_color, sizeof(float) * 4);
    } else {
        color_att.load = LoadOp::Load;
    }
    pass.colors.push_back(color_att);

    if (depth) {
        DepthAttachmentDesc depth_att;
        depth_att.texture = depth;
        depth_att.load = clear_color ? LoadOp::Clear : LoadOp::Load;
        depth_att.clear_depth = clear_depth;
        pass.depth = depth_att;
        pass.has_depth = true;
    }

    cmd_->begin_render_pass(pass);
    in_pass_ = true;
    pipeline_dirty_ = true;
}

void RenderContext2::end_pass() {
    if (!in_pass_) return;
    cmd_->end_render_pass();
    in_pass_ = false;
}

// ============================================================================
// Mutable render state
// ============================================================================

void RenderContext2::set_depth_test(bool enabled) {
    if (depth_stencil_.depth_test != enabled) {
        depth_stencil_.depth_test = enabled;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_depth_write(bool enabled) {
    if (depth_stencil_.depth_write != enabled) {
        depth_stencil_.depth_write = enabled;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_depth_func(CompareOp op) {
    if (depth_stencil_.depth_compare != op) {
        depth_stencil_.depth_compare = op;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_blend(bool enabled) {
    if (blend_.enabled != enabled) {
        blend_.enabled = enabled;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_blend_func(BlendFactor src, BlendFactor dst) {
    if (blend_.src_color != src || blend_.dst_color != dst) {
        blend_.src_color = src;
        blend_.dst_color = dst;
        blend_.src_alpha = src;
        blend_.dst_alpha = dst;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_cull(CullMode mode) {
    if (raster_.cull != mode) {
        raster_.cull = mode;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_polygon_mode(PolygonMode mode) {
    if (raster_.polygon_mode != mode) {
        raster_.polygon_mode = mode;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_color_mask(bool r, bool g, bool b, bool a) {
    if (color_mask_.r != r || color_mask_.g != g ||
        color_mask_.b != b || color_mask_.a != a) {
        color_mask_ = {r, g, b, a};
        pipeline_dirty_ = true;
    }
}

// ============================================================================
// Shader / layout / format
// ============================================================================

void RenderContext2::bind_shader(ShaderHandle vs, ShaderHandle fs, ShaderHandle gs) {
    if (bound_vs_ != vs || bound_fs_ != fs || bound_gs_ != gs) {
        bound_vs_ = vs;
        bound_fs_ = fs;
        bound_gs_ = gs;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_vertex_layout(const VertexBufferLayout& layout) {
    vertex_layout_ = layout;
    pipeline_dirty_ = true;
}

void RenderContext2::set_topology(PrimitiveTopology topo) {
    if (topology_ != topo) {
        topology_ = topo;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::bind_texture(uint32_t /*unit*/, TextureHandle /*tex*/) {
    // In the OpenGL path, textures are still bound via glActiveTexture/glBindTexture
    // by the legacy code. This is a placeholder for future resource set integration.
    // For now, render passes bind textures directly through the GL interop.
}

void RenderContext2::set_color_format(PixelFormat fmt) {
    if (color_format_ != fmt) {
        color_format_ = fmt;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_depth_format(PixelFormat fmt) {
    if (depth_format_ != fmt) {
        depth_format_ = fmt;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::set_sample_count(uint32_t samples) {
    if (sample_count_ != samples) {
        sample_count_ = samples;
        pipeline_dirty_ = true;
    }
}

// ============================================================================
// Viewport / Scissor
// ============================================================================

void RenderContext2::set_viewport(int x, int y, int w, int h) {
    cmd_->set_viewport((float)x, (float)y, (float)w, (float)h);
}

void RenderContext2::set_scissor(int x, int y, int w, int h) {
    cmd_->set_scissor((float)x, (float)y, (float)w, (float)h);
}

void RenderContext2::clear_scissor() {
    // Reset scissor to full viewport — effectively disabling it.
    // Actual implementation depends on backend; for now set a large rect.
    cmd_->set_scissor(0, 0, 16384, 16384);
}

// ============================================================================
// Pipeline resolution
// ============================================================================

void RenderContext2::flush_pipeline() {
    if (!pipeline_dirty_) return;

    PipelineCacheKey key;
    key.vertex_shader = bound_vs_;
    key.fragment_shader = bound_fs_;
    key.geometry_shader = bound_gs_;
    key.vertex_layout = vertex_layout_;
    key.topology = topology_;
    key.raster = raster_;
    key.depth_stencil = depth_stencil_;
    key.blend = blend_;
    key.color_mask = color_mask_;
    key.color_format = color_format_;
    key.depth_format = depth_format_;
    key.sample_count = sample_count_;

    auto pipeline = cache_.get(key);
    cmd_->bind_pipeline(pipeline);
    pipeline_dirty_ = false;
}

// ============================================================================
// Drawing
// ============================================================================

void RenderContext2::ensure_fsq_resources() {
    if (fsq_vbo_) return;

    // Create VBO
    BufferDesc vbo_desc;
    vbo_desc.size = sizeof(FSQ_VERTICES);
    vbo_desc.usage = BufferUsage::Vertex;
    fsq_vbo_ = device_.create_buffer(vbo_desc);
    device_.upload_buffer(fsq_vbo_, {
        reinterpret_cast<const uint8_t*>(FSQ_VERTICES),
        sizeof(FSQ_VERTICES)
    });

    // Create IBO
    BufferDesc ibo_desc;
    ibo_desc.size = sizeof(FSQ_INDICES);
    ibo_desc.usage = BufferUsage::Index;
    fsq_ibo_ = device_.create_buffer(ibo_desc);
    device_.upload_buffer(fsq_ibo_, {
        reinterpret_cast<const uint8_t*>(FSQ_INDICES),
        sizeof(FSQ_INDICES)
    });

    // Create built-in vertex shader
    ShaderDesc vs_desc;
    vs_desc.stage = ShaderStage::Vertex;
    vs_desc.source = FSQ_VERT_SRC;
    fsq_vs_ = device_.create_shader(vs_desc);
}

void RenderContext2::draw_fullscreen_quad() {
    ensure_fsq_resources();

    // Set FSQ vertex layout if not already set
    VertexBufferLayout fsq_layout;
    fsq_layout.stride = 4 * sizeof(float);
    fsq_layout.attributes = {
        {0, VertexFormat::Float2, 0},                    // aPos
        {1, VertexFormat::Float2, 2 * sizeof(float)},    // aUV
    };
    set_vertex_layout(fsq_layout);

    // If no vertex shader bound, use built-in FSQ vertex shader
    ShaderHandle vs = bound_vs_ ? bound_vs_ : fsq_vs_;
    if (vs != bound_vs_) {
        bind_shader(vs, bound_fs_, bound_gs_);
    }

    flush_pipeline();

    cmd_->bind_vertex_buffer(0, fsq_vbo_);
    cmd_->bind_index_buffer(fsq_ibo_, IndexType::Uint32);
    cmd_->draw_indexed(6);
}

void RenderContext2::draw(
    BufferHandle vbo, BufferHandle ibo,
    uint32_t index_count, IndexType idx_type
) {
    flush_pipeline();
    cmd_->bind_vertex_buffer(0, vbo);
    cmd_->bind_index_buffer(ibo, idx_type);
    cmd_->draw_indexed(index_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo, uint32_t vertex_count) {
    flush_pipeline();
    cmd_->bind_vertex_buffer(0, vbo);
    cmd_->draw(vertex_count);
}

void RenderContext2::draw_immediate_lines(const float* data, uint32_t vertex_count) {
    if (vertex_count == 0) return;

    // 7 floats per vertex: x,y,z, r,g,b,a
    size_t byte_size = vertex_count * 7 * sizeof(float);

    BufferDesc desc;
    desc.size = byte_size;
    desc.usage = BufferUsage::Vertex;
    auto buf = device_.create_buffer(desc);
    device_.upload_buffer(buf, {reinterpret_cast<const uint8_t*>(data), byte_size});

    VertexBufferLayout layout;
    layout.stride = 7 * sizeof(float);
    layout.attributes = {
        {0, VertexFormat::Float3, 0},                    // position
        {1, VertexFormat::Float4, 3 * sizeof(float)},    // color
    };
    set_vertex_layout(layout);
    set_topology(PrimitiveTopology::LineList);

    flush_pipeline();
    cmd_->bind_vertex_buffer(0, buf);
    cmd_->draw(vertex_count);

    // Cleanup temp buffer
    device_.destroy(buf);
}

void RenderContext2::draw_immediate_triangles(const float* data, uint32_t vertex_count) {
    if (vertex_count == 0) return;

    size_t byte_size = vertex_count * 7 * sizeof(float);

    BufferDesc desc;
    desc.size = byte_size;
    desc.usage = BufferUsage::Vertex;
    auto buf = device_.create_buffer(desc);
    device_.upload_buffer(buf, {reinterpret_cast<const uint8_t*>(data), byte_size});

    VertexBufferLayout layout;
    layout.stride = 7 * sizeof(float);
    layout.attributes = {
        {0, VertexFormat::Float3, 0},
        {1, VertexFormat::Float4, 3 * sizeof(float)},
    };
    set_vertex_layout(layout);
    set_topology(PrimitiveTopology::TriangleList);

    flush_pipeline();
    cmd_->bind_vertex_buffer(0, buf);
    cmd_->draw(vertex_count);

    device_.destroy(buf);
}

void RenderContext2::blit(TextureHandle src, TextureHandle dst) {
    cmd_->copy_texture(src, dst);
}

} // namespace tgfx2
