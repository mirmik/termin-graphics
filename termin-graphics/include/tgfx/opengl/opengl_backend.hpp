#pragma once

#include <tgfx/tgfx_api.h>
#include <glad/glad.h>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "tgfx/tgfx_gpu_ops.h"
#include "tgfx/tc_gpu_context.h"
}

#include <tcbase/tc_log.hpp>
#include "tgfx/opengl/opengl_shader.hpp"
#include "tgfx/opengl/opengl_texture.hpp"
#include "tgfx/opengl/opengl_mesh.hpp"
#include "tgfx/opengl/opengl_uniform_buffer.hpp"

namespace termin {

// Initialize OpenGL function pointers via glad.
// Must be called after OpenGL context is created.
// Returns true on success.
inline bool init_opengl() {
    return gladLoaderLoadGL() != 0;
}

// GL_KHR_debug constants (may not be in all glad versions)
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_SOURCE_API 0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#define GL_DEBUG_SOURCE_OTHER 0x824B
#define GL_DEBUG_TYPE_ERROR 0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#define GL_DEBUG_TYPE_MARKER 0x8268
#define GL_DEBUG_TYPE_OTHER 0x8251
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

// OpenGL debug callback for detailed error messages.
// Enabled when GL_KHR_debug extension is available.
inline void GLAPIENTRY gl_debug_callback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei /*length*/,
    const GLchar* message,
    const void* /*userParam*/
) {
    // Convert source to string
    const char* src_str = "UNKNOWN";
    switch (source) {
        case GL_DEBUG_SOURCE_API: src_str = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: src_str = "WINDOW"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: src_str = "SHADER"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: src_str = "3RD_PARTY"; break;
        case GL_DEBUG_SOURCE_APPLICATION: src_str = "APP"; break;
        case GL_DEBUG_SOURCE_OTHER: src_str = "OTHER"; break;
    }

    // Convert type to string
    const char* type_str = "UNKNOWN";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: type_str = "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "DEPRECATED"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: type_str = "UNDEFINED"; break;
        case GL_DEBUG_TYPE_PORTABILITY: type_str = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: type_str = "PERFORMANCE"; break;
        case GL_DEBUG_TYPE_MARKER: type_str = "MARKER"; break;
        case GL_DEBUG_TYPE_OTHER: type_str = "OTHER"; break;
    }

    // Log based on severity
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            tc::Log::error("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            tc::Log::warn("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_LOW:
            tc::Log::info("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        default:
            tc::Log::debug("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
    }
}

// ============================================================================
// tgfx_gpu_ops implementation functions
// ============================================================================

namespace gpu_ops_impl {

inline uint32_t texture_upload(
    const uint8_t* data,
    int width,
    int height,
    int channels,
    bool mipmap,
    bool clamp_wrap
) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Determine format
    GLenum format = GL_RGBA;
    GLenum internal_format = GL_RGBA8;
    switch (channels) {
        case 1: format = GL_RED; internal_format = GL_R8; break;
        case 2: format = GL_RG; internal_format = GL_RG8; break;
        case 3: format = GL_RGB; internal_format = GL_RGB8; break;
        case 4: format = GL_RGBA; internal_format = GL_RGBA8; break;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    // Set wrapping mode
    GLenum wrap_mode = clamp_wrap ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);

    // Set filtering
    if (mipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

inline void texture_bind(uint32_t gpu_id, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, gpu_id);
}

inline void texture_delete(uint32_t gpu_id) {
    glDeleteTextures(1, &gpu_id);
}

inline uint32_t depth_texture_upload(
    const float* data,
    int width,
    int height,
    bool compare_mode
) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
        width, height, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT,
        data
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (compare_mode) {
        // Enable hardware depth comparison for sampler2DShadow
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

inline void depth_texture_bind(uint32_t gpu_id, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, gpu_id);
}

// Decomposed mesh upload: takes raw vertex data + layout instead of tc_mesh*
inline uint32_t mesh_upload(
    const void* vertex_data,
    size_t vertex_count,
    const uint32_t* indices,
    size_t index_count,
    const tgfx_vertex_layout* layout,
    uint32_t* out_vbo,
    uint32_t* out_ebo
) {
    if (!vertex_data || vertex_count == 0 || !layout) {
        return 0;
    }

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertex_count * layout->stride,
                 vertex_data, GL_STATIC_DRAW);

    // Upload index data
    if (indices && index_count > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     index_count * sizeof(uint32_t),
                     indices, GL_STATIC_DRAW);
    }

    // Setup vertex attributes
    for (uint8_t i = 0; i < layout->attrib_count; ++i) {
        const tgfx_vertex_attrib& attr = layout->attribs[i];

        GLenum gl_type = GL_FLOAT;
        bool is_integer = false;
        switch (attr.type) {
            case TGFX_ATTRIB_FLOAT32: gl_type = GL_FLOAT; break;
            case TGFX_ATTRIB_INT32: gl_type = GL_INT; is_integer = true; break;
            case TGFX_ATTRIB_UINT32: gl_type = GL_UNSIGNED_INT; is_integer = true; break;
            case TGFX_ATTRIB_INT16: gl_type = GL_SHORT; is_integer = true; break;
            case TGFX_ATTRIB_UINT16: gl_type = GL_UNSIGNED_SHORT; is_integer = true; break;
            case TGFX_ATTRIB_INT8: gl_type = GL_BYTE; is_integer = true; break;
            case TGFX_ATTRIB_UINT8: gl_type = GL_UNSIGNED_BYTE; is_integer = true; break;
        }

        glEnableVertexAttribArray(attr.location);

        // Use glVertexAttribIPointer for integer types (required for ivec4/uvec4 in shader)
        // glVertexAttribPointer converts to float, which breaks integer attributes on AMD
        if (is_integer) {
            glVertexAttribIPointer(
                attr.location,
                attr.size,
                gl_type,
                layout->stride,
                reinterpret_cast<void*>(static_cast<size_t>(attr.offset))
            );
        } else {
            glVertexAttribPointer(
                attr.location,
                attr.size,
                gl_type,
                GL_FALSE,
                layout->stride,
                reinterpret_cast<void*>(static_cast<size_t>(attr.offset))
            );
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Output VBO/EBO IDs to caller
    if (out_vbo) *out_vbo = vbo;
    if (out_ebo) *out_ebo = ebo;

    return vao;
}

// Decomposed mesh draw: takes VAO + index_count + mode instead of tc_mesh*
inline void mesh_draw(uint32_t vao, size_t index_count, tgfx_draw_mode mode) {
    if (vao == 0) {
        return;
    }
    glBindVertexArray(vao);
    GLenum gl_mode = (mode == TGFX_DRAW_LINES) ? GL_LINES : GL_TRIANGLES;
    glDrawElements(gl_mode, static_cast<GLsizei>(index_count), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

inline void mesh_delete(uint32_t vao_id) {
    glDeleteVertexArrays(1, &vao_id);
}

inline void buffer_delete(uint32_t buffer_id) {
    glDeleteBuffers(1, &buffer_id);
}

// Create VAO from existing shared VBO/EBO (for additional GL contexts)
inline uint32_t mesh_create_vao(const tgfx_vertex_layout* layout, uint32_t vbo, uint32_t ebo) {
    if (!layout || vbo == 0) {
        return 0;
    }

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Bind existing shared VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Setup vertex attributes
    for (uint8_t i = 0; i < layout->attrib_count; ++i) {
        const tgfx_vertex_attrib& attr = layout->attribs[i];

        GLenum gl_type = GL_FLOAT;
        bool is_integer = false;
        switch (attr.type) {
            case TGFX_ATTRIB_FLOAT32: gl_type = GL_FLOAT; break;
            case TGFX_ATTRIB_INT32: gl_type = GL_INT; is_integer = true; break;
            case TGFX_ATTRIB_UINT32: gl_type = GL_UNSIGNED_INT; is_integer = true; break;
            case TGFX_ATTRIB_INT16: gl_type = GL_SHORT; is_integer = true; break;
            case TGFX_ATTRIB_UINT16: gl_type = GL_UNSIGNED_SHORT; is_integer = true; break;
            case TGFX_ATTRIB_INT8: gl_type = GL_BYTE; is_integer = true; break;
            case TGFX_ATTRIB_UINT8: gl_type = GL_UNSIGNED_BYTE; is_integer = true; break;
        }

        glEnableVertexAttribArray(attr.location);

        if (is_integer) {
            glVertexAttribIPointer(
                attr.location,
                attr.size,
                gl_type,
                layout->stride,
                reinterpret_cast<void*>(static_cast<size_t>(attr.offset))
            );
        } else {
            glVertexAttribPointer(
                attr.location,
                attr.size,
                gl_type,
                GL_FALSE,
                layout->stride,
                reinterpret_cast<void*>(static_cast<size_t>(attr.offset))
            );
        }
    }

    // Bind existing shared EBO
    if (ebo != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return vao;
}

inline void register_gpu_ops() {
    static tgfx_gpu_ops ops = {};
    // Texture operations
    ops.texture_upload = texture_upload;
    ops.depth_texture_upload = depth_texture_upload;
    ops.texture_bind = texture_bind;
    ops.depth_texture_bind = depth_texture_bind;
    ops.texture_delete = texture_delete;
    // Mesh operations (decomposed - no tc_mesh dependency)
    ops.mesh_upload = mesh_upload;
    ops.mesh_draw = mesh_draw;
    ops.mesh_delete = mesh_delete;
    ops.mesh_create_vao = mesh_create_vao;
    // Buffer operations
    ops.buffer_delete = buffer_delete;
    // User data
    ops.user_data = nullptr;

    tgfx_gpu_set_ops(&ops);
}

} // namespace gpu_ops_impl


} // namespace termin
