#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#include "tgfx2/enums.hpp"
#include "tgfx2/vertex_layout.hpp"

namespace tgfx2::vk {

VkFormat to_vk_format(PixelFormat fmt);
VkCompareOp to_vk_compare(CompareOp op);
VkBlendFactor to_vk_blend_factor(BlendFactor f);
VkBlendOp to_vk_blend_op(BlendOp op);
VkCullModeFlags to_vk_cull_mode(CullMode mode);
VkFrontFace to_vk_front_face(FrontFace face);
VkPolygonMode to_vk_polygon_mode(PolygonMode mode);
VkPrimitiveTopology to_vk_topology(PrimitiveTopology topo);
VkFilter to_vk_filter(FilterMode mode);
VkSamplerMipmapMode to_vk_mipmap_mode(FilterMode mode);
VkSamplerAddressMode to_vk_address_mode(AddressMode mode);
VkIndexType to_vk_index_type(IndexType type);
VkShaderStageFlagBits to_vk_shader_stage(ShaderStage stage);

VkFormat to_vk_vertex_format(VertexFormat fmt);

VkBufferUsageFlags to_vk_buffer_usage(BufferUsage usage);
VkImageUsageFlags to_vk_image_usage(TextureUsage usage);

VkImageAspectFlags format_aspect_flags(PixelFormat fmt);
bool is_depth_format(PixelFormat fmt);

} // namespace tgfx2::vk

#endif // TGFX2_HAS_VULKAN
