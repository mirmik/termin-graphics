#pragma once

#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_render_device.hpp"

#include <string>
#include <vector>

namespace tgfx {

struct SpirvVertexInputs {
    bool known = false;
    std::vector<uint32_t> locations;
};

std::string reflect_spirv_stage_entry_point(
    const std::vector<uint32_t>& spirv,
    ShaderStage stage
);

SpirvVertexInputs reflect_spirv_vertex_inputs(
    const std::vector<uint32_t>& spirv,
    const std::string& entry_point
);

std::vector<VkShaderResource::DescriptorBinding> reflect_spirv_descriptor_bindings(
    const std::vector<uint32_t>& spirv
);

bool vertex_shader_uses_location(const VkShaderResource* shader, uint32_t location);

bool vertex_attributes_have_location(
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    uint32_t location
);

std::string join_u32s(const std::vector<uint32_t>& values);

std::string describe_vk_vertex_attributes(
    const std::vector<VkVertexInputAttributeDescription>& attributes
);

std::string describe_vertex_layouts(
    const std::vector<VertexBufferLayout>& layouts
);

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
