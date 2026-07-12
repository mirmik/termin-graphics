// render_context.cpp - Mid-level rendering abstraction over tgfx2.
#include "tgfx2/render_context.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tcbase/tc_log.h"
extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tc_profiler.h"
}

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <span>
#include <utility>

namespace tgfx {

namespace {

const char* render_context_backend_name(BackendType backend) {
    switch (backend) {
        case BackendType::OpenGL: return "opengl";
        case BackendType::Vulkan: return "vulkan";
        case BackendType::Metal: return "metal";
        case BackendType::D3D11: return "d3d11";
        case BackendType::Null: return "null";
    }
    return "unknown";
}

bool fixed_resource_name_equals(const char* stored, std::string_view name) {
    for (size_t i = 0; i < TC_SHADER_RESOURCE_NAME_MAX; ++i) {
        const char ch = stored[i];
        if (ch == '\0') {
            return i == name.size();
        }
        if (i >= name.size() || ch != name[i]) {
            return false;
        }
    }
    return name.size() == TC_SHADER_RESOURCE_NAME_MAX;
}

VertexLayoutDesc fullscreen_quad_vertex_layout_desc() {
    VertexLayoutDesc layout;
    layout.stride = 4 * sizeof(float);
    layout.attribute_count = 2;
    layout.attributes[0] = {
        0,
        VertexFormat::Float2,
        0,
        intern_vertex_semantic("position"),
    };
    layout.attributes[1] = {
        1,
        VertexFormat::Float2,
        2 * sizeof(float),
        intern_vertex_semantic("uv"),
    };
    return layout;
}

} // namespace

// ============================================================================
// Fullscreen quad shader (built-in, minimal)
// ============================================================================

// Fullscreen quad geometry: two triangles covering [-1,1].
// Vertex format: [x, y, u, v].
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
    clear_pending_binding_buckets();

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

    // Keep RenderContext2's high-level viewport cache in sync with the
    // render target selected by this pass. Some backends set the native
    // viewport inside begin_render_pass(), but renderers such as Text3D
    // also need the size on the CPU side for pixel-space expansion.
    if (color) {
        const TextureDesc desc = device_.texture_desc(color);
        viewport_w_ = std::max(1, static_cast<int>(desc.width));
        viewport_h_ = std::max(1, static_cast<int>(desc.height));
    } else if (depth) {
        const TextureDesc desc = device_.texture_desc(depth);
        viewport_w_ = std::max(1, static_cast<int>(desc.width));
        viewport_h_ = std::max(1, static_cast<int>(desc.height));
    }

    // Sync the pipeline's multisample state with the attachment's actual
    // sample count. FBOPool may allocate MSAA textures (e.g. scene color
    // at 4x) while the pipeline cache key defaults to sample_count=1 -
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
    clear_pending_binding_buckets();
    // Vulkan's cmd-buffer-level binds don't survive a render pass
    // boundary — reset cached state so draw() re-binds on first use.
    reset_cached_vertex_buffers();
    last_bound_ibo_ = {};
    last_bound_ibo_offset_ = 0;
    last_bound_index_type_ = IndexType::Uint32;
    last_bound_pipeline_ = {};
    last_bound_resource_layout_token_ = 0;
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
    const VertexLayoutDesc desc = make_vertex_layout_desc(layout);
    set_vertex_layout(desc);
}

void RenderContext2::set_vertex_layouts(const std::vector<VertexBufferLayout>& layouts) {
    std::vector<VertexLayoutDesc> descs;
    descs.reserve(layouts.size());
    for (const VertexBufferLayout& layout : layouts) {
        descs.push_back(make_vertex_layout_desc(layout));
    }
    set_vertex_layouts(
        descs.data(),
        static_cast<uint32_t>(descs.size()));
}

void RenderContext2::set_vertex_layout(const VertexLayoutDesc& layout) {
    set_vertex_layouts(&layout, 1);
}

void RenderContext2::set_vertex_layouts(
    const VertexLayoutDesc* layouts,
    uint32_t count
) {
    vertex_layouts_.clear();
    vertex_layouts_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        vertex_layouts_.push_back(make_vertex_layout_desc(layouts[i]));
    }
    vertex_layouts_hash_ = hash_vertex_layout_descs(
        vertex_layouts_.data(),
        static_cast<uint32_t>(vertex_layouts_.size()));
    pipeline_dirty_ = true;
}

void RenderContext2::set_topology(PrimitiveTopology topo) {
    if (topology_ != topo) {
        topology_ = topo;
        pipeline_dirty_ = true;
    }
}

void RenderContext2::reset_cached_vertex_buffers() {
    last_bound_vbos_.fill({});
    last_bound_vbo_offsets_.fill(0);
    last_bound_vbo_count_ = 0;
}

void RenderContext2::ensure_cached_vertex_buffer_slots(uint32_t count) {
    if (count <= last_bound_vbo_count_) {
        return;
    }
    if (count > kTrackedVertexBufferMax) {
        tc_log(TC_LOG_ERROR,
               "RenderContext2: requested %u cached vertex buffer slots, max=%u",
               count,
               kTrackedVertexBufferMax);
        count = kTrackedVertexBufferMax;
    }
    for (uint32_t i = last_bound_vbo_count_; i < count; ++i) {
        last_bound_vbos_[i] = {};
        last_bound_vbo_offsets_[i] = 0;
    }
    last_bound_vbo_count_ = count;
}

// ============================================================================
// Resource bindings (UBOs, textures, samplers)
// ============================================================================

RenderContext2::ResourceScope RenderContext2::scope_from_shader_resource(
    uint32_t shader_scope
) {
    switch (shader_scope) {
        case TC_SHADER_RESOURCE_SCOPE_FRAME: return ResourceScope::Frame;
        case TC_SHADER_RESOURCE_SCOPE_PASS: return ResourceScope::Pass;
        case TC_SHADER_RESOURCE_SCOPE_MATERIAL: return ResourceScope::Material;
        case TC_SHADER_RESOURCE_SCOPE_DRAW: return ResourceScope::Draw;
        case TC_SHADER_RESOURCE_SCOPE_TRANSIENT: return ResourceScope::Transient;
        case TC_SHADER_RESOURCE_SCOPE_UNSCOPED:
        case TC_SHADER_RESOURCE_SCOPE_UNKNOWN:
        default:
            return ResourceScope::Unknown;
    }
}

ShaderResourceScope RenderContext2::shader_scope_from_resource_scope(
    ResourceScope scope
) {
    switch (scope) {
        case ResourceScope::Frame: return ShaderResourceScope::Frame;
        case ResourceScope::Pass: return ShaderResourceScope::Pass;
        case ResourceScope::Material: return ShaderResourceScope::Material;
        case ResourceScope::Draw: return ShaderResourceScope::Draw;
        case ResourceScope::Transient: return ShaderResourceScope::Transient;
        case ResourceScope::Unknown:
        case ResourceScope::Count:
        default:
            return ShaderResourceScope::Unknown;
    }
}

void RenderContext2::mark_binding_scope_dirty(ResourceScope scope) {
    dirty_binding_scopes_[static_cast<size_t>(scope)] = true;
    bindings_dirty_ = true;
}

void RenderContext2::mark_all_binding_scopes_dirty() {
    dirty_binding_scopes_.fill(true);
    bindings_dirty_ = true;
}

void RenderContext2::clear_dirty_binding_scopes() {
    dirty_binding_scopes_.fill(false);
    bindings_dirty_ = false;
}

bool same_backend_placement(
    const BackendPlacement& a,
    const BackendPlacement& b
) {
    return a.kind == b.kind &&
        a.vulkan.set == b.vulkan.set &&
        a.vulkan.binding == b.vulkan.binding &&
        a.vulkan.descriptor_kind == b.vulkan.descriptor_kind &&
        a.d3d11.register_class == b.d3d11.register_class &&
        a.d3d11.register_index == b.d3d11.register_index &&
        a.opengl.binding_class == b.opengl.binding_class &&
        a.opengl.binding_point == b.opengl.binding_point &&
        a.opengl.texture_unit == b.opengl.texture_unit;
}

bool same_backend_bound_resource_slot(
    const BackendBoundResourceSlot& a,
    const BackendBoundResourceSlot& b
) {
    return a.kind == b.kind &&
        a.scope == b.scope &&
        a.stage_mask == b.stage_mask &&
        a.array_count == b.array_count &&
        a.size == b.size &&
        same_backend_placement(a.placement, b.placement) &&
        a.debug_name == b.debug_name;
}

bool same_bound_resource_value(
    const BoundResourceValue& a,
    const BoundResourceValue& b
) {
    return a.kind == b.kind &&
        a.buffer == b.buffer &&
        a.texture == b.texture &&
        a.sampler == b.sampler &&
        a.offset == b.offset &&
        a.range == b.range &&
        a.array_element == b.array_element;
}

BoundResourceBinding* RenderContext2::find_planned_binding(
    std::vector<BoundResourceBinding>& bindings,
    const BackendBoundResourceSlot& slot,
    const BoundResourceValue& value
) {
    for (BoundResourceBinding& binding : bindings) {
        if (same_backend_placement(binding.slot.placement, slot.placement) &&
            binding.slot.kind == slot.kind &&
            binding.value.array_element == value.array_element) {
            return &binding;
        }
    }
    return nullptr;
}

void RenderContext2::upsert_pending_planned_binding(
    ResourceScope scope,
    const BackendBoundResourceSlot& slot,
    const BoundResourceValue& value
) {
    auto& bucket = pending_binding_buckets_[static_cast<size_t>(scope)];
    BoundResourceBinding* existing =
        find_planned_binding(bucket.planned, slot, value);
    if (existing) {
        if (same_backend_bound_resource_slot(existing->slot, slot) &&
            same_bound_resource_value(existing->value, value)) {
            return;
        }
        existing->slot = slot;
        existing->value = value;
    } else {
        bucket.planned.push_back({slot, value});
    }
    mark_binding_scope_dirty(scope);
}

bool RenderContext2::pending_binding_buckets_empty() const {
    for (const ResourceBindingBucket& bucket : pending_binding_buckets_) {
        if (!bucket.planned.empty()) {
            return false;
        }
    }
    return true;
}

void RenderContext2::clear_pending_binding_buckets() {
    for (ResourceBindingBucket& bucket : pending_binding_buckets_) {
        bucket.planned.clear();
    }
    mark_all_binding_scopes_dirty();
}

bool RenderContext2::any_dirty_binding_scope() const {
    for (bool dirty : dirty_binding_scopes_) {
        if (dirty) {
            return true;
        }
    }
    return false;
}

BoundResourceSetDesc RenderContext2::build_pending_bound_resource_set(
    uintptr_t resource_layout_token
) const {
    BoundResourceSetDesc bound_desc;
    bound_desc.resource_layout_token = resource_layout_token;

    bound_desc.groups.reserve(pending_binding_buckets_.size());
    for (size_t i = 0; i < pending_binding_buckets_.size(); ++i) {
        const ResourceBindingBucket& bucket = pending_binding_buckets_[i];
        if (bucket.planned.empty()) {
            continue;
        }
        BoundResourceGroup group;
        group.scope = shader_scope_from_resource_scope(
            static_cast<ResourceScope>(i));
        group.dirty = dirty_binding_scopes_[i];
        group.bindings = bucket.planned;
        bound_desc.groups.push_back(std::move(group));
    }
    return bound_desc;
}

const tc_shader_resource_binding* RenderContext2::active_resource_binding_by_name(
    std::string_view name,
    const char* action
) const {
    if (!active_shader_layout_) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: %s('%.*s') called without active shader layout",
               action,
               static_cast<int>(name.size()),
               name.data());
        return nullptr;
    }

    const tc_shader_resource_binding* bindings =
        tc_shader_resource_bindings(active_shader_layout_);
    const uint32_t count = tc_shader_resource_binding_count(active_shader_layout_);
    if (count > 0 && !bindings) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: %s('%.*s') shader '%s' has invalid resource binding table",
               action,
               static_cast<int>(name.size()),
               name.data(),
               active_shader_layout_->name ? active_shader_layout_->name
                                           : active_shader_layout_->uuid);
        return nullptr;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (fixed_resource_name_equals(bindings[i].name, name)) {
            return &bindings[i];
        }
    }

    tc_log(TC_LOG_WARN,
           "RenderContext2: %s('%.*s') not found in shader '%s'",
           action,
           static_cast<int>(name.size()),
           name.data(),
           active_shader_layout_->name ? active_shader_layout_->name
                                       : active_shader_layout_->uuid);
    return nullptr;
}

const BackendBindingPlanEntry* RenderContext2::active_backend_binding_for(
    const tc_shader_resource_binding* rb,
    const char* action
) const {
    if (!rb) {
        tc_log(TC_LOG_WARN, "RenderContext2: %s called with null shader resource binding", action);
        return nullptr;
    }
    if (!active_shader_layout_) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: %s('%s') called without active shader layout",
               action,
               rb->name);
        return nullptr;
    }
    if (!active_backend_binding_plan_valid_) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: %s('%s') skipped because backend binding plan is unavailable",
               action,
               rb->name);
        return nullptr;
    }

    const tc_shader_resource_binding* bindings =
        tc_shader_resource_bindings(active_shader_layout_);
    const uint32_t count = tc_shader_resource_binding_count(active_shader_layout_);
    if (count > 0 && !bindings) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: %s('%s') shader '%s' has invalid resource binding table",
               action,
               rb->name,
               active_shader_layout_->name ? active_shader_layout_->name
                                           : active_shader_layout_->uuid);
        return nullptr;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (&bindings[i] != rb) {
            continue;
        }
        if (i >= active_backend_binding_plan_.entries.size()) {
            tc_log(TC_LOG_WARN,
                   "RenderContext2: %s('%s') missing backend binding plan entry at index=%u",
                   action,
                   rb->name,
                   i);
            return nullptr;
        }
        const BackendBindingPlanEntry& entry = active_backend_binding_plan_.entries[i];
        if (entry.resource.name != rb->name ||
            static_cast<uint32_t>(entry.resource.kind) != rb->kind ||
            static_cast<uint32_t>(entry.resource.scope) != rb->scope) {
            tc_log(TC_LOG_WARN,
                   "RenderContext2: %s('%s') backend binding plan entry mismatch at index=%u",
                   action,
                   rb->name,
                   i);
            return nullptr;
        }
        return &entry;
    }

    tc_log(TC_LOG_WARN,
           "RenderContext2: %s('%s') binding does not belong to active shader '%s'",
           action,
           rb->name,
           active_shader_layout_->name ? active_shader_layout_->name
                                       : active_shader_layout_->uuid);
    return nullptr;
}

void RenderContext2::clear_resource_bindings() {
    if (pending_binding_buckets_empty()) return;
    clear_pending_binding_buckets();
}

// ============================================================================
// Symbolic resource binding
// ============================================================================

void RenderContext2::use_shader_resource_layout(const struct ::tc_shader* shader) {
    if (!shader) {
        active_shader_layout_ = nullptr;
        active_backend_binding_plan_ = {};
        active_backend_binding_plan_valid_ = false;
        if (!pending_binding_buckets_empty()) {
            clear_pending_binding_buckets();
        }
        return;
    }

    const bool layout_changed = active_shader_layout_ != shader;
    active_shader_layout_ = shader;
    active_backend_binding_plan_ = {};
    std::string plan_error;
    active_backend_binding_plan_valid_ = build_backend_binding_plan(
        device_.backend_type(),
        tc_shader_resource_bindings(shader),
        tc_shader_resource_binding_count(shader),
        active_backend_binding_plan_,
        &plan_error);
    if (!active_backend_binding_plan_valid_) {
        tc_log(TC_LOG_ERROR,
               "RenderContext2: failed to build backend binding plan for shader '%s' backend=%s: %s",
               shader->name ? shader->name : shader->uuid,
               render_context_backend_name(device_.backend_type()),
               plan_error.empty() ? "unknown error" : plan_error.c_str());
    }
    // Bindings resolved for the previous layout must not be flushed against
    // the new shader.
    bool cleared = false;
    for (size_t i = 0; i < pending_binding_buckets_.size(); ++i) {
        ResourceBindingBucket& bucket = pending_binding_buckets_[i];
        if (layout_changed && !bucket.planned.empty()) {
            bucket.planned.clear();
            mark_binding_scope_dirty(static_cast<ResourceScope>(i));
            cleared = true;
        }
    }
    if (cleared) {
        bindings_dirty_ = true;
    }
}

void RenderContext2::bind_uniform(std::string_view name, BufferHandle buffer,
                                  uint64_t offset, uint64_t range) {
    bind_uniform(active_resource_binding_by_name(name, "bind_uniform"), buffer, offset, range);
}

void RenderContext2::bind_uniform(const tc_shader_resource_binding* rb,
                                  BufferHandle buffer,
                                  uint64_t offset,
                                  uint64_t range) {
    if (!rb) return;
    if (rb->kind != TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: bind_uniform('%s') resolved to kind=%u, expected constant_buffer",
               rb->name,
               rb->kind);
        return;
    }
    const BackendBindingPlanEntry* plan_entry =
        active_backend_binding_for(rb, "bind_uniform");
    if (!plan_entry) return;

    BoundResourceValue value;
    value.kind = BoundResourceKind::UniformBuffer;
    value.buffer = buffer;
    value.offset = offset;
    value.range = range;
    const BackendBoundResourceSlot slot =
        bound_resource_slot_from_plan_entry(*plan_entry);
    upsert_pending_planned_binding(
        scope_from_shader_resource(rb->scope),
        slot,
        value);
}

void RenderContext2::bind_storage_buffer(std::string_view name, BufferHandle buffer,
                                         uint64_t offset, uint64_t range) {
    bind_storage_buffer(
        active_resource_binding_by_name(name, "bind_storage_buffer"),
        buffer,
        offset,
        range);
}

void RenderContext2::bind_storage_buffer(const tc_shader_resource_binding* rb,
                                         BufferHandle buffer,
                                         uint64_t offset,
                                         uint64_t range) {
    if (!rb) return;
    if (rb->kind != TC_SHADER_RESOURCE_STORAGE_BUFFER) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: bind_storage_buffer('%s') resolved to kind=%u, expected storage_buffer",
               rb->name,
               rb->kind);
        return;
    }
    const BackendBindingPlanEntry* plan_entry =
        active_backend_binding_for(rb, "bind_storage_buffer");
    if (!plan_entry) return;

    BoundResourceValue value;
    value.kind = BoundResourceKind::StorageBuffer;
    value.buffer = buffer;
    value.offset = offset;
    value.range = range;
    const BackendBoundResourceSlot slot =
        bound_resource_slot_from_plan_entry(*plan_entry);
    upsert_pending_planned_binding(
        scope_from_shader_resource(rb->scope),
        slot,
        value);
}

void RenderContext2::bind_texture(std::string_view name, TextureHandle texture,
                                  SamplerHandle sampler) {
    bind_texture_array_element(name, 0, texture, sampler);
}

void RenderContext2::bind_texture(const tc_shader_resource_binding* rb,
                                  TextureHandle texture,
                                  SamplerHandle sampler) {
    bind_texture_array_element(rb, 0, texture, sampler);
}

void RenderContext2::bind_texture_array_element(std::string_view name,
                                                uint32_t array_element,
                                                TextureHandle texture,
                                                SamplerHandle sampler) {
    bind_texture_array_element(
        active_resource_binding_by_name(name, "bind_texture"),
        array_element,
        texture,
        sampler);
}

void RenderContext2::bind_texture_array_element(const tc_shader_resource_binding* rb,
                                                uint32_t array_element,
                                                TextureHandle texture,
                                                SamplerHandle sampler) {
    if (!rb) return;
    if (rb->kind != TC_SHADER_RESOURCE_TEXTURE) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: bind_texture('%s') resolved to kind=%u, expected texture",
               rb->name,
               rb->kind);
        return;
    }
    const BackendBindingPlanEntry* plan_entry =
        active_backend_binding_for(rb, "bind_texture");
    if (!plan_entry) return;

    BoundResourceValue value;
    value.kind = BoundResourceKind::SampledTexture;
    value.texture = texture;
    value.sampler = sampler;
    value.array_element = array_element;
    const BackendBoundResourceSlot slot =
        bound_resource_slot_from_plan_entry(*plan_entry);
    upsert_pending_planned_binding(
        scope_from_shader_resource(rb->scope),
        slot,
        value);
}

void RenderContext2::bind_uniform_data(std::string_view name, const void* data, uint32_t size) {
    bind_uniform_data(active_resource_binding_by_name(name, "bind_uniform_data"), data, size);
}

void RenderContext2::bind_uniform_data(const tc_shader_resource_binding* rb,
                                       const void* data,
                                       uint32_t size) {
    if (!data || size == 0 || !rb) return;
    if (rb->kind != TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
        tc_log(TC_LOG_WARN,
               "RenderContext2: bind_uniform_data('%s') resolved to kind=%u, expected constant_buffer",
               rb->name,
               rb->kind);
        return;
    }
    const BackendBindingPlanEntry* plan_entry =
        active_backend_binding_for(rb, "bind_uniform_data");
    if (!plan_entry) return;

    BufferHandle buffer = device_.ring_ubo_handle();
    uint32_t ring_offset = 0;
    const bool ring_write_succeeded =
        buffer.id != 0 && device_.ring_ubo_write(data, size, ring_offset);
    uint64_t offset = ring_offset;
    if (!ring_write_succeeded) {
        BufferDesc bd;
        bd.size = size;
        bd.usage = BufferUsage::Uniform;
        bd.cpu_visible = true;
        buffer = device_.create_buffer(bd);
        device_.upload_buffer(buffer, {reinterpret_cast<const uint8_t*>(data), size});
        defer_destroy(buffer);
    }

    BoundResourceValue value;
    value.kind = BoundResourceKind::UniformBuffer;
    value.buffer = buffer;
    value.offset = offset;
    value.range = size;
    const BackendBoundResourceSlot slot =
        bound_resource_slot_from_plan_entry(*plan_entry);
    upsert_pending_planned_binding(
        scope_from_shader_resource(rb->scope),
        slot,
        value);
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
        pending_push_constants_size_ = 0;
        push_constants_dirty_ = false;
        return;
    }
    if (size > TGFX2_PUSH_CONSTANTS_MAX_BYTES) {
        tc_log(TC_LOG_ERROR,
               "RenderContext2: push constants payload size=%u exceeds max=%u",
               size,
               TGFX2_PUSH_CONSTANTS_MAX_BYTES);
        pending_push_constants_size_ = 0;
        push_constants_dirty_ = false;
        return;
    }
    std::memcpy(pending_push_constants_.data(), data, size);
    pending_push_constants_size_ = size;
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
    viewport_w_ = std::max(1, w);
    viewport_h_ = std::max(1, h);
    cmd_->set_viewport((float)x, (float)y, (float)w, (float)h);
}

void RenderContext2::set_scissor(int x, int y, int w, int h) {
    cmd_->set_scissor((float)x, (float)y, (float)w, (float)h);
}

void RenderContext2::clear_scissor() {
    // Keep scissoring enabled, but restore it to the active render extent.
    // The cached extent is synchronized from color or depth attachments in
    // begin_pass() and from explicit viewport changes in set_viewport().
    cmd_->set_scissor(0, 0, viewport_w_, viewport_h_);
}

// ============================================================================
// Pipeline resolution
// ============================================================================

bool RenderContext2::flush_pipeline() {
    if (!pipeline_dirty_) {
        // Pipeline wasn't touched, but push-constants might still be
        // pending from a set_push_constants() call that came after the
        // previous draw. Emit them here so the next draw sees fresh
        // bytes without paying the pipeline-cache lookup.
        if (push_constants_dirty_ && pending_push_constants_size_ != 0) {
            cmd_->set_push_constants(pending_push_constants_.data(),
                                     pending_push_constants_size_);
            push_constants_dirty_ = false;
        }
        return true;
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
    if (depth_format_ == PixelFormat::Undefined) {
        key.depth_stencil.depth_test = false;
        key.depth_stencil.depth_write = false;
    }
    key.blend = blend_;
    key.color_mask = color_mask_;
    key.color_format = color_format_;
    key.depth_format = depth_format_;
    key.sample_count = sample_count_;

    auto pipeline = cache_.get(key);
    if (!pipeline) {
        tc_log(TC_LOG_ERROR,
               "RenderContext2: pipeline creation failed; skipping draw until the backend recovers");
        return false;
    }
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
        const uint64_t resource_layout_token =
            device_.pipeline_resource_layout_token(pipeline);
        if (resource_layout_token != last_bound_resource_layout_token_) {
            if (current_resource_set_) {
                deferred_destroy_resource_sets_.push_back(current_resource_set_);
                current_resource_set_ = {};
            }
            last_bound_resource_layout_token_ = resource_layout_token;
            mark_all_binding_scopes_dirty();
        }
        // Vertex/index binding state is pipeline-local on OpenGL because
        // bind_pipeline() recreates the VAO. GL_ELEMENT_ARRAY_BUFFER is VAO
        // state too, so both VBOs and IBO must be rebound even when handle ids
        // are unchanged. Other backends tolerate the extra bind after a
        // pipeline change.
        reset_cached_vertex_buffers();
        last_bound_ibo_ = {};
        last_bound_ibo_offset_ = 0;
        last_bound_index_type_ = IndexType::Uint32;
        // A new VkPipeline brings a (possibly new) VkPipelineLayout — the
        // previous push-constant bytes are no longer guaranteed to be
        // visible to the next draw. Force a re-emit.
        push_constants_dirty_ = true;
    }
    pipeline_dirty_ = false;

    if (push_constants_dirty_ && pending_push_constants_size_ != 0) {
        cmd_->set_push_constants(pending_push_constants_.data(),
                                 pending_push_constants_size_);
        push_constants_dirty_ = false;
    }
    return true;
}

void RenderContext2::flush_resource_set() {
    if (!bindings_dirty_) return;

    if (!any_dirty_binding_scope()) {
        clear_dirty_binding_scopes();
        return;
    }

    // Defer-destroy the previous set: the command buffer we're still
    // recording into may reference it, and Vulkan forbids mutating
    // bindings that a live cmd buf holds. Drained in end_frame() after
    // submit + fence wait. On OpenGL `destroy(ResourceSetHandle)` is
    // cheap and this deferment is harmless.
    if (current_resource_set_) {
        deferred_destroy_resource_sets_.push_back(current_resource_set_);
        current_resource_set_ = {};
    }

    // Pass the current pipeline's resource layout token so the backend binds
    // values against the right native layout. Vulkan currently maps this token
    // to a VkDescriptorSetLayout; OpenGL and D3D11 use pipeline-local tokens.
    uintptr_t resource_layout_token = 0;
    if (last_bound_pipeline_) {
        resource_layout_token =
            device_.pipeline_resource_layout_token(last_bound_pipeline_);
    }

    BoundResourceSetDesc bound_desc =
        build_pending_bound_resource_set(resource_layout_token);
    if (bound_desc.resource_layout_token != 0) {
        current_resource_set_ = device_.create_bound_resource_set(bound_desc);
        if (current_resource_set_) {
            cmd_->bind_resource_set(current_resource_set_, 0);
        }
    } else {
        const size_t pending_binding_count = bound_resource_binding_count(bound_desc);
        if (pending_binding_count != 0) {
            tc_log(TC_LOG_WARN,
                   "RenderContext2: flush_resource_set skipping pipeline=%u "
                   "(resource_layout_token is null) with %zu pending bindings",
                   last_bound_pipeline_.id, pending_binding_count);
            clear_pending_binding_buckets();
        }
    }

    clear_dirty_binding_scopes();
}

// ============================================================================
// Drawing
// ============================================================================

void RenderContext2::ensure_fsq_resources() {
    if (fsq_vbo_) return;

    // Create VBO. Vertices are canonical TerminClip; backend-native Y
    // conversion happens in termin-engine-fsq.vert.slang.
    BufferDesc vbo_desc;
    vbo_desc.size = static_cast<uint64_t>(sizeof(FSQ_VERTICES));
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
    std::vector<uint8_t> shader_artifact;
    if (!termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
            fsq_shader,
            device_.backend_type(),
            shader_artifact)) {
        tc_log(TC_LOG_ERROR,
               "RenderContext2: failed to load fullscreen quad shader artifact for backend=%s",
               render_context_backend_name(device_.backend_type()));
        return;
    }
    if (device_.backend_type() == BackendType::OpenGL) {
        vs_desc.source.assign(
            reinterpret_cast<const char*>(shader_artifact.data()),
            shader_artifact.size());
    } else {
        vs_desc.bytecode = std::move(shader_artifact);
    }
    fsq_vs_ = device_.create_shader(vs_desc);
}

ShaderHandle RenderContext2::fsq_vertex_shader() {
    ensure_fsq_resources();
    return fsq_vs_;
}

void RenderContext2::draw_fullscreen_quad() {
    ensure_fsq_resources();
    if (!fsq_vs_) {
        tc_log(TC_LOG_ERROR, "RenderContext2: fullscreen quad vertex shader is not available");
        return;
    }

    set_vertex_layout(fullscreen_quad_vertex_layout_desc());
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

    if (!flush_pipeline()) return;
    flush_resource_set();

    cmd_->bind_vertex_buffer(0, fsq_vbo_);
    cmd_->bind_index_buffer(fsq_ibo_, IndexType::Uint32);
    cmd_->draw_indexed(6);
}

void RenderContext2::draw_fullscreen_quad_with_bound_shader() {
    ensure_fsq_resources();

    set_vertex_layout(fullscreen_quad_vertex_layout_desc());
    set_topology(PrimitiveTopology::TriangleList);

    if (!flush_pipeline()) return;
    flush_resource_set();

    cmd_->bind_vertex_buffer(0, fsq_vbo_);
    cmd_->bind_index_buffer(fsq_ibo_, IndexType::Uint32);
    cmd_->draw_indexed(6);
}

void RenderContext2::draw(
    BufferHandle vbo, BufferHandle ibo,
    uint32_t index_count, IndexType idx_type
) {
    if (!flush_pipeline()) return;
    flush_resource_set();
    // Redundant-bind culling: a pipeline-compatible sequence of draws
    // over the same mesh (think: several chronosquad enemies rendered
    // from the same instanced VBO) hits this path hundreds of times.
    // vkCmdBindVertexBuffers / vkCmdBindIndexBuffer each cost a cmd-buffer
    // word + driver trampoline — redundant ones add up to a measurable
    // slice of cmd-recording time.
    ensure_cached_vertex_buffer_slots(1);
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

void RenderContext2::draw(
    BufferHandle vbo,
    BufferHandle ibo,
    uint64_t index_offset,
    uint32_t index_count,
    int32_t vertex_offset,
    IndexType idx_type
) {
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(1);
    if (vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != 0) {
        cmd_->bind_vertex_buffer(0, vbo);
        last_bound_vbos_[0] = vbo;
        last_bound_vbo_offsets_[0] = 0;
    }
    if (ibo != last_bound_ibo_
        || last_bound_ibo_offset_ != index_offset
        || idx_type != last_bound_index_type_) {
        cmd_->bind_index_buffer(ibo, idx_type, index_offset);
        last_bound_ibo_ = ibo;
        last_bound_ibo_offset_ = index_offset;
        last_bound_index_type_ = idx_type;
    }
    cmd_->draw_indexed(index_count, 0, vertex_offset);
}

void RenderContext2::draw_indexed_instanced(const IndexedInstancedDraw& draw) {
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(2);
    if (draw.vertex_buffer != last_bound_vbos_[0]
        || draw.vertex_offset != last_bound_vbo_offsets_[0]) {
        cmd_->bind_vertex_buffer(0, draw.vertex_buffer, draw.vertex_offset);
        last_bound_vbos_[0] = draw.vertex_buffer;
        last_bound_vbo_offsets_[0] = draw.vertex_offset;
    }
    if (draw.instance_buffer && (draw.instance_buffer != last_bound_vbos_[1]
        || draw.instance_offset != last_bound_vbo_offsets_[1])) {
        cmd_->bind_vertex_buffer(1, draw.instance_buffer, draw.instance_offset);
        last_bound_vbos_[1] = draw.instance_buffer;
        last_bound_vbo_offsets_[1] = draw.instance_offset;
    }
    if (draw.index_buffer != last_bound_ibo_
        || draw.index_offset != last_bound_ibo_offset_
        || draw.index_type != last_bound_index_type_) {
        cmd_->bind_index_buffer(draw.index_buffer, draw.index_type, draw.index_offset);
        last_bound_ibo_ = draw.index_buffer;
        last_bound_ibo_offset_ = draw.index_offset;
        last_bound_index_type_ = draw.index_type;
    }
    cmd_->draw_indexed_instanced(draw.index_count, draw.instance_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo, uint32_t vertex_count) {
    draw_arrays(vbo, 0, vertex_count);
}

void RenderContext2::draw_arrays(BufferHandle vbo,
                                 uint64_t vertex_offset,
                                 uint32_t vertex_count) {
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(1);
    if (vbo != last_bound_vbos_[0] || last_bound_vbo_offsets_[0] != vertex_offset) {
        cmd_->bind_vertex_buffer(0, vbo, vertex_offset);
        last_bound_vbos_[0] = vbo;
        last_bound_vbo_offsets_[0] = vertex_offset;
    }
    cmd_->draw(vertex_count);
}

void RenderContext2::draw_arrays_instanced(BufferHandle vbo,
                                           uint32_t vertex_count,
                                           uint32_t instance_count) {
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(1);
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
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(2);
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
    if (!flush_pipeline()) return;
    flush_resource_set();
    ensure_cached_vertex_buffer_slots(2);
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
// the backend doesn't provide a ring. This keeps small renderer-owned streams
// off the per-draw allocation path on both OpenGL and Vulkan.
void RenderContext2::draw_transient_arrays(const void* data,
                                           uint32_t byte_size,
                                           uint32_t vertex_count,
                                           const VertexBufferLayout& layout,
                                           PrimitiveTopology topology) {
    draw_transient_arrays(
        data,
        byte_size,
        vertex_count,
        make_vertex_layout_desc(layout),
        topology);
}

void RenderContext2::draw_transient_arrays(const void* data,
                                           uint32_t byte_size,
                                           uint32_t vertex_count,
                                           const VertexLayoutDesc& layout,
                                           PrimitiveTopology topology) {
    if (!data || byte_size == 0 || vertex_count == 0) return;

    const bool profile = tc_profiler_enabled();

    BufferHandle buf;
    uint64_t vb_offset = 0;
    bool used_ring = false;

    if (profile) tc_profiler_begin_section("transient.upload");
    const uint64_t ring_offset = device_.transient_vertex_write(data, byte_size);
    if (ring_offset != UINT64_MAX) {
        buf = device_.transient_vertex_buffer();
        vb_offset = ring_offset;
        used_ring = true;
    } else {
        BufferDesc desc;
        desc.size = byte_size;
        desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
        buf = device_.create_buffer(desc);
        device_.upload_buffer(
            buf,
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(data),
                byte_size));
    }
    if (profile) tc_profiler_end_section();

    if (profile) tc_profiler_begin_section("transient.draw");
    set_vertex_layout(layout);
    set_topology(topology);
    draw_arrays(buf, vb_offset, vertex_count);
    if (profile) tc_profiler_end_section();

    // Ring buffers are process-lifetime; only the fallback path queues
    // the buffer for destruction after the current frame's submit
    // (Vulkan needs the VkBuffer alive until the queue drains).
    if (!used_ring) {
        deferred_destroy_buffers_.push_back(buf);
    }
}

void RenderContext2::draw_immediate_generic(const float* data,
                                            uint32_t vertex_count,
                                            PrimitiveTopology topo) {
    VertexLayoutDesc layout;
    layout.stride = 7 * sizeof(float);
    layout.use_shader_input_locations = true;
    layout.attribute_count = 2;
    layout.attributes[0] = {0, VertexFormat::Float3, 0, nullptr};
    layout.attributes[1] = {1, VertexFormat::Float4, 3 * sizeof(float), nullptr};
    draw_transient_arrays(
        data,
        vertex_count * 7 * static_cast<uint32_t>(sizeof(float)),
        vertex_count,
        layout,
        topo);
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
