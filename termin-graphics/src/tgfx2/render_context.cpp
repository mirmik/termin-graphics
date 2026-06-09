// render_context.cpp - Mid-level rendering abstraction over tgfx2.
#include "tgfx2/render_context.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
extern "C" {
#include "tc_profiler.h"
}

#include <cstring>
#include <string>

namespace tgfx {

// ============================================================================
// Fullscreen quad shader (built-in, minimal)
// ============================================================================

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
    const bool profile = tc_profiler_enabled();
    if (in_pass_) {
        end_pass();
    }
    if (profile) tc_profiler_begin_section("RenderContext2::end_frame");
    if (profile) tc_profiler_begin_section("RenderContext2::submit");
    cmd_->end();
    device_.submit(*cmd_);
    cmd_.reset();
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("RenderContext2::destroy_transients");
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
    if (profile) tc_profiler_end_section();
    if (profile) tc_profiler_end_section();
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
    // Descriptor/resource bindings are render-pass local in practice:
    // a later pass may use the same binding numbers for completely
    // different textures or buffers. Keep stale sampler slots from
    // leaking into shaders that intentionally leave a slot unbound and
    // rely on backend defaults.
    pending_bindings_.clear();
    bindings_dirty_ = true;
    // Vulkan's cmd-buffer-level binds don't survive a render pass
    // boundary — reset cached state so draw() re-binds on first use.
    last_bound_vbos_.clear();
    last_bound_vbo_offsets_.clear();
    last_bound_ibo_ = {};
    last_bound_ibo_offset_ = 0;
    last_bound_index_type_ = IndexType::Uint32;
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

void RenderContext2::set_depth_bias(bool enabled, float constant, float slope, float clamp) {
    if (raster_.depth_bias_enabled != enabled ||
            raster_.depth_bias_constant != constant ||
            raster_.depth_bias_slope != slope ||
            raster_.depth_bias_clamp != clamp) {
        raster_.depth_bias_enabled = enabled;
        raster_.depth_bias_constant = constant;
        raster_.depth_bias_slope = slope;
        raster_.depth_bias_clamp = clamp;
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
    set_vertex_layouts({layout});
}

void RenderContext2::set_vertex_layouts(const std::vector<VertexBufferLayout>& layouts) {
    vertex_layouts_ = layouts;
    // Cache the layout's hash once here so flush_pipeline's cache lookup
    // doesn't have to iterate the attributes vector on every draw.
    size_t h = 0;
    auto mix = [&h](size_t v) {
        h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    mix(std::hash<size_t>{}(layouts.size()));
    for (const auto& layout : layouts) {
        mix(std::hash<uint32_t>{}(layout.stride));
        mix(std::hash<bool>{}(layout.per_instance));
        mix(std::hash<size_t>{}(layout.attributes.size()));
        for (const auto& a : layout.attributes) {
            mix(std::hash<uint32_t>{}(a.location));
            mix(std::hash<int>{}(static_cast<int>(a.format)));
            mix(std::hash<uint32_t>{}(a.offset));
        }
    }
    vertex_layouts_hash_ = h;
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

static ResourceBinding* find_binding(
    std::vector<ResourceBinding>& bindings,
    uint32_t binding,
    ResourceBinding::Kind kind,
    uint32_t set = 0,
    uint32_t array_element = 0
) {
    for (auto& b : bindings) {
        if (b.set == set && b.binding == binding && b.kind == kind && b.array_element == array_element) {
            return &b;
        }
    }
    return nullptr;
}

void RenderContext2::bind_uniform_buffer(uint32_t binding, BufferHandle buffer,
                                          uint64_t offset, uint64_t range,
                                          uint32_t set) {
    ResourceBinding* existing =
        find_binding(pending_bindings_, binding, ResourceBinding::Kind::UniformBuffer, set);
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
        b.set = set;
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
                                                const void* data, uint32_t size,
                                                uint32_t set) {
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
        bind_uniform_buffer(binding, tmp, 0, size, set);
        return;
    }
    uint32_t offset = device_.ring_ubo_write(data, size);
    bind_uniform_buffer(binding, ring, offset, size, set);
}

void RenderContext2::bind_sampled_texture(uint32_t binding, TextureHandle tex,
                                           SamplerHandle sampler, uint32_t set) {
    bind_sampled_texture_array_element(binding, 0, tex, sampler, set);
}

void RenderContext2::bind_sampled_texture_array_element(
    uint32_t binding,
    uint32_t array_element,
    TextureHandle tex,
    SamplerHandle sampler,
    uint32_t set
) {
    ResourceBinding* existing =
        find_binding(
            pending_bindings_,
            binding,
            ResourceBinding::Kind::SampledTexture,
            set,
            array_element
        );
    if (existing) {
        if (existing->texture == tex && existing->sampler == sampler) {
            return;
        }
        existing->texture = tex;
        existing->sampler = sampler;
    } else {
        ResourceBinding b;
        b.set = set;
        b.kind = ResourceBinding::Kind::SampledTexture;
        b.binding = binding;
        b.array_element = array_element;
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
    // Stash the bytes and mark dirty — the actual vkCmdPushConstants
    // happens inside flush_pipeline() right before the draw, which also
    // handles the re-emit-on-new-layout case after a pipeline change.
    // The old path emitted immediately when pipeline_dirty_ was false,
    // which produced a double emit every time callers did
    // set_push_constants() → set_blend(...) → draw() — ~20% of draws,
    // visible as pushC ≈ 1.4 per draw in the hot-path summary.
    if (data == nullptr || size == 0) {
        pending_push_constants_.clear();
        push_constants_dirty_ = false;
        return;
    }
    pending_push_constants_.assign(
        reinterpret_cast<const uint8_t*>(data),
        reinterpret_cast<const uint8_t*>(data) + size);
    push_constants_dirty_ = true;
}

void RenderContext2::defer_destroy(TextureHandle handle) {
    if (handle) deferred_destroy_textures_.push_back(handle);
}

void RenderContext2::defer_destroy(BufferHandle handle) {
    if (handle) deferred_destroy_buffers_.push_back(handle);
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
    if (!pipeline_dirty_) {
        // Pipeline wasn't touched, but push-constants might still be
        // pending from a set_push_constants() call that came after the
        // previous draw. Emit them here so the next draw sees fresh
        // bytes without paying the pipeline-cache lookup.
        if (push_constants_dirty_ && !pending_push_constants_.empty()) {
            cmd_->set_push_constants(pending_push_constants_.data(),
                                     static_cast<uint32_t>(pending_push_constants_.size()));
            push_constants_dirty_ = false;
        }
        return;
    }

    PipelineCacheKey key;
    key.vertex_shader = bound_vs_;
    key.fragment_shader = bound_fs_;
    key.geometry_shader = bound_gs_;
    key.vertex_layouts = vertex_layouts_;
    key.vertex_layouts_hash = vertex_layouts_hash_;
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
        // Vertex attribute interpretation is pipeline-local (and OpenGL
        // recreates the VAO on bind_pipeline), so force vertex buffers to
        // be rebound even if the handle ids are unchanged.
        last_bound_vbos_.clear();
        last_bound_vbo_offsets_.clear();
        // A new VkPipeline brings a (possibly new) VkPipelineLayout — the
        // previous push-constant bytes are no longer guaranteed to be
        // visible to the next draw. Force a re-emit.
        push_constants_dirty_ = true;
    }
    pipeline_dirty_ = false;

    if (push_constants_dirty_ && !pending_push_constants_.empty()) {
        cmd_->set_push_constants(pending_push_constants_.data(),
                                 static_cast<uint32_t>(pending_push_constants_.size()));
        push_constants_dirty_ = false;
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

        // Dynamic UBO offsets: Vulkan's shared layout declares six
        // UNIFORM_BUFFER_DYNAMIC slots — bindings 0, 1, 2, 3, 16, 24 (lighting,
        // material, per-frame, shadow, bone block, Slang draw data) — and expects their
        // offsets in that ascending order at bind time. Pick them out of
        // pending_bindings_ by matching binding numbers; slots left unset
        // default to 0 (the descriptor write already points them at the
        // ring buffer with range=WHOLE, so offset=0 is always in bounds).
        // OpenGL ignores the offsets array and reads offset straight from
        // the ResourceBinding — same source of truth, no duplication.
        static constexpr uint32_t DYN_BINDINGS[6] = {0, 1, 2, 3, 16, 24};
        uint32_t offsets[6] = {0, 0, 0, 0, 0, 0};
        for (const auto& b : pending_bindings_) {
            if (b.kind != ResourceBinding::Kind::UniformBuffer) continue;
            for (uint32_t i = 0; i < 6; ++i) {
                if (DYN_BINDINGS[i] == b.binding) {
                    offsets[i] = static_cast<uint32_t>(b.offset);
                    break;
                }
            }
        }
        cmd_->bind_resource_set(current_resource_set_, 0, offsets, 6);
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
    const EngineShaderStageSource& fsq_shader = engine_fullscreen_quad_vertex_shader();
    ShaderDesc vs_desc;
    vs_desc.stage = fsq_shader.stage;
    vs_desc.debug_name = std::string(fsq_shader.uuid) + ":vertex";
    if (device_.backend_type() == BackendType::Vulkan
        && termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
            fsq_shader,
            device_.backend_type(),
            vs_desc.bytecode)) {
    } else {
        vs_desc.source = fsq_shader.fallback_glsl_source;
    }
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
    // fullscreen-pass output.
    set_topology(PrimitiveTopology::TriangleList);

    // Fullscreen quad drawing owns its vertex stream. Reusing an
    // arbitrary previously-bound mesh VS is invalid on Vulkan: the FSQ
    // layout only provides locations 0/1, while a material VS may require
    // 0/1/2 or more. Fragment-only post effects bind just their FS and rely
    // on this helper to force the matching FSQ VS every time.
    if (fsq_vs_ != bound_vs_) {
        bind_shader(fsq_vs_, bound_fs_, bound_gs_);
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
    if (last_bound_vbos_.size() <= 0) {
        last_bound_vbos_.resize(1);
        last_bound_vbo_offsets_.resize(1);
    }
    if (vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != 0) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbos_[0] = vbo;
        last_bound_vbo_offsets_[0] = 0;
    }
    if (ibo != last_bound_ibo_
        || last_bound_ibo_offset_ != 0
        || idx_type != last_bound_index_type_) {
        cmd_->bind_index_buffer(ibo, idx_type);
        last_bound_ibo_ = ibo;
        last_bound_ibo_offset_ = 0;
        last_bound_index_type_ = idx_type;
    }
    cmd_->draw_indexed(index_count);
}

void RenderContext2::draw_indexed_instanced(
    BufferHandle vertex_vbo,
    BufferHandle index_buffer,
    BufferHandle instance_vbo,
    uint32_t index_count,
    uint32_t instance_count,
    IndexType idx_type
) {
    draw_indexed_instanced(
        vertex_vbo,
        0,
        index_buffer,
        0,
        instance_vbo,
        0,
        index_count,
        instance_count,
        idx_type
    );
}

void RenderContext2::draw_indexed_instanced(
    BufferHandle vertex_vbo,
    uint64_t vertex_offset,
    BufferHandle index_buffer,
    uint64_t index_offset,
    BufferHandle instance_vbo,
    uint64_t instance_offset,
    uint32_t index_count,
    uint32_t instance_count,
    IndexType idx_type
) {
    flush_pipeline();
    flush_resource_set();
    if (last_bound_vbos_.size() < 2) {
        last_bound_vbos_.resize(2);
        last_bound_vbo_offsets_.resize(2);
    }
    if (vertex_vbo != last_bound_vbos_[0]
        || vertex_offset != last_bound_vbo_offsets_[0]) {
        cmd_->bind_vertex_buffer(0, vertex_vbo, vertex_offset);
        last_bound_vbos_[0] = vertex_vbo;
        last_bound_vbo_offsets_[0] = vertex_offset;
    }
    if (instance_vbo != last_bound_vbos_[1]
        || instance_offset != last_bound_vbo_offsets_[1]) {
        cmd_->bind_vertex_buffer(1, instance_vbo, instance_offset);
        last_bound_vbos_[1] = instance_vbo;
        last_bound_vbo_offsets_[1] = instance_offset;
    }
    if (index_buffer != last_bound_ibo_
        || index_offset != last_bound_ibo_offset_
        || idx_type != last_bound_index_type_) {
        cmd_->bind_index_buffer(index_buffer, idx_type, index_offset);
        last_bound_ibo_ = index_buffer;
        last_bound_ibo_offset_ = index_offset;
        last_bound_index_type_ = idx_type;
    }
    cmd_->draw_indexed_instanced(index_count, instance_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo, uint32_t vertex_count) {
    flush_pipeline();
    flush_resource_set();
    if (last_bound_vbos_.size() <= 0) {
        last_bound_vbos_.resize(1);
        last_bound_vbo_offsets_.resize(1);
    }
    if (vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != 0) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbos_[0] = vbo;
        last_bound_vbo_offsets_[0] = 0;
    }
    cmd_->draw(vertex_count);
}

void RenderContext2::draw_arrays_instanced(BufferHandle vbo,
                                           uint32_t vertex_count,
                                           uint32_t instance_count) {
    flush_pipeline();
    flush_resource_set();
    if (last_bound_vbos_.size() <= 0) {
        last_bound_vbos_.resize(1);
        last_bound_vbo_offsets_.resize(1);
    }
    if (vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != 0) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbos_[0] = vbo;
        last_bound_vbo_offsets_[0] = 0;
    }
    cmd_->draw_instanced(vertex_count, instance_count);
}

void RenderContext2::draw_arrays_instanced(BufferHandle vertex_vbo,
                                           BufferHandle instance_vbo,
                                           uint32_t vertex_count,
                                           uint32_t instance_count) {
    flush_pipeline();
    flush_resource_set();
    if (last_bound_vbos_.size() < 2) {
        last_bound_vbos_.resize(2);
        last_bound_vbo_offsets_.resize(2);
    }
    if (vertex_vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != 0) {
        cmd_->bind_vertex_buffer(0, vertex_vbo);
        last_bound_vbos_[0] = vertex_vbo;
        last_bound_vbo_offsets_[0] = 0;
    }
    if (instance_vbo != last_bound_vbos_[1] || last_bound_vbo_offsets_[1] != 0) {
        cmd_->bind_vertex_buffer(1, instance_vbo);
        last_bound_vbos_[1] = instance_vbo;
        last_bound_vbo_offsets_[1] = 0;
    }
    cmd_->draw_instanced(vertex_count, instance_count);
}

void RenderContext2::draw_arrays_instanced(BufferHandle vertex_vbo,
                                           uint64_t vertex_offset,
                                           BufferHandle instance_vbo,
                                           uint64_t instance_offset,
                                           uint32_t vertex_count,
                                           uint32_t instance_count) {
    flush_pipeline();
    flush_resource_set();
    if (last_bound_vbos_.size() < 2) {
        last_bound_vbos_.resize(2);
        last_bound_vbo_offsets_.resize(2);
    }
    if (vertex_vbo != last_bound_vbos_[0]
        || vertex_offset != last_bound_vbo_offsets_[0]) {
        cmd_->bind_vertex_buffer(0, vertex_vbo, vertex_offset);
        last_bound_vbos_[0] = vertex_vbo;
        last_bound_vbo_offsets_[0] = vertex_offset;
    }
    if (instance_vbo != last_bound_vbos_[1]
        || instance_offset != last_bound_vbo_offsets_[1]) {
        cmd_->bind_vertex_buffer(1, instance_vbo, instance_offset);
        last_bound_vbos_[1] = instance_vbo;
        last_bound_vbo_offsets_[1] = instance_offset;
    }
    cmd_->draw_instanced(vertex_count, instance_count);
}

// Immediate vertex draw scaffold shared by draw_immediate_lines/_triangles.
// Tries the device's transient vertex ring (persistent VBO with sub-upload)
// first; falls back to create_buffer + upload_buffer + deferred destroy if
// the backend doesn't provide a ring. This keeps small UI/debug streams off
// the per-draw allocation path on both OpenGL and Vulkan.
void RenderContext2::draw_immediate_generic(const float* data,
                                            uint32_t vertex_count,
                                            PrimitiveTopology topo) {
    if (vertex_count == 0) return;

    const size_t byte_size = vertex_count * 7 * sizeof(float);
    const bool profile = tc_profiler_enabled();

    BufferHandle buf;
    uint64_t vb_offset = 0;
    bool used_ring = false;

    if (profile) tc_profiler_begin_section("immediate.upload");
    const uint64_t ring_offset = device_.transient_vertex_write(
        data, static_cast<uint32_t>(byte_size));
    if (ring_offset != UINT64_MAX) {
        buf = device_.transient_vertex_buffer();
        vb_offset = ring_offset;
        used_ring = true;
    } else {
        BufferDesc desc;
        desc.size = byte_size;
        desc.usage = BufferUsage::Vertex;
        buf = device_.create_buffer(desc);
        device_.upload_buffer(buf,
            {reinterpret_cast<const uint8_t*>(data), byte_size});
    }
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("immediate.pipeline");
    VertexBufferLayout layout;
    layout.stride = 7 * sizeof(float);
    layout.attributes = {
        {0, VertexFormat::Float3, 0},
        {1, VertexFormat::Float4, 3 * sizeof(float)},
    };
    set_vertex_layout(layout);
    set_topology(topo);

    flush_pipeline();
    flush_resource_set();
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("immediate.draw");
    cmd_->bind_vertex_buffer(0, buf, vb_offset);
    cmd_->draw(vertex_count);
    if (profile) tc_profiler_end_section();

    // Ring buffers are process-lifetime; only the fallback path queues
    // the buffer for destruction after the current frame's submit
    // (Vulkan needs the VkBuffer alive until the queue drains).
    if (!used_ring) {
        deferred_destroy_buffers_.push_back(buf);
    }
}

void RenderContext2::draw_immediate_lines(const float* data, uint32_t vertex_count) {
    draw_immediate_generic(data, vertex_count, PrimitiveTopology::LineList);
}

void RenderContext2::draw_immediate_triangles(const float* data, uint32_t vertex_count) {
    draw_immediate_generic(data, vertex_count, PrimitiveTopology::TriangleList);
}

void RenderContext2::blit(TextureHandle src, TextureHandle dst) {
    cmd_->copy_texture(src, dst);
}

} // namespace tgfx
