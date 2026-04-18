// render_context.cpp - Mid-level rendering abstraction over tgfx2.
#include "tgfx2/render_context.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

#include <glad/glad.h>
#include <cstring>

namespace tgfx {

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
    if (current_resource_set_) device_.destroy(current_resource_set_);
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

    if (current_resource_set_) {
        device_.destroy(current_resource_set_);
        current_resource_set_ = {};
    }
    pending_bindings_.clear();
    bindings_dirty_ = true;

    // Drain deferred-destroy lists. Pass code accumulated non-owning
    // external wrappers during the frame (e.g. material textures,
    // temporary mesh buffers); now that the command list is submitted
    // and the GL bindings have been copied out, the wrapper HandlePool
    // entries can be safely released. Underlying GL objects survive.
    for (auto h : deferred_destroy_textures_) device_.destroy(h);
    for (auto h : deferred_destroy_buffers_)  device_.destroy(h);
    deferred_destroy_textures_.clear();
    deferred_destroy_buffers_.clear();
}

// ============================================================================
// Render pass
// ============================================================================

void RenderContext2::begin_pass(
    TextureHandle color, TextureHandle depth,
    const float* clear_color, float clear_depth,
    bool clear_depth_enabled
) {
    if (in_pass_) {
        end_pass();
    }

    RenderPassDesc pass;

    // Only push the color attachment when caller supplied a valid
    // texture handle — a default-constructed TextureHandle (id == 0)
    // means "depth-only pass", and pushing a placeholder entry would
    // try to attach a nonexistent texture in get_or_create_fbo().
    if (color) {
        ColorAttachmentDesc color_att;
        color_att.texture = color;
        if (clear_color) {
            color_att.load = LoadOp::Clear;
            memcpy(color_att.clear_color, clear_color, sizeof(float) * 4);
        } else {
            color_att.load = LoadOp::Load;
        }
        pass.colors.push_back(color_att);
    }

    if (depth) {
        DepthAttachmentDesc depth_att;
        depth_att.texture = depth;
        depth_att.load = clear_depth_enabled ? LoadOp::Clear : LoadOp::Load;
        depth_att.clear_depth = clear_depth;
        pass.depth = depth_att;
        pass.has_depth = true;
    }

    // Sync the pipeline-key formats with what the pass actually carries.
    // Without this the cache keeps whatever format was set earlier (or
    // the D32F default for depth) and builds a VkRenderPass that doesn't
    // match begin_render_pass — Vulkan then fails with
    //   vkCmdDraw: RenderPasses incompatible (attachment count mismatch).
    PixelFormat new_color = color
        ? device_.texture_desc(color).format
        : PixelFormat::Undefined;
    PixelFormat new_depth = depth
        ? device_.texture_desc(depth).format
        : PixelFormat::Undefined;
    if (color_format_ != new_color) {
        color_format_ = new_color;
        pipeline_dirty_ = true;
    }
    if (depth_format_ != new_depth) {
        depth_format_ = new_depth;
        pipeline_dirty_ = true;
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

// ============================================================================
// Resource bindings (UBOs, textures, samplers)
// ============================================================================

static ResourceBinding* find_binding(std::vector<ResourceBinding>& bindings,
                                     uint32_t binding, ResourceBinding::Kind kind) {
    for (auto& b : bindings) {
        if (b.binding == binding && b.kind == kind) return &b;
    }
    return nullptr;
}

void RenderContext2::bind_uniform_buffer(uint32_t binding, BufferHandle buffer,
                                          uint64_t offset, uint64_t range) {
    ResourceBinding* existing =
        find_binding(pending_bindings_, binding, ResourceBinding::Kind::UniformBuffer);
    if (existing) {
        if (existing->buffer == buffer && existing->offset == offset &&
            existing->range == range) {
            return;
        }
        existing->buffer = buffer;
        existing->offset = offset;
        existing->range = range;
    } else {
        ResourceBinding b;
        b.kind = ResourceBinding::Kind::UniformBuffer;
        b.binding = binding;
        b.buffer = buffer;
        b.offset = offset;
        b.range = range;
        pending_bindings_.push_back(b);
    }
    bindings_dirty_ = true;
}

void RenderContext2::bind_sampled_texture(uint32_t binding, TextureHandle tex,
                                           SamplerHandle sampler) {
    ResourceBinding* existing =
        find_binding(pending_bindings_, binding, ResourceBinding::Kind::SampledTexture);
    if (existing) {
        if (existing->texture == tex && existing->sampler == sampler) {
            return;
        }
        existing->texture = tex;
        existing->sampler = sampler;
    } else {
        ResourceBinding b;
        b.kind = ResourceBinding::Kind::SampledTexture;
        b.binding = binding;
        b.texture = tex;
        b.sampler = sampler;
        pending_bindings_.push_back(b);
    }
    bindings_dirty_ = true;
}

void RenderContext2::clear_resource_bindings() {
    if (pending_bindings_.empty()) return;
    pending_bindings_.clear();
    bindings_dirty_ = true;
}

void RenderContext2::set_push_constants(const void* data, uint32_t size) {
    if (!cmd_) return;
    cmd_->set_push_constants(data, size);
}

void RenderContext2::defer_destroy(TextureHandle handle) {
    if (handle) deferred_destroy_textures_.push_back(handle);
}

void RenderContext2::defer_destroy(BufferHandle handle) {
    if (handle) deferred_destroy_buffers_.push_back(handle);
}

// --- Transitional legacy-uniform setters ---

void RenderContext2::set_uniform_int(const char* name, int value) {
    if (!name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform1i(loc, value);
}

void RenderContext2::set_uniform_float(const char* name, float value) {
    if (!name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform1f(loc, value);
}

void RenderContext2::set_uniform_mat4(const char* name, const float* data,
                                      bool transpose) {
    if (!name || !data) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1,
                           transpose ? GL_TRUE : GL_FALSE,
                           data);
    }
}

void RenderContext2::set_uniform_vec2(const char* name, float x, float y) {
    if (!name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

void RenderContext2::set_uniform_vec3(const char* name, float x, float y, float z) {
    if (!name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}

void RenderContext2::set_uniform_vec4(const char* name, float x, float y, float z, float w) {
    if (!name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

void RenderContext2::set_uniform_mat4_array(const char* name, const float* data,
                                             int count, bool transpose) {
    if (!name || !data || count <= 0) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, count,
                           transpose ? GL_TRUE : GL_FALSE,
                           data);
    }
}

void RenderContext2::set_block_binding(const char* block_name, uint32_t binding_slot) {
    if (!block_name) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLuint idx = glGetUniformBlockIndex(static_cast<GLuint>(prog), block_name);
    if (idx == GL_INVALID_INDEX) return;
    glUniformBlockBinding(static_cast<GLuint>(prog), idx, binding_slot);
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

void RenderContext2::flush_resource_set() {
    if (!bindings_dirty_) return;

    if (current_resource_set_) {
        device_.destroy(current_resource_set_);
        current_resource_set_ = {};
    }

    if (!pending_bindings_.empty()) {
        ResourceSetDesc desc;
        desc.bindings = pending_bindings_;
        current_resource_set_ = device_.create_resource_set(desc);
        cmd_->bind_resource_set(current_resource_set_);
    }

    bindings_dirty_ = false;
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

ShaderHandle RenderContext2::fsq_vertex_shader() {
    ensure_fsq_resources();
    return fsq_vs_;
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
    flush_resource_set();

    cmd_->bind_vertex_buffer(0, fsq_vbo_);
    cmd_->bind_index_buffer(fsq_ibo_, IndexType::Uint32);
    cmd_->draw_indexed(6);
}

void RenderContext2::draw(
    BufferHandle vbo, BufferHandle ibo,
    uint32_t index_count, IndexType idx_type
) {
    flush_pipeline();
    flush_resource_set();
    cmd_->bind_vertex_buffer(0, vbo);
    cmd_->bind_index_buffer(ibo, idx_type);
    cmd_->draw_indexed(index_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo, uint32_t vertex_count) {
    flush_pipeline();
    flush_resource_set();
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
    flush_resource_set();
    cmd_->bind_vertex_buffer(0, buf);
    cmd_->draw(vertex_count);

    // Defer destroy — on Vulkan the command buffer runs asynchronously
    // after vkQueueSubmit, so the VkBuffer must outlive end_frame().
    // The deferred list is drained in end_frame() after device_.submit
    // has waited on the frame fence. OpenGL is equally happy either way
    // (glBufferData copies host memory immediately).
    deferred_destroy_buffers_.push_back(buf);
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
    flush_resource_set();
    cmd_->bind_vertex_buffer(0, buf);
    cmd_->draw(vertex_count);

    // Defer destroy — see draw_immediate_lines for the lifetime story.
    deferred_destroy_buffers_.push_back(buf);
}

void RenderContext2::blit(TextureHandle src, TextureHandle dst) {
    cmd_->copy_texture(src, dst);
}

uint32_t RenderContext2::last_gl_error() {
    // glad lives in this translation unit's module, so glGetError
    // is always a valid function pointer here even when the caller
    // is in another DLL (e.g. the Python binding module).
    return static_cast<uint32_t>(glGetError());
}

} // namespace tgfx
