#pragma once

#include <glad/glad.h>
#include "tgfx2/enums.hpp"
#include "tgfx2/vertex_layout.hpp"

namespace tgfx::gl {

// Pixel format -> GL internal format, data format, data type
struct GLFormatInfo {
    GLenum internal_format;
    GLenum format;
    GLenum type;
};

GLFormatInfo to_gl_format(PixelFormat fmt);

GLenum to_gl_compare(CompareOp op);
GLenum to_gl_blend_factor(BlendFactor f);
GLenum to_gl_blend_op(BlendOp op);
GLenum to_gl_cull_mode(CullMode mode);
GLenum to_gl_front_face(FrontFace face);
GLenum to_gl_polygon_mode(PolygonMode mode);
GLenum to_gl_topology(PrimitiveTopology topo);
GLenum to_gl_filter(FilterMode mode);
GLenum to_gl_min_filter(FilterMode min, FilterMode mip);
GLenum to_gl_address_mode(AddressMode mode);
GLenum to_gl_index_type(IndexType type);
GLenum to_gl_shader_stage(ShaderStage stage);

// Vertex format helpers
int vertex_format_component_count(VertexFormat fmt);
GLenum vertex_format_gl_type(VertexFormat fmt);
bool vertex_format_is_integer(VertexFormat fmt);
bool vertex_format_is_normalized(VertexFormat fmt);

GLenum to_gl_buffer_target(BufferUsage usage);
GLenum to_gl_buffer_usage(bool cpu_visible);

} // namespace tgfx::gl
