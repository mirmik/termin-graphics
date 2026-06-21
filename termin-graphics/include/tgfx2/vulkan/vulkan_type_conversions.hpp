#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/vertex_layout.hpp"

namespace tgfx::vk {

TGFX2_API VkFormat to_vk_format(PixelFormat fmt);
TGFX2_API VkCompareOp to_vk_compare(CompareOp op);
TGFX2_API VkBlendFactor to_vk_blend_factor(BlendFactor f);
TGFX2_API VkBlendOp to_vk_blend_op(BlendOp op);
TGFX2_API VkCullModeFlags to_vk_cull_mode(CullMode mode);
TGFX2_API VkFrontFace to_vk_front_face(FrontFace face);
TGFX2_API VkPolygonMode to_vk_polygon_mode(PolygonMode mode);
TGFX2_API VkPrimitiveTopology to_vk_topology(PrimitiveTopology topo);
TGFX2_API VkFilter to_vk_filter(FilterMode mode);
TGFX2_API VkSamplerMipmapMode to_vk_mipmap_mode(FilterMode mode);
TGFX2_API VkSamplerAddressMode to_vk_address_mode(AddressMode mode);
TGFX2_API VkIndexType to_vk_index_type(IndexType type);
TGFX2_API VkShaderStageFlagBits to_vk_shader_stage(ShaderStage stage);

TGFX2_API VkFormat to_vk_vertex_format(VertexFormat fmt);

TGFX2_API VkBufferUsageFlags to_vk_buffer_usage(BufferUsage usage);
TGFX2_API VkImageUsageFlags to_vk_image_usage(TextureUsage usage);

TGFX2_API VkImageAspectFlags format_aspect_flags(PixelFormat fmt);

} // namespace tgfx::vk

#endif // TGFX2_HAS_VULKAN
