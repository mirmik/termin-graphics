#pragma once

#include <glad/glad.h>
#include <cstdint>
#include <cstring>

#include "tgfx/handles.hpp"
#include "tgfx/tgfx_log.hpp"

namespace termin {

// OpenGL Uniform Buffer Object (UBO) implementation.
// Uses GL_DYNAMIC_DRAW for frequent updates.
class OpenGLUniformBufferHandle : public UniformBufferHandle {
public:
    GLuint handle_ = 0;
    size_t size_ = 0;
    int bound_point_ = -1;

    explicit OpenGLUniformBufferHandle(size_t size) : size_(size) {
        glGenBuffers(1, &handle_);
        glBindBuffer(GL_UNIFORM_BUFFER, handle_);
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    ~OpenGLUniformBufferHandle() override {
        release();
    }

    void update(const void* data, size_t size, size_t offset = 0) override {
        if (handle_ == 0) return;
        if (offset + size > size_) {
            tgfx::Log::error("OpenGLUniformBufferHandle::update: out of bounds (offset=%zu, size=%zu, buffer_size=%zu)",
                           offset, size, size_);
            return;
        }

        glBindBuffer(GL_UNIFORM_BUFFER, handle_);
        glBufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(offset),
                        static_cast<GLsizeiptr>(size), data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void bind(int binding_point) override {
        if (handle_ == 0) return;
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(binding_point), handle_);
        bound_point_ = binding_point;
    }

    void unbind() override {
        if (bound_point_ >= 0) {
            glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(bound_point_), 0);
            bound_point_ = -1;
        }
    }

    void release() override {
        if (handle_ != 0) {
            glDeleteBuffers(1, &handle_);
            handle_ = 0;
        }
    }

    uint32_t get_id() const override { return handle_; }
    size_t get_size() const override { return size_; }
};

} // namespace termin
