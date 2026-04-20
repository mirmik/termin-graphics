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

// Built-in FSQ vertex shader. `#version 450 core` + explicit location
// qualifiers on varyings — both required for Vulkan shaderc (SPIR-V).
// GL 4.3+ accepts the same source via core GL_ARB_shading_language_420pack.
static const char* FSQ_VERT_SRC = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 vUV;
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
    for (auto h : deferred_destroy_resource_sets_) device_.destroy(h);
    deferred_destroy_textures_.clear();
    deferred_destroy_buffers_.clear();
    deferred_destroy_resource_sets_.clear();
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

    // Sync the pipeline's multisample state with the attachment's actual
    // sample count. FBOPool may allocate MSAA textures (e.g. scene color
    // at 4x) while the pipeline cache key defaults to sample_count=1 —
    // Vulkan then refuses vkCreateFramebuffer with SAMPLE_COUNT_4 vs
    // SAMPLE_COUNT_1 mismatch. Pick the attachment that's present; if
    // both are, trust the color (depth should match by construction).
    uint32_t new_samples = 1;
    if (color) {
        new_samples = device_.texture_desc(color).sample_count;
    } else if (depth) {
        new_samples = device_.texture_desc(depth).sample_count;
    }
    if (new_samples == 0) new_samples = 1;
    if (sample_count_ != new_samples) {
        sample_count_ = new_samples;
        pipeline_dirty_ = true;
    }

    cmd_->begin_render_pass(pass);
    in_pass_ = true;
    // Vulkan's cmd-buffer-level binds don't survive a render pass
    // boundary — reset cached state so draw() re-binds on first use.
    last_bound_vbo_ = {};
    last_bound_ibo_ = {};
    last_bound_pipeline_ = {};
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

void RenderContext2::bind_uniform_buffer_ring(uint32_t binding,
                                                const void* data, uint32_t size) {
    if (!data || size == 0) return;
    // Write into the device ring; the returned offset is aligned to
    // minUniformBufferOffsetAlignment. An invalid ring (handle.id == 0)
    // means the backend hasn't implemented the ring path — fall back to
    // a transient UBO via the classic API so behaviour stays correct.
    BufferHandle ring = device_.ring_ubo_handle();
    if (ring.id == 0) {
        // Transient UBO fallback — creates garbage at the callrate of
        // whoever used this API before the backend grew a real ring.
        BufferDesc bd;
        bd.size = size;
        bd.usage = BufferUsage::Uniform;
        bd.cpu_visible = true;
        BufferHandle tmp = device_.create_buffer(bd);
        device_.upload_buffer(tmp, {reinterpret_cast<const uint8_t*>(data), size});
        defer_destroy(tmp);
        bind_uniform_buffer(binding, tmp, 0, size);
        return;
    }
    uint32_t offset = device_.ring_ubo_write(data, size);
    bind_uniform_buffer(binding, ring, offset, size);
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
    // vkCmdPushConstants requires a bound pipeline. Callers typically
    // do bind_shader → set_push_constants → draw; at this point the
    // vertex layout is still unset, so flushing now would build an
    // invalid pipeline. Queue the data so flush_pipeline re-emits it
    // after each pipeline bind.
    //
    // If a pipeline is already bound (pipeline_dirty_ == false), apply
    // immediately. Without this, a second draw that reuses the same
    // pipeline would skip flush_pipeline and silently drop the new
    // push-constants — manifests as every-other-draw artefacts (e.g.
    // gizmos where only the first primitive of a shader is visible).
    if (data == nullptr || size == 0) {
        pending_push_constants_.clear();
        return;
    }
    pending_push_constants_.assign(
        reinterpret_cast<const uint8_t*>(data),
        reinterpret_cast<const uint8_t*>(data) + size);
    if (!pipeline_dirty_ && bound_vs_) {
        cmd_->set_push_constants(pending_push_constants_.data(),
                                 static_cast<uint32_t>(pending_push_constants_.size()));
    }
}

void RenderContext2::defer_destroy(TextureHandle handle) {
    if (handle) deferred_destroy_textures_.push_back(handle);
}

void RenderContext2::defer_destroy(BufferHandle handle) {
    if (handle) deferred_destroy_buffers_.push_back(handle);
}

// --- Transitional legacy-uniform setters ---
//
// These exist only for shaders that still declare plain `uniform sampler2D
// u_foo;` / `uniform mat4 u_view;` style legacy uniforms on the GL path.
// On Vulkan the whole mechanism is a dead end: SPIR-V has no name-based
// uniform lookup, samplers and matrices live in descriptor sets + UBOs
// that are wired by explicit `layout(binding = N)`. So every one of these
// helpers short-circuits when the process has no GL loader — we detect
// that through glad's function-pointer table being zeroed out.

namespace {
// True only when glad loaded a real OpenGL function. If the process
// picked the Vulkan backend, `gladLoadGLLoader` was never called and
// `glad_glGetIntegerv` (the macro behind `glGetIntegerv`) is still
// nullptr. Calling through it crashes — this guard turns all the
// helpers below into silent no-ops on Vulkan, which matches the
// semantic they already have on a Vulkan shader (no legacy plain
// uniforms to bind).
inline bool gl_loader_ready() {
    return glad_glGetIntegerv != nullptr;
}
} // namespace

void RenderContext2::set_uniform_int(const char* name, int value) {
    if (!name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform1i(loc, value);
}

void RenderContext2::set_uniform_float(const char* name, float value) {
    if (!name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform1f(loc, value);
}

void RenderContext2::set_uniform_mat4(const char* name, const float* data,
                                      bool transpose) {
    if (!name || !data || !gl_loader_ready()) return;
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
    if (!name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

void RenderContext2::set_uniform_vec3(const char* name, float x, float y, float z) {
    if (!name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}

void RenderContext2::set_uniform_vec4(const char* name, float x, float y, float z, float w) {
    if (!name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = glGetUniformLocation(static_cast<GLuint>(prog), name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

void RenderContext2::set_uniform_mat4_array(const char* name, const float* data,
                                             int count, bool transpose) {
    if (!name || !data || count <= 0 || !gl_loader_ready()) return;
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
    if (!block_name || !gl_loader_ready()) return;
    flush_pipeline();
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLuint idx = glGetUniformBlockIndex(static_cast<GLuint>(prog), block_name);
    if (idx == GL_INVALID_INDEX) return;
    glUniformBlockBinding(static_cast<GLuint>(prog), idx, binding_slot);
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
    // Redundant-bind culling at the pipeline level. Pass code often
    // calls set_depth_test/set_blend/set_cull/bind_shader per draw, which
    // flips pipeline_dirty_ even when the final pipeline ends up
    // identical (same state combo hashes into the same VkPipeline).
    // Skipping the vkCmdBindPipeline when the handle didn't actually
    // change drops the cmd-recording cost in half on scenes with many
    // draws sharing one material.
    bool pipeline_changed = (pipeline.id != last_bound_pipeline_.id);
    if (pipeline_changed) {
        cmd_->bind_pipeline(pipeline);
        last_bound_pipeline_ = pipeline;
    }
    pipeline_dirty_ = false;

    // Re-emit pending push-constants every flush when we got here through
    // a set_* call that flipped pipeline_dirty_ on — the `if (!pipeline_
    // dirty_) return;` early-out above already skipped the no-op case.
    // Callers that set_push_constants() WHILE pipeline_dirty_ was true
    // stashed the bytes in pending_push_constants_ without emitting, so
    // we have to flush them here even when the resolved VkPipeline turns
    // out to be the same as before — otherwise the next draw reads stale
    // push-constant bytes and e.g. all instanced cubes collapse onto one
    // model matrix.
    if (!pending_push_constants_.empty()) {
        cmd_->set_push_constants(pending_push_constants_.data(),
                                 static_cast<uint32_t>(pending_push_constants_.size()));
    }
}

void RenderContext2::flush_resource_set() {
    if (!bindings_dirty_) return;

    // Defer-destroy the previous set: the command buffer we're still
    // recording into may reference it, and Vulkan forbids mutating
    // bindings that a live cmd buf holds. Drained in end_frame() after
    // submit + fence wait. On OpenGL `destroy(ResourceSetHandle)` is
    // cheap and this deferment is harmless.
    if (current_resource_set_) {
        deferred_destroy_resource_sets_.push_back(current_resource_set_);
        current_resource_set_ = {};
    }

    if (!pending_bindings_.empty()) {
        ResourceSetDesc desc;
        desc.bindings = pending_bindings_;
        current_resource_set_ = device_.create_resource_set(desc);

        // Dynamic UBO offsets: Vulkan's shared layout declares five
        // UNIFORM_BUFFER_DYNAMIC slots — bindings 0, 1, 2, 3, 16 (lighting,
        // material, per-frame, shadow, bone block) — and expects their
        // offsets in that ascending order at bind time. Pick them out of
        // pending_bindings_ by matching binding numbers; slots left unset
        // default to 0 (the descriptor write already points them at the
        // ring buffer with range=WHOLE, so offset=0 is always in bounds).
        // OpenGL ignores the offsets array and reads offset straight from
        // the ResourceBinding — same source of truth, no duplication.
        static constexpr uint32_t DYN_BINDINGS[5] = {0, 1, 2, 3, 16};
        uint32_t offsets[5] = {0, 0, 0, 0, 0};
        for (const auto& b : pending_bindings_) {
            if (b.kind != ResourceBinding::Kind::UniformBuffer) continue;
            for (uint32_t i = 0; i < 5; ++i) {
                if (DYN_BINDINGS[i] == b.binding) {
                    offsets[i] = static_cast<uint32_t>(b.offset);
                    break;
                }
            }
        }
        cmd_->bind_resource_set(current_resource_set_, offsets, 5);
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
    // Explicit topology — previous draws (ImmediateRenderer line/point
    // batches) may leave topology_ pointing at LineList, which makes
    // the FSQ index list `{0,1,2, 0,2,3}` render as three line segments
    // instead of two triangles. Was showing up as a diagonal sliver in
    // PostFX output.
    set_topology(PrimitiveTopology::TriangleList);

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
    // Redundant-bind culling: a pipeline-compatible sequence of draws
    // over the same mesh (think: several chronosquad enemies rendered
    // from the same instanced VBO) hits this path hundreds of times.
    // vkCmdBindVertexBuffers / vkCmdBindIndexBuffer each cost a cmd-buffer
    // word + driver trampoline — redundant ones add up to a measurable
    // slice of cmd-recording time.
    if (vbo != last_bound_vbo_) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbo_ = vbo;
    }
    if (ibo != last_bound_ibo_) {
        cmd_->bind_index_buffer(ibo, idx_type);
        last_bound_ibo_ = ibo;
    }
    cmd_->draw_indexed(index_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo, uint32_t vertex_count) {
    flush_pipeline();
    flush_resource_set();
    if (vbo != last_bound_vbo_) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbo_ = vbo;
    }
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
