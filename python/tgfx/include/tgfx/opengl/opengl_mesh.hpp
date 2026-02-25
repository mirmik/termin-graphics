#pragma once

#include <glad/glad.h>
#include <vector>
#include <cstdint>
#include <cstring>

#include "tgfx/handles.hpp"

extern "C" {
#include "tgfx/tgfx_types.h"
}

namespace termin {

// Helper to convert byte offset to void* for glVertexAttribPointer
inline const void* gl_offset(size_t offset) {
    return reinterpret_cast<const void*>(offset);
}

// Generic mesh handle for raw vertex data with custom layout.
class OpenGLRawMeshHandle : public GPUMeshHandle {
public:
    OpenGLRawMeshHandle(
        const float* vertex_data, size_t vertex_bytes,
        const uint32_t* indices, size_t index_count,
        int stride,
        int position_offset, int position_size,
        bool has_normal, int normal_offset,
        bool has_uv, int uv_offset,
        bool has_joints = false, int joints_offset = 0,
        bool has_weights = false, int weights_offset = 0,
        DrawMode mode = DrawMode::Triangles
    ) : vao_(0), vbo_(0), ebo_(0), index_count_(static_cast<GLsizei>(index_count)), draw_mode_(mode) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_bytes, vertex_data, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint32_t), indices, GL_STATIC_DRAW);

        // Position: location 0
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, position_size, GL_FLOAT, GL_FALSE, stride, gl_offset(position_offset));

        // Normal: location 1 (optional)
        if (has_normal) {
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, gl_offset(normal_offset));
        }

        // UV: location 2 (optional)
        if (has_uv) {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, gl_offset(uv_offset));
        }

        // Joints: location 3 (optional, for skinning)
        if (has_joints) {
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, gl_offset(joints_offset));
        }

        // Weights: location 4 (optional, for skinning)
        if (has_weights) {
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, gl_offset(weights_offset));
        }

        glBindVertexArray(0);
    }

    ~OpenGLRawMeshHandle() override {
        release();
    }

    void draw() override {
        glBindVertexArray(vao_);
        GLenum gl_mode = (draw_mode_ == DrawMode::Lines) ? GL_LINES : GL_TRIANGLES;
        glDrawElements(gl_mode, index_count_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void release() override {
        if (vao_ != 0) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        if (vbo_ != 0) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
        if (ebo_ != 0) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    }

private:
    GLuint vao_;
    GLuint vbo_;
    GLuint ebo_;
    GLsizei index_count_;
    DrawMode draw_mode_;
};

// Mesh handle that uses tgfx_vertex_layout to set up vertex attributes.
class OpenGLLayoutMeshHandle : public GPUMeshHandle {
public:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei index_count_ = 0;
    DrawMode draw_mode_ = DrawMode::Triangles;

    OpenGLLayoutMeshHandle(
        const void* vertex_data,
        size_t vertex_count,
        const uint32_t* indices,
        size_t index_count,
        const tgfx_vertex_layout* layout,
        DrawMode mode = DrawMode::Triangles
    ) : draw_mode_(mode) {
        if (!vertex_data || vertex_count == 0 || !layout) return;

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);

        glBindVertexArray(vao_);

        size_t vertex_bytes = vertex_count * layout->stride;
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_bytes, vertex_data, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
        index_count_ = static_cast<GLsizei>(index_count);

        GLsizei stride = layout->stride;
        for (uint8_t i = 0; i < layout->attrib_count; i++) {
            const tgfx_vertex_attrib& attr = layout->attribs[i];
            GLuint location = attr.location;

            glEnableVertexAttribArray(location);

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

            if (is_integer) {
                glVertexAttribIPointer(location, attr.size, gl_type, stride, gl_offset(attr.offset));
            } else {
                glVertexAttribPointer(location, attr.size, gl_type, GL_FALSE, stride, gl_offset(attr.offset));
            }
        }

        glBindVertexArray(0);
    }

    ~OpenGLLayoutMeshHandle() override {
        release();
    }

    void draw() override {
        if (vao_ == 0) return;
        glBindVertexArray(vao_);
        GLenum gl_mode = (draw_mode_ == DrawMode::Lines) ? GL_LINES : GL_TRIANGLES;
        glDrawElements(gl_mode, index_count_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void release() override {
        if (vao_ != 0) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        if (vbo_ != 0) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
        if (ebo_ != 0) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    }
};

} // namespace termin
