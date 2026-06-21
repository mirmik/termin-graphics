#include "tgfx2/backend_binding_plan.hpp"

#include <cstdio>

extern "C" {
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {
namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

std::string resource_label(const tc_shader_resource_binding& binding) {
    if (binding.name[0] != '\0') {
        return std::string("'") + binding.name + "'";
    }
    return "<unnamed>";
}

ShaderResourceKind shader_resource_kind(uint32_t kind) {
    switch (kind) {
        case TC_SHADER_RESOURCE_CONSTANT_BUFFER:
            return ShaderResourceKind::ConstantBuffer;
        case TC_SHADER_RESOURCE_TEXTURE:
            return ShaderResourceKind::Texture;
        case TC_SHADER_RESOURCE_SAMPLER:
            return ShaderResourceKind::Sampler;
        case TC_SHADER_RESOURCE_STORAGE_BUFFER:
            return ShaderResourceKind::StorageBuffer;
        case TC_SHADER_RESOURCE_STORAGE_TEXTURE:
            return ShaderResourceKind::StorageTexture;
        case TC_SHADER_RESOURCE_NONE:
        default:
            return ShaderResourceKind::None;
    }
}

ShaderResourceScope shader_resource_scope(uint32_t scope) {
    switch (scope) {
        case TC_SHADER_RESOURCE_SCOPE_FRAME:
            return ShaderResourceScope::Frame;
        case TC_SHADER_RESOURCE_SCOPE_PASS:
            return ShaderResourceScope::Pass;
        case TC_SHADER_RESOURCE_SCOPE_MATERIAL:
            return ShaderResourceScope::Material;
        case TC_SHADER_RESOURCE_SCOPE_DRAW:
            return ShaderResourceScope::Draw;
        case TC_SHADER_RESOURCE_SCOPE_TRANSIENT:
            return ShaderResourceScope::Transient;
        case TC_SHADER_RESOURCE_SCOPE_UNSCOPED:
            return ShaderResourceScope::Unscoped;
        case TC_SHADER_RESOURCE_SCOPE_UNKNOWN:
        default:
            return ShaderResourceScope::Unknown;
    }
}

BackendDescriptorKind descriptor_kind_for_resource(ShaderResourceKind kind) {
    switch (kind) {
        case ShaderResourceKind::ConstantBuffer:
            return BackendDescriptorKind::UniformBuffer;
        case ShaderResourceKind::Texture:
            return BackendDescriptorKind::SampledTexture;
        case ShaderResourceKind::Sampler:
            return BackendDescriptorKind::Sampler;
        case ShaderResourceKind::StorageBuffer:
            return BackendDescriptorKind::StorageBuffer;
        case ShaderResourceKind::StorageTexture:
            return BackendDescriptorKind::StorageTexture;
        case ShaderResourceKind::None:
        default:
            return BackendDescriptorKind::None;
    }
}

D3D11RegisterClass d3d11_register_class_from_shader(uint32_t register_class) {
    switch (register_class) {
        case TC_SHADER_D3D11_REGISTER_B:
            return D3D11RegisterClass::B;
        case TC_SHADER_D3D11_REGISTER_T:
            return D3D11RegisterClass::T;
        case TC_SHADER_D3D11_REGISTER_S:
            return D3D11RegisterClass::S;
        case TC_SHADER_D3D11_REGISTER_U:
            return D3D11RegisterClass::U;
        case TC_SHADER_D3D11_REGISTER_NONE:
        default:
            return D3D11RegisterClass::None;
    }
}

D3D11RegisterClass expected_d3d11_register_class(ShaderResourceKind kind) {
    switch (kind) {
        case ShaderResourceKind::ConstantBuffer:
            return D3D11RegisterClass::B;
        case ShaderResourceKind::Texture:
            return D3D11RegisterClass::T;
        case ShaderResourceKind::Sampler:
            return D3D11RegisterClass::S;
        case ShaderResourceKind::StorageBuffer:
        case ShaderResourceKind::StorageTexture:
            return D3D11RegisterClass::U;
        case ShaderResourceKind::None:
        default:
            return D3D11RegisterClass::None;
    }
}

OpenGLBindingClass opengl_binding_class_for_resource(ShaderResourceKind kind) {
    switch (kind) {
        case ShaderResourceKind::ConstantBuffer:
            return OpenGLBindingClass::UniformBuffer;
        case ShaderResourceKind::StorageBuffer:
            return OpenGLBindingClass::StorageBuffer;
        case ShaderResourceKind::Texture:
            return OpenGLBindingClass::TextureUnit;
        case ShaderResourceKind::Sampler:
            return OpenGLBindingClass::SamplerUnit;
        case ShaderResourceKind::StorageTexture:
            return OpenGLBindingClass::ImageUnit;
        case ShaderResourceKind::None:
        default:
            return OpenGLBindingClass::None;
    }
}

bool validate_resource_binding(
    const tc_shader_resource_binding& binding,
    ShaderResourceKind kind,
    std::string* error
) {
    if (binding.name[0] == '\0') {
        set_error(error, "shader resource binding has empty name");
        return false;
    }
    if (kind == ShaderResourceKind::None) {
        set_error(error, "shader resource " + resource_label(binding) + " has invalid kind");
        return false;
    }
    if (binding.stage_mask == TC_SHADER_STAGE_NONE) {
        set_error(error, "shader resource " + resource_label(binding) + " has empty stage mask");
        return false;
    }
    return true;
}

bool build_vulkan_entry(
    const tc_shader_resource_binding& binding,
    BackendBindingPlanEntry& entry,
    std::string* error
) {
    if (binding.set != 0) {
        set_error(
            error,
            "shader resource " + resource_label(binding) +
            " uses Vulkan descriptor set " + std::to_string(binding.set) +
            ", but tgfx2 currently supports set 0 only");
        return false;
    }
    entry.placement.kind = BackendPlacementKind::VulkanDescriptor;
    entry.placement.vulkan.set = binding.set;
    entry.placement.vulkan.binding = binding.binding;
    entry.placement.vulkan.descriptor_kind =
        descriptor_kind_for_resource(entry.resource.kind);
    return true;
}

bool build_d3d11_entry(
    const tc_shader_resource_binding& binding,
    BackendBindingPlanEntry& entry,
    std::string* error
) {
    if (!binding.has_d3d11_placement) {
        set_error(
            error,
            "shader resource " + resource_label(binding) +
            " is missing D3D11 placement");
        return false;
    }
    const D3D11RegisterClass register_class =
        d3d11_register_class_from_shader(binding.d3d11.register_class);
    const D3D11RegisterClass expected =
        expected_d3d11_register_class(entry.resource.kind);
    if (register_class == D3D11RegisterClass::None || register_class != expected) {
        set_error(
            error,
            "shader resource " + resource_label(binding) +
            " has incompatible D3D11 register class");
        return false;
    }
    entry.placement.kind = BackendPlacementKind::D3D11Register;
    entry.placement.d3d11.register_class = register_class;
    entry.placement.d3d11.register_index = binding.d3d11.register_index;
    return true;
}

bool build_opengl_entry(
    const tc_shader_resource_binding& binding,
    BackendBindingPlanEntry& entry,
    std::string* error
) {
    const OpenGLBindingClass binding_class =
        opengl_binding_class_for_resource(entry.resource.kind);
    if (binding_class == OpenGLBindingClass::None) {
        set_error(
            error,
            "shader resource " + resource_label(binding) +
            " has no OpenGL binding class");
        return false;
    }
    entry.placement.kind = BackendPlacementKind::OpenGLBinding;
    entry.placement.opengl.binding_class = binding_class;
    if (binding_class == OpenGLBindingClass::TextureUnit ||
        binding_class == OpenGLBindingClass::SamplerUnit) {
        entry.placement.opengl.texture_unit = binding.binding;
    } else {
        entry.placement.opengl.binding_point = binding.binding;
    }
    return true;
}

bool d3d11_conflicts(
    const BackendBindingPlanEntry& a,
    const BackendBindingPlanEntry& b
) {
    if (a.placement.kind != BackendPlacementKind::D3D11Register ||
        b.placement.kind != BackendPlacementKind::D3D11Register) {
        return false;
    }
    return a.placement.d3d11.register_class == b.placement.d3d11.register_class &&
        a.placement.d3d11.register_index == b.placement.d3d11.register_index &&
        (a.stage_mask & b.stage_mask) != 0;
}

bool validate_plan_conflicts(
    const BackendBindingPlan& plan,
    std::string* error
) {
    if (plan.backend != BackendType::D3D11) {
        return true;
    }
    for (size_t i = 0; i < plan.entries.size(); ++i) {
        for (size_t j = i + 1; j < plan.entries.size(); ++j) {
            if (!d3d11_conflicts(plan.entries[i], plan.entries[j])) {
                continue;
            }
            set_error(
                error,
                "D3D11 binding plan conflict between resource '" +
                plan.entries[i].resource.name + "' and '" +
                plan.entries[j].resource.name + "'");
            return false;
        }
    }
    return true;
}

} // namespace

bool build_backend_binding_plan(
    BackendType backend,
    const tc_shader_resource_binding* bindings,
    uint32_t binding_count,
    BackendBindingPlan& out_plan,
    std::string* error
) {
    out_plan = {};
    out_plan.backend = backend;
    if (error) {
        error->clear();
    }
    if (binding_count > 0 && bindings == nullptr) {
        set_error(error, "binding_count is non-zero but bindings is null");
        return false;
    }

    out_plan.entries.reserve(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i) {
        const tc_shader_resource_binding& binding = bindings[i];
        const ShaderResourceKind kind = shader_resource_kind(binding.kind);
        if (!validate_resource_binding(binding, kind, error)) {
            out_plan = {};
            return false;
        }

        BackendBindingPlanEntry entry;
        entry.resource.name = binding.name;
        entry.resource.kind = kind;
        entry.resource.scope = shader_resource_scope(binding.scope);
        entry.stage_mask = binding.stage_mask;
        entry.size = binding.size;

        bool ok = false;
        switch (backend) {
            case BackendType::Vulkan:
                ok = build_vulkan_entry(binding, entry, error);
                break;
            case BackendType::D3D11:
                ok = build_d3d11_entry(binding, entry, error);
                break;
            case BackendType::OpenGL:
                ok = build_opengl_entry(binding, entry, error);
                break;
            case BackendType::Metal:
            case BackendType::Null:
            default:
                set_error(error, "backend has no binding plan builder");
                ok = false;
                break;
        }
        if (!ok) {
            out_plan = {};
            return false;
        }
        out_plan.entries.push_back(std::move(entry));
    }

    if (!validate_plan_conflicts(out_plan, error)) {
        out_plan = {};
        return false;
    }
    return true;
}

} // namespace tgfx
