#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

#include "tgfx2/descriptors.hpp"

extern "C" {
struct tc_shader_resource_binding;
}

namespace tgfx {

enum class ShaderResourceKind : uint32_t {
    None = 0,
    ConstantBuffer,
    Texture,
    Sampler,
    StorageBuffer,
    StorageTexture,
};

enum class ShaderResourceScope : uint32_t {
    Unknown = 0,
    Frame,
    Pass,
    Material,
    Draw,
    Transient,
    Unscoped,
};

struct ShaderResourceKey {
    std::string name;
    ShaderResourceKind kind = ShaderResourceKind::None;
    ShaderResourceScope scope = ShaderResourceScope::Unknown;
};

enum class BackendPlacementKind : uint32_t {
    None = 0,
    VulkanDescriptor,
    D3D11Register,
    OpenGLBinding,
};

enum class BackendDescriptorKind : uint32_t {
    None = 0,
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    Sampler,
    StorageTexture,
};

enum class D3D11RegisterClass : uint32_t {
    None = 0,
    B,
    T,
    S,
    U,
};

enum class OpenGLBindingClass : uint32_t {
    None = 0,
    UniformBuffer,
    StorageBuffer,
    TextureUnit,
    SamplerUnit,
    ImageUnit,
};

struct VulkanDescriptorPlacement {
    uint32_t set = 0;
    uint32_t binding = 0;
    BackendDescriptorKind descriptor_kind = BackendDescriptorKind::None;
};

struct D3D11RegisterPlacement {
    D3D11RegisterClass register_class = D3D11RegisterClass::None;
    uint32_t register_index = 0;
    bool scalar_sampler_for_texture_array = false;
};

struct OpenGLBindingPlacement {
    OpenGLBindingClass binding_class = OpenGLBindingClass::None;
    uint32_t binding_point = 0;
    uint32_t texture_unit = 0;
};

struct BackendPlacement {
    BackendPlacementKind kind = BackendPlacementKind::None;
    VulkanDescriptorPlacement vulkan;
    D3D11RegisterPlacement d3d11;
    OpenGLBindingPlacement opengl;
};

struct BackendBindingPlanEntry {
    ShaderResourceKey resource;
    uint32_t stage_mask = 0;
    uint32_t array_count = 1;
    uint32_t size = 0;
    BackendPlacement placement;
};

struct BackendBindingPlan {
    BackendType backend = BackendType::Null;
    std::vector<BackendBindingPlanEntry> entries;
};

struct BackendBindingRange {
    uint32_t base = 0;
    uint32_t size = 0;
};

enum class BackendBindingConflictClass : uint32_t {
    None = 0,
    Descriptor,
    ConstantBuffer,
    StorageBuffer,
    Texture,
    Sampler,
    StorageTexture,
};

TGFX2_API bool shader_resource_kind_is_constant_buffer(ShaderResourceKind kind);
TGFX2_API bool shader_resource_kind_is_texture_like(ShaderResourceKind kind);
TGFX2_API bool shader_resource_scope_has_transitional_binding_range(
    ShaderResourceScope scope);
TGFX2_API uint32_t stable_shader_resource_name_hash(std::string_view value);
TGFX2_API BackendBindingRange transitional_backend_binding_range(
    BackendType backend,
    ShaderResourceKind kind,
    ShaderResourceScope scope);
TGFX2_API BackendBindingConflictClass backend_binding_conflict_class(
    BackendType backend,
    ShaderResourceKind kind);

enum class BoundResourceKind : uint32_t {
    UniformBuffer = 0,
    StorageBuffer,
    SampledTexture,
    Sampler,
};

struct BoundResourceValue {
    BoundResourceKind kind = BoundResourceKind::UniformBuffer;
    BufferHandle buffer;
    TextureHandle texture;
    SamplerHandle sampler;
    uint64_t offset = 0;
    uint64_t range = 0;
    uint32_t array_element = 0;
};

struct BackendBoundResourceSlot {
    ShaderResourceKind kind = ShaderResourceKind::None;
    ShaderResourceScope scope = ShaderResourceScope::Unknown;
    uint32_t stage_mask = 0;
    uint32_t array_count = 1;
    uint32_t size = 0;
    BackendPlacement placement;
    const char* debug_name = nullptr;
};

struct BoundResourceBinding {
    BackendBoundResourceSlot slot;
    BoundResourceValue value;
};

static_assert(std::is_standard_layout_v<BoundResourceValue>);
static_assert(std::is_trivially_copyable_v<BoundResourceValue>);
static_assert(std::is_standard_layout_v<BackendBoundResourceSlot>);
static_assert(std::is_trivially_copyable_v<BackendBoundResourceSlot>);
static_assert(std::is_standard_layout_v<BoundResourceBinding>);
static_assert(std::is_trivially_copyable_v<BoundResourceBinding>);

inline const char* bound_resource_debug_name(const BoundResourceBinding& binding) {
    return binding.slot.debug_name ? binding.slot.debug_name : "<unnamed>";
}

// Flat, non-owning packet that crosses the render-runtime/device boundary.
// The backing bindings are retained by RenderContext2 or a backend resource set.
struct BoundResourceGroupView {
    ShaderResourceScope scope = ShaderResourceScope::Unknown;
    // True when this scope changed since the last emitted resource set for the
    // current pass/pipeline. Backends that retain native binding state
    // (OpenGL/D3D11) can skip clean groups; descriptor-set backends may still
    // consume all groups to build a complete backend resource object.
    bool dirty = true;
    const BoundResourceBinding* bindings = nullptr;
    uint32_t binding_count = 0;
};

struct BoundResourceSetDesc {
    uintptr_t resource_layout_token = 0;
    // Groups preserve the shader resource scope at the backend boundary so
    // frame/pass/material/draw bindings do not need inferred names or slots.
    const BoundResourceGroupView* groups = nullptr;
    uint32_t group_count = 0;
};

static_assert(std::is_standard_layout_v<BoundResourceGroupView>);
static_assert(std::is_trivially_copyable_v<BoundResourceGroupView>);
static_assert(std::is_standard_layout_v<BoundResourceSetDesc>);
static_assert(std::is_trivially_copyable_v<BoundResourceSetDesc>);

// Backend-local owning storage for packets consumed after create_bound_resource_set
// returns. It deliberately stays behind the device boundary; only view packets
// above are exposed to IRenderDevice.
class TGFX2_API BoundResourceSetStorage {
private:
    struct GroupStorage {
        ShaderResourceScope scope = ShaderResourceScope::Unknown;
        bool dirty = true;
        std::vector<BoundResourceBinding> bindings;
    };

    uintptr_t resource_layout_token_ = 0;
    std::vector<GroupStorage> groups_;
    std::vector<BoundResourceGroupView> group_views_;

    void rebuild_views();

public:
    void set_resource_layout_token(uintptr_t resource_layout_token) {
        resource_layout_token_ = resource_layout_token;
    }
    void append_group(
        ShaderResourceScope scope,
        bool dirty,
        const BoundResourceBinding* bindings,
        uint32_t binding_count);
    void assign(const BoundResourceSetDesc& desc);
    BoundResourceSetDesc view() const;
};

template <typename Fn>
void for_each_bound_resource_binding(const BoundResourceSetDesc& desc, Fn&& fn) {
    for (uint32_t group_index = 0; group_index < desc.group_count; ++group_index) {
        const BoundResourceGroupView& group = desc.groups[group_index];
        for (uint32_t binding_index = 0;
             binding_index < group.binding_count;
             ++binding_index) {
            fn(group.bindings[binding_index]);
        }
    }
}

template <typename Fn>
void for_each_dirty_bound_resource_binding(const BoundResourceSetDesc& desc, Fn&& fn) {
    for (uint32_t group_index = 0; group_index < desc.group_count; ++group_index) {
        const BoundResourceGroupView& group = desc.groups[group_index];
        if (!group.dirty) {
            continue;
        }
        for (uint32_t binding_index = 0;
             binding_index < group.binding_count;
             ++binding_index) {
            fn(group.bindings[binding_index]);
        }
    }
}

TGFX2_API size_t bound_resource_binding_count(const BoundResourceSetDesc& desc);
TGFX2_API size_t dirty_bound_resource_binding_count(const BoundResourceSetDesc& desc);

TGFX2_API bool build_backend_binding_plan(
    BackendType backend,
    const tc_shader_resource_binding* bindings,
    uint32_t binding_count,
    BackendBindingPlan& out_plan,
    std::string* error = nullptr);

TGFX2_API BackendBoundResourceSlot bound_resource_slot_from_plan_entry(
    const BackendBindingPlanEntry& entry);

} // namespace tgfx
