#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_type_conversions.hpp"

namespace tgfx::vk {

VkFormat to_vk_format(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::R8_UNorm:          return VK_FORMAT_R8_UNORM;
        case PixelFormat::RG8_UNorm:         return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::RGB8_UNorm:        return VK_FORMAT_R8G8B8_UNORM;
        case PixelFormat::RGBA8_UNorm:       return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::BGRA8_UNorm:       return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::R16F:              return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::RG16F:             return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::RGBA16F:           return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R32F:              return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::RG32F:             return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::RGBA32F:           return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::D32F:              return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::D24_UNorm:         return VK_FORMAT_X8_D24_UNORM_PACK32;
        case PixelFormat::Undefined:         return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

VkCompareOp to_vk_compare(CompareOp op) {
    switch (op) {
        case CompareOp::Never:        return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:         return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkBlendFactor to_vk_blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:              return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:               return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcAlpha:          return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:          return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcColor:          return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:          return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:  return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp to_vk_blend_op(BlendOp op) {
    switch (op) {
        case BlendOp::Add:             return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:             return VK_BLEND_OP_MIN;
        case BlendOp::Max:             return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkCullModeFlags to_vk_cull_mode(CullMode mode) {
    switch (mode) {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_BACK_BIT;
}

VkFrontFace to_vk_front_face(FrontFace face) {
    switch (face) {
        case FrontFace::CCW: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case FrontFace::CW:  return VK_FRONT_FACE_CLOCKWISE;
    }
    return VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkPolygonMode to_vk_polygon_mode(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
        case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
        case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_FILL;
}

VkPrimitiveTopology to_vk_topology(PrimitiveTopology topo) {
    switch (topo) {
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopology::LineList:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LineStrip:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::TriangleList:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkFilter to_vk_filter(FilterMode mode) {
    switch (mode) {
        case FilterMode::Nearest: return VK_FILTER_NEAREST;
        case FilterMode::Linear:  return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

VkSamplerMipmapMode to_vk_mipmap_mode(FilterMode mode) {
    switch (mode) {
        case FilterMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case FilterMode::Linear:  return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkSamplerAddressMode to_vk_address_mode(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkIndexType to_vk_index_type(IndexType type) {
    switch (type) {
        case IndexType::Uint16: return VK_INDEX_TYPE_UINT16;
        case IndexType::Uint32: return VK_INDEX_TYPE_UINT32;
    }
    return VK_INDEX_TYPE_UINT32;
}

VkShaderStageFlagBits to_vk_shader_stage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

VkFormat to_vk_vertex_format(VertexFormat fmt) {
    switch (fmt) {
        case VertexFormat::Float:   return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:  return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:  return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UByte4:  return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

VkBufferUsageFlags to_vk_buffer_usage(BufferUsage usage) {
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT; // always allow upload
    if (has_flag(usage, BufferUsage::Vertex))  flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (has_flag(usage, BufferUsage::Index))   flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (has_flag(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has_flag(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (has_flag(usage, BufferUsage::CopySrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (has_flag(usage, BufferUsage::CopyDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageUsageFlags to_vk_image_usage(TextureUsage usage) {
    VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT; // always allow upload
    if (has_flag(usage, TextureUsage::Sampled))              flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (has_flag(usage, TextureUsage::Storage))              flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (has_flag(usage, TextureUsage::ColorAttachment))      flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (has_flag(usage, TextureUsage::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (has_flag(usage, TextureUsage::CopySrc))              flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (has_flag(usage, TextureUsage::CopyDst))              flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageAspectFlags format_aspect_flags(PixelFormat fmt) {
    if (is_depth_format(fmt)) return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

bool is_depth_format(PixelFormat fmt) {
    return fmt == PixelFormat::D24_UNorm_S8_UInt
        || fmt == PixelFormat::D24_UNorm
        || fmt == PixelFormat::D32F;
}

} // namespace tgfx::vk

#endif // TGFX2_HAS_VULKAN
