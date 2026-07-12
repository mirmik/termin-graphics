#include "tgfx2/d3d11/d3d11_type_conversions.hpp"

#include <stdexcept>

namespace tgfx::d3d11 {

DXGI_FORMAT to_dxgi_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
        case PixelFormat::RG8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
        case PixelFormat::RGB8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::R16F: return DXGI_FORMAT_R16_FLOAT;
        case PixelFormat::RG16F: return DXGI_FORMAT_R16G16_FLOAT;
        case PixelFormat::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case PixelFormat::R32F: return DXGI_FORMAT_R32_FLOAT;
        case PixelFormat::RG32F: return DXGI_FORMAT_R32G32_FLOAT;
        case PixelFormat::RGBA32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case PixelFormat::D24_UNorm:
        case PixelFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
        case PixelFormat::D32F: return DXGI_FORMAT_R32_TYPELESS;
        case PixelFormat::Undefined: return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT to_dxgi_srv_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::D24_UNorm:
        case PixelFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case PixelFormat::D32F: return DXGI_FORMAT_R32_FLOAT;
        default: return to_dxgi_format(format);
    }
}

DXGI_FORMAT to_dxgi_dsv_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::D24_UNorm:
        case PixelFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::D32F: return DXGI_FORMAT_D32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT to_dxgi_vertex_format(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float: return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::Int: return DXGI_FORMAT_R32_SINT;
        case VertexFormat::Int2: return DXGI_FORMAT_R32G32_SINT;
        case VertexFormat::Int3: return DXGI_FORMAT_R32G32B32_SINT;
        case VertexFormat::Int4: return DXGI_FORMAT_R32G32B32A32_SINT;
        case VertexFormat::UInt: return DXGI_FORMAT_R32_UINT;
        case VertexFormat::UInt2: return DXGI_FORMAT_R32G32_UINT;
        case VertexFormat::UInt3: return DXGI_FORMAT_R32G32B32_UINT;
        case VertexFormat::UInt4: return DXGI_FORMAT_R32G32B32A32_UINT;
        case VertexFormat::Short: return DXGI_FORMAT_R16_SINT;
        case VertexFormat::Short2: return DXGI_FORMAT_R16G16_SINT;
        case VertexFormat::Short4: return DXGI_FORMAT_R16G16B16A16_SINT;
        case VertexFormat::UShort: return DXGI_FORMAT_R16_UINT;
        case VertexFormat::UShort2: return DXGI_FORMAT_R16G16_UINT;
        case VertexFormat::UShort4: return DXGI_FORMAT_R16G16B16A16_UINT;
        case VertexFormat::Byte4: return DXGI_FORMAT_R8G8B8A8_SINT;
        case VertexFormat::UByte4: return DXGI_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UByte4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Short3:
        case VertexFormat::UShort3:
            break;
    }
    throw std::runtime_error("D3D11: unsupported vertex format");
}

DXGI_FORMAT to_dxgi_index_format(IndexType type) {
    return type == IndexType::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

D3D11_PRIMITIVE_TOPOLOGY to_d3d_topology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveTopology::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveTopology::LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PrimitiveTopology::TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }
    return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

D3D11_COMPARISON_FUNC to_d3d_compare(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return D3D11_COMPARISON_NEVER;
        case CompareOp::Less: return D3D11_COMPARISON_LESS;
        case CompareOp::Equal: return D3D11_COMPARISON_EQUAL;
        case CompareOp::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
        case CompareOp::Greater: return D3D11_COMPARISON_GREATER;
        case CompareOp::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
        case CompareOp::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case CompareOp::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_ALWAYS;
}

D3D11_CULL_MODE to_d3d_cull(CullMode mode) {
    switch (mode) {
        case CullMode::None: return D3D11_CULL_NONE;
        case CullMode::Front: return D3D11_CULL_FRONT;
        case CullMode::Back: return D3D11_CULL_BACK;
    }
    return D3D11_CULL_BACK;
}

bool to_d3d_front_counter_clockwise(FrontFace face) {
    // termin_to_native_clip flips Y at the D3D11 shader-output boundary, so
    // native rasterizer winding is opposite to the logical authoring winding.
    return face == FrontFace::CW;
}

D3D11_FILL_MODE to_d3d_fill(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::Fill: return D3D11_FILL_SOLID;
        case PolygonMode::Line: return D3D11_FILL_WIREFRAME;
        case PolygonMode::Point: break;
    }
    throw std::runtime_error("D3D11: point polygon mode is not supported");
}

D3D11_BLEND to_d3d_blend_factor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return D3D11_BLEND_ZERO;
        case BlendFactor::One: return D3D11_BLEND_ONE;
        case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
        case BlendFactor::SrcColor: return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::DstColor: return D3D11_BLEND_DEST_COLOR;
        case BlendFactor::OneMinusDstColor: return D3D11_BLEND_INV_DEST_COLOR;
    }
    return D3D11_BLEND_ONE;
}

D3D11_BLEND_OP to_d3d_blend_op(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return D3D11_BLEND_OP_ADD;
        case BlendOp::Subtract: return D3D11_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min: return D3D11_BLEND_OP_MIN;
        case BlendOp::Max: return D3D11_BLEND_OP_MAX;
    }
    return D3D11_BLEND_OP_ADD;
}

D3D11_FILTER to_d3d_filter(const SamplerDesc& desc) {
    const bool min_linear = desc.min_filter == FilterMode::Linear;
    const bool mag_linear = desc.mag_filter == FilterMode::Linear;
    const bool mip_linear = desc.mip_filter == FilterMode::Linear;
    if (desc.compare_enable) {
        if (min_linear && mag_linear && mip_linear) return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        if (!min_linear && !mag_linear && !mip_linear) return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        return D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    }
    if (min_linear && mag_linear && mip_linear) return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (!min_linear && !mag_linear && !mip_linear) return D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (min_linear && mag_linear && !mip_linear) return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (!min_linear && !mag_linear && mip_linear) return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
}

D3D11_TEXTURE_ADDRESS_MODE to_d3d_address(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat: return D3D11_TEXTURE_ADDRESS_WRAP;
        case AddressMode::MirroredRepeat: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case AddressMode::ClampToEdge: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case AddressMode::ClampToBorder: return D3D11_TEXTURE_ADDRESS_BORDER;
    }
    return D3D11_TEXTURE_ADDRESS_CLAMP;
}

uint32_t pixel_format_bytes(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8_UNorm: return 1;
        case PixelFormat::RG8_UNorm: return 2;
        case PixelFormat::RGB8_UNorm:
        case PixelFormat::RGBA8_UNorm:
        case PixelFormat::BGRA8_UNorm:
        case PixelFormat::R32F:
        case PixelFormat::D24_UNorm:
        case PixelFormat::D24_UNorm_S8_UInt:
        case PixelFormat::D32F:
            return 4;
        case PixelFormat::R16F: return 2;
        case PixelFormat::RG16F: return 4;
        case PixelFormat::RGBA16F: return 8;
        case PixelFormat::RG32F: return 8;
        case PixelFormat::RGBA32F: return 16;
        case PixelFormat::Undefined: return 0;
    }
    return 0;
}

bool is_depth_format(PixelFormat format) {
    return format == PixelFormat::D24_UNorm ||
           format == PixelFormat::D24_UNorm_S8_UInt ||
           format == PixelFormat::D32F;
}

} // namespace tgfx::d3d11
