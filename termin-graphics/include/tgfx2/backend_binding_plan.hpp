#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

struct BoundResourceBinding {
    BackendBindingPlanEntry plan_entry;
    BoundResourceValue value;
};

struct BoundResourceGroup {
    ShaderResourceScope scope = ShaderResourceScope::Unknown;
    // True when this scope changed since the last emitted resource set for the
    // current pass/pipeline. Backends that retain native binding state
    // (OpenGL/D3D11) can skip clean groups; descriptor-set backends may still
    // consume all groups to build a complete backend resource object.
    bool dirty = true;
    std::vector<BoundResourceBinding> bindings;
};

struct BoundResourceSetDesc {
    uintptr_t resource_layout_token = 0;
    // Preferred representation for migrated paths. Groups preserve the shader
    // resource scope at the backend boundary so frame/pass/material/draw
    // bindings do not need to be inferred from names or numeric slots.
    std::vector<BoundResourceGroup> groups;
    // Transitional flat compatibility representation. If groups is non-empty,
    // concrete tgfx2 backends and adapters consume groups and ignore this list.
    std::vector<BoundResourceBinding> bindings;
};

template <typename Fn>
void for_each_bound_resource_binding(const BoundResourceSetDesc& desc, Fn&& fn) {
    if (!desc.groups.empty()) {
        for (const BoundResourceGroup& group : desc.groups) {
            for (const BoundResourceBinding& binding : group.bindings) {
                fn(binding);
            }
        }
        return;
    }
    for (const BoundResourceBinding& binding : desc.bindings) {
        fn(binding);
    }
}

template <typename Fn>
void for_each_dirty_bound_resource_binding(const BoundResourceSetDesc& desc, Fn&& fn) {
    if (!desc.groups.empty()) {
        for (const BoundResourceGroup& group : desc.groups) {
            if (!group.dirty) {
                continue;
            }
            for (const BoundResourceBinding& binding : group.bindings) {
                fn(binding);
            }
        }
        return;
    }
    for (const BoundResourceBinding& binding : desc.bindings) {
        fn(binding);
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

TGFX2_API ResourceBinding resource_binding_from_bound(
    const BoundResourceBinding& binding);

// Compatibility adapter for custom/unported backends that still implement only
// create_resource_set(ResourceSetDesc). Concrete tgfx2 backends should override
// create_bound_resource_set() and consume BackendBindingPlanEntry placement
// directly.
TGFX2_API ResourceSetDesc legacy_resource_set_desc_from_bound(
    const BoundResourceSetDesc& bound_desc,
    const std::vector<ResourceBinding>& legacy_numeric_bindings = {});

} // namespace tgfx
