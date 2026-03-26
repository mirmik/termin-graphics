#include "tgfx2/opengl/opengl_type_conversions.hpp"

namespace tgfx2::gl {

GLFormatInfo to_gl_format(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::R8_UNorm:           return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case PixelFormat::RG8_UNorm:          return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
        case PixelFormat::RGB8_UNorm:         return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
        case PixelFormat::RGBA8_UNorm:        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case PixelFormat::BGRA8_UNorm:        return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE};
        case PixelFormat::R16F:               return {GL_R16F, GL_RED, GL_HALF_FLOAT};
        case PixelFormat::RG16F:              return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
        case PixelFormat::RGBA16F:            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case PixelFormat::R32F:               return {GL_R32F, GL_RED, GL_FLOAT};
        case PixelFormat::RG32F:              return {GL_RG32F, GL_RG, GL_FLOAT};
        case PixelFormat::RGBA32F:            return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
        case PixelFormat::D24_UNorm_S8_UInt:  return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
        case PixelFormat::D32F:               return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

GLenum to_gl_compare(CompareOp op) {
    switch (op) {
        case CompareOp::Never:        return GL_NEVER;
        case CompareOp::Less:         return GL_LESS;
        case CompareOp::Equal:        return GL_EQUAL;
        case CompareOp::LessEqual:    return GL_LEQUAL;
        case CompareOp::Greater:      return GL_GREATER;
        case CompareOp::NotEqual:     return GL_NOTEQUAL;
        case CompareOp::GreaterEqual: return GL_GEQUAL;
        case CompareOp::Always:       return GL_ALWAYS;
    }
    return GL_LESS;
}

GLenum to_gl_blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:              return GL_ZERO;
        case BlendFactor::One:               return GL_ONE;
        case BlendFactor::SrcAlpha:          return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:  return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:          return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:  return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcColor:          return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:  return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:          return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:  return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

GLenum to_gl_blend_op(BlendOp op) {
    switch (op) {
        case BlendOp::Add:             return GL_FUNC_ADD;
        case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:             return GL_MIN;
        case BlendOp::Max:             return GL_MAX;
    }
    return GL_FUNC_ADD;
}

GLenum to_gl_cull_mode(CullMode mode) {
    switch (mode) {
        case CullMode::None:  return GL_NONE;
        case CullMode::Front: return GL_FRONT;
        case CullMode::Back:  return GL_BACK;
    }
    return GL_BACK;
}

GLenum to_gl_front_face(FrontFace face) {
    switch (face) {
        case FrontFace::CCW: return GL_CCW;
        case FrontFace::CW:  return GL_CW;
    }
    return GL_CCW;
}

GLenum to_gl_polygon_mode(PolygonMode mode) {
    switch (mode) {
        case PolygonMode::Fill:  return GL_FILL;
        case PolygonMode::Line:  return GL_LINE;
        case PolygonMode::Point: return GL_POINT;
    }
    return GL_FILL;
}

GLenum to_gl_topology(PrimitiveTopology topo) {
    switch (topo) {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:       return GL_LINES;
        case PrimitiveTopology::LineStrip:      return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList:   return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip:  return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

GLenum to_gl_filter(FilterMode mode) {
    switch (mode) {
        case FilterMode::Nearest: return GL_NEAREST;
        case FilterMode::Linear:  return GL_LINEAR;
    }
    return GL_LINEAR;
}

GLenum to_gl_min_filter(FilterMode min, FilterMode mip) {
    if (min == FilterMode::Nearest && mip == FilterMode::Nearest) return GL_NEAREST_MIPMAP_NEAREST;
    if (min == FilterMode::Linear  && mip == FilterMode::Nearest) return GL_LINEAR_MIPMAP_NEAREST;
    if (min == FilterMode::Nearest && mip == FilterMode::Linear)  return GL_NEAREST_MIPMAP_LINEAR;
    return GL_LINEAR_MIPMAP_LINEAR;
}

GLenum to_gl_address_mode(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat:         return GL_REPEAT;
        case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:    return GL_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:  return GL_CLAMP_TO_BORDER;
    }
    return GL_REPEAT;
}

GLenum to_gl_index_type(IndexType type) {
    switch (type) {
        case IndexType::Uint16: return GL_UNSIGNED_SHORT;
        case IndexType::Uint32: return GL_UNSIGNED_INT;
    }
    return GL_UNSIGNED_INT;
}

GLenum to_gl_shader_stage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:   return GL_VERTEX_SHADER;
        case ShaderStage::Fragment: return GL_FRAGMENT_SHADER;
        case ShaderStage::Geometry: return GL_GEOMETRY_SHADER;
        case ShaderStage::Compute:  return GL_VERTEX_SHADER; // Compute not available in GL 3.3
    }
    return GL_VERTEX_SHADER;
}

int vertex_format_component_count(VertexFormat fmt) {
    switch (fmt) {
        case VertexFormat::Float:   return 1;
        case VertexFormat::Float2:  return 2;
        case VertexFormat::Float3:  return 3;
        case VertexFormat::Float4:  return 4;
        case VertexFormat::UByte4:  return 4;
        case VertexFormat::UByte4N: return 4;
    }
    return 4;
}

GLenum vertex_format_gl_type(VertexFormat fmt) {
    switch (fmt) {
        case VertexFormat::Float:
        case VertexFormat::Float2:
        case VertexFormat::Float3:
        case VertexFormat::Float4:  return GL_FLOAT;
        case VertexFormat::UByte4:
        case VertexFormat::UByte4N: return GL_UNSIGNED_BYTE;
    }
    return GL_FLOAT;
}

bool vertex_format_is_integer(VertexFormat fmt) {
    return fmt == VertexFormat::UByte4;
}

bool vertex_format_is_normalized(VertexFormat fmt) {
    return fmt == VertexFormat::UByte4N;
}

GLenum to_gl_buffer_target(BufferUsage usage) {
    if (has_flag(usage, BufferUsage::Uniform)) return GL_UNIFORM_BUFFER;
    // GL_SHADER_STORAGE_BUFFER (0x90D2) requires GL 4.3
    if (has_flag(usage, BufferUsage::Storage)) return 0x90D2;
    if (has_flag(usage, BufferUsage::Index))   return GL_ELEMENT_ARRAY_BUFFER;
    if (has_flag(usage, BufferUsage::Vertex))  return GL_ARRAY_BUFFER;
    return GL_ARRAY_BUFFER;
}

GLenum to_gl_buffer_usage(bool cpu_visible) {
    return cpu_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
}

} // namespace tgfx2::gl
