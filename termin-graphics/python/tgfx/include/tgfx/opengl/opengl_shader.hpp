#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cstring>

#include "tgfx/handles.hpp"
#include <tcbase/tc_log.hpp>

namespace termin {

inline GLuint compile_shader(const char* source, GLenum shader_type) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return shader;
}

inline GLuint link_program(GLuint vert, GLuint frag, GLuint geom = 0) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    if (geom != 0) {
        glAttachShader(program, geom);
    }

    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("Program link error: ") + log);
    }

    glDetachShader(program, vert);
    glDetachShader(program, frag);
    if (geom != 0) {
        glDetachShader(program, geom);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (geom != 0) {
        glDeleteShader(geom);
    }

    return program;
}

class OpenGLShaderHandle : public ShaderHandle {
public:
    OpenGLShaderHandle(
        const char* vertex_source,
        const char* fragment_source,
        const char* geometry_source = nullptr
    ) : vertex_source_(vertex_source),
        fragment_source_(fragment_source),
        geometry_source_(geometry_source ? geometry_source : ""),
        program_(0) {}

    ~OpenGLShaderHandle() override {
        release();
    }

    void use() override {
        ensure_compiled();
        glUseProgram(program_);
    }

    void stop() override {
        glUseProgram(0);
    }

    void release() override {
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        uniform_cache_.clear();
    }

    void set_uniform_int(const char* name, int value) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniform1i(loc, value);
        }
    }

    void set_uniform_float(const char* name, float value) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniform1f(loc, value);
        }
    }

    void set_uniform_vec2(const char* name, float x, float y) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniform2f(loc, x, y);
        }
    }

    void set_uniform_vec3(const char* name, float x, float y, float z) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniform3f(loc, x, y, z);
        }
    }

    void set_uniform_vec4(const char* name, float x, float y, float z, float w) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniform4f(loc, x, y, z, w);
        }
    }

    void set_uniform_matrix4(const char* name, const float* data, bool transpose) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniformMatrix4fv(loc, 1, transpose ? GL_TRUE : GL_FALSE, data);
        }
    }

    void set_uniform_matrix4_array(const char* name, const float* data, int count, bool transpose) override {
        ensure_compiled();
        GLint loc = get_uniform_location(name);
        if (loc >= 0) {
            glUniformMatrix4fv(loc, count, transpose ? GL_TRUE : GL_FALSE, data);
        } else {
            static int warn_count = 0;
            if (warn_count < 3) {
                warn_count++;
                tc::Log::warn("[set_uniform_matrix4_array] uniform '%s' not found (loc=%d)", name, loc);
            }
        }
    }

    void set_uniform_block_binding(const char* block_name, int binding_point) override {
        ensure_compiled();
        GLuint block_index = glGetUniformBlockIndex(program_, block_name);
        if (block_index != GL_INVALID_INDEX) {
            glUniformBlockBinding(program_, block_index, binding_point);
        }
    }

    GLuint get_program() const { return program_; }

private:
    void ensure_compiled() {
        if (program_ != 0) {
            if (glIsProgram(program_)) {
                return;
            }
            tc::Log::warn("ensure_compiled: program %u invalid, recompiling", program_);
        }

        program_ = 0;
        uniform_cache_.clear();

        GLuint vert = compile_shader(vertex_source_.c_str(), GL_VERTEX_SHADER);
        GLuint frag = compile_shader(fragment_source_.c_str(), GL_FRAGMENT_SHADER);
        GLuint geom = 0;
        if (!geometry_source_.empty()) {
            geom = compile_shader(geometry_source_.c_str(), GL_GEOMETRY_SHADER);
        }
        program_ = link_program(vert, frag, geom);
    }

    GLint get_uniform_location(const char* name) {
        auto it = uniform_cache_.find(name);
        if (it != uniform_cache_.end()) {
            return it->second;
        }
        GLint loc = glGetUniformLocation(program_, name);
        uniform_cache_[name] = loc;
        return loc;
    }

    std::string vertex_source_;
    std::string fragment_source_;
    std::string geometry_source_;
    GLuint program_;
    std::unordered_map<std::string, GLint> uniform_cache_;
};

} // namespace termin
