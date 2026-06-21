#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

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

TGFX2_API bool build_backend_binding_plan(
    BackendType backend,
    const tc_shader_resource_binding* bindings,
    uint32_t binding_count,
    BackendBindingPlan& out_plan,
    std::string* error = nullptr);

} // namespace tgfx
