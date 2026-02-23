#pragma once

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
}

#include "tgfx/tgfx_log.hpp"
#include "tgfx/graphics_backend.hpp"
#include "tgfx/opengl/opengl_shader.hpp"
#include "tgfx/opengl/opengl_texture.hpp"
#include "tgfx/opengl/opengl_mesh.hpp"
#include "tgfx/opengl/opengl_framebuffer.hpp"
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
            tgfx::Log::error("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            tgfx::Log::warn("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        case GL_DEBUG_SEVERITY_LOW:
            tgfx::Log::info("[GL %s/%s #%u] %s", src_str, type_str, id, message);
            break;
        default:
            tgfx::Log::debug("[GL %s/%s #%u] %s", src_str, type_str, id, message);
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

inline uint32_t shader_compile(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source
) {
    auto compile_shader = [](GLenum type, const char* source) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(shader, 512, nullptr, info_log);
            tgfx::Log::error("Shader compile error: %s", info_log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_source);
    if (vs == 0) return 0;

    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint gs = 0;
    if (geometry_source && geometry_source[0] != '\0') {
        gs = compile_shader(GL_GEOMETRY_SHADER, geometry_source);
        if (gs == 0) {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return 0;
        }
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    if (gs != 0) {
        glAttachShader(program, gs);
    }
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program, 512, nullptr, info_log);
        tgfx::Log::error("Shader link error: %s", info_log);
        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (gs != 0) glDeleteShader(gs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    if (gs != 0) glDeleteShader(gs);

    return program;
}

inline void shader_use(uint32_t gpu_id) {
    glUseProgram(gpu_id);
}

inline void shader_delete(uint32_t gpu_id) {
    glDeleteProgram(gpu_id);
}

inline void shader_set_int(uint32_t gpu_id, const char* name, int value) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniform1i(loc, value);
    }
}

inline void shader_set_float(uint32_t gpu_id, const char* name, float value) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniform1f(loc, value);
    }
}

inline void shader_set_vec2(uint32_t gpu_id, const char* name, float x, float y) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniform2f(loc, x, y);
    }
}

inline void shader_set_vec3(uint32_t gpu_id, const char* name, float x, float y, float z) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniform3f(loc, x, y, z);
    }
}

inline void shader_set_vec4(uint32_t gpu_id, const char* name, float x, float y, float z, float w) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniform4f(loc, x, y, z, w);
    }
}

inline void shader_set_mat4(uint32_t gpu_id, const char* name, const float* data, bool transpose) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniformMatrix4fv(loc, 1, transpose ? GL_TRUE : GL_FALSE, data);
    }
}

inline void shader_set_mat4_array(uint32_t gpu_id, const char* name, const float* data, int count, bool transpose) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc != -1) {
        glUniformMatrix4fv(loc, count, transpose ? GL_TRUE : GL_FALSE, data);
    } else {
        static int debug_count = 0;
        if (debug_count < 5 && std::strstr(name, "bone") != nullptr) {
            tgfx::Log::warn("[shader_set_mat4_array] Uniform '%s' not found in program %u", name, gpu_id);
            debug_count++;
        }
    }
}

inline void shader_set_block_binding(uint32_t gpu_id, const char* block_name, int binding_point) {
    GLuint block_index = glGetUniformBlockIndex(gpu_id, block_name);
    if (block_index != GL_INVALID_INDEX) {
        glUniformBlockBinding(gpu_id, block_index, binding_point);
    }
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
    // Shader operations
    ops.shader_compile = shader_compile;
    ops.shader_use = shader_use;
    ops.shader_delete = shader_delete;
    // Uniform setters
    ops.shader_set_int = shader_set_int;
    ops.shader_set_float = shader_set_float;
    ops.shader_set_vec2 = shader_set_vec2;
    ops.shader_set_vec3 = shader_set_vec3;
    ops.shader_set_vec4 = shader_set_vec4;
    ops.shader_set_mat4 = shader_set_mat4;
    ops.shader_set_mat4_array = shader_set_mat4_array;
    ops.shader_set_block_binding = shader_set_block_binding;
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

// OpenGL 3.3+ graphics backend implementation.
// Singleton - only one instance can exist.
class OpenGLGraphicsBackend : public GraphicsBackend {
private:
    static OpenGLGraphicsBackend* instance_;

    // GPU timer query data
    struct GPUQueryData {
        GLuint query_id;
        double result_ms;
        bool pending;
    };

    bool initialized_;

    // UI drawing resources
    GLuint ui_vao_ = 0;
    GLuint ui_vbo_ = 0;

    // Immediate mode rendering resources
    GLuint immediate_vao_ = 0;
    GLuint immediate_vbo_ = 0;

    // GPU timer queries
    std::unordered_map<std::string, GPUQueryData> gpu_queries_;
    std::string current_gpu_query_;

    // Static flag for GLAD initialization (shared across all backends)
    static bool glad_initialized_;

    OpenGLGraphicsBackend() : initialized_(false) {
        tgfx::Log::info("[OpenGLGraphicsBackend] Constructor, this=%p, glad_initialized_=%d",
                      this, glad_initialized_);
    }

    // Delete copy and move constructors and operators
    OpenGLGraphicsBackend(const OpenGLGraphicsBackend&) = delete;
    OpenGLGraphicsBackend(OpenGLGraphicsBackend&&) = delete;
    OpenGLGraphicsBackend& operator=(const OpenGLGraphicsBackend&) = delete;
    OpenGLGraphicsBackend& operator=(OpenGLGraphicsBackend&&) = delete;

public:
    static OpenGLGraphicsBackend& get_instance();

    ~OpenGLGraphicsBackend() override = default;

    void ensure_ready() override {
        // Initialize GLAD if not already done
        if (!glad_initialized_) {
            if (!gladLoaderLoadGL()) {
                throw std::runtime_error("Failed to initialize GLAD");
            }
            glad_initialized_ = true;

            // Register GPU ops for tgfx_gpu module
            gpu_ops_impl::register_gpu_ops();

            // Enable OpenGL debug output for detailed error messages (if available)
            #ifdef GLAD_GL_KHR_debug
            if (GLAD_GL_KHR_debug && glDebugMessageCallback) {
                glEnable(GL_DEBUG_OUTPUT);
                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                glDebugMessageCallback(gl_debug_callback, nullptr);
                // Filter out notifications (too verbose)
                glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
                tgfx::Log::info("OpenGL debug output enabled");
            }
            #endif
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        initialized_ = true;
    }

    // --- Viewport ---

    void set_viewport(int x, int y, int width, int height) override {
        glViewport(x, y, width, height);
    }

    void enable_scissor(int x, int y, int width, int height) override {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, width, height);
    }

    void disable_scissor() override {
        glDisable(GL_SCISSOR_TEST);
    }

    // --- Clear ---

    void clear_color_depth(float r, float g, float b, float a) override {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void clear_color(float r, float g, float b, float a) override {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void clear_depth(float value) override {
        glClearDepth(static_cast<double>(value));
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    // --- Color mask ---

    void set_color_mask(bool r, bool g, bool b, bool a) override {
        glColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE,
                    b ? GL_TRUE : GL_FALSE, a ? GL_TRUE : GL_FALSE);
    }

    // --- Depth ---

    void set_depth_test(bool enabled) override {
        if (enabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }

    void set_depth_mask(bool enabled) override {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    void set_depth_func(DepthFunc func) override {
        static const GLenum gl_funcs[] = {
            GL_LESS, GL_LEQUAL, GL_EQUAL, GL_GREATER,
            GL_GEQUAL, GL_NOTEQUAL, GL_ALWAYS, GL_NEVER
        };
        glDepthFunc(gl_funcs[static_cast<int>(func)]);
    }

    // --- Culling ---

    void set_cull_face(bool enabled) override {
        if (enabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    // --- Blending ---

    void set_blend(bool enabled) override {
        if (enabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
    }

    void set_blend_func(BlendFactor src, BlendFactor dst) override {
        static const GLenum gl_factors[] = {
            GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
        };
        glBlendFunc(gl_factors[static_cast<int>(src)], gl_factors[static_cast<int>(dst)]);
    }

    // --- Polygon mode ---

    void set_polygon_mode(PolygonMode mode) override {
        GLenum gl_mode = (mode == PolygonMode::Line) ? GL_LINE : GL_FILL;
        glPolygonMode(GL_FRONT_AND_BACK, gl_mode);
    }

    // --- State management ---

    void reset_state() override {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        glDisable(GL_BLEND);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
    }

    void reset_gl_state() override {
        // Reset active texture unit to 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Unbind shader program
        glUseProgram(0);

        // Unbind VAO
        glBindVertexArray(0);

        // Unbind buffers
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        // Reset render state
        reset_state();
    }

    void apply_render_state(const RenderState& state) override {
        set_polygon_mode(state.polygon_mode);
        set_cull_face(state.cull);
        set_depth_test(state.depth_test);
        set_depth_mask(state.depth_write);
        set_blend(state.blend);
        if (state.blend) {
            set_blend_func(state.blend_src, state.blend_dst);
        }
    }

    // --- Resource creation ---

    ShaderHandlePtr create_shader(
        const char* vertex_source,
        const char* fragment_source,
        const char* geometry_source
    ) override {
        return std::make_unique<OpenGLShaderHandle>(vertex_source, fragment_source, geometry_source);
    }

    GPUMeshHandlePtr create_mesh(
        const void* vertex_data,
        size_t vertex_count,
        const uint32_t* indices,
        size_t index_count,
        const tgfx_vertex_layout* layout,
        DrawMode mode = DrawMode::Triangles
    ) override {
        return std::make_unique<OpenGLLayoutMeshHandle>(
            vertex_data, vertex_count, indices, index_count, layout, mode);
    }

    GPUTextureHandlePtr create_texture(
        const uint8_t* data,
        int width,
        int height,
        int channels,
        bool mipmap,
        bool clamp
    ) override {
        return std::make_unique<OpenGLTextureHandle>(data, width, height, channels, mipmap, clamp);
    }

    FramebufferHandlePtr create_framebuffer(int width, int height, int samples, const std::string& format = "", TextureFilter filter = TextureFilter::LINEAR) override;
    FramebufferHandlePtr create_shadow_framebuffer(int width, int height) override;

    UniformBufferHandlePtr create_uniform_buffer(size_t size) override {
        return std::make_unique<OpenGLUniformBufferHandle>(size);
    }

    // Create a handle that wraps an external FBO (e.g., window default FBO).
    // Does not allocate any resources - useful for window backends.
    FramebufferHandlePtr create_external_framebuffer(uint32_t fbo_id, int width, int height) override {
        return OpenGLFramebufferHandle::create_external(fbo_id, width, height);
    }

    // --- Framebuffer operations ---

    void bind_framebuffer(FramebufferHandle* fbo) override {
        if (glad_glBindFramebuffer == nullptr) {
            tgfx::Log::error("[OpenGLGraphicsBackend] bind_framebuffer: glad_glBindFramebuffer is NULL! GLAD not loaded?");
            return;
        }
        // Check GL error state before bind
        GLenum pre_err = glGetError();
        if (pre_err != GL_NO_ERROR) {
            tgfx::Log::warn("[OpenGLGraphicsBackend] bind_framebuffer: pre-existing GL error: 0x%X", pre_err);
        }

        GLuint fbo_id = fbo ? fbo->get_fbo_id() : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
    }

    void bind_framebuffer_id(uint32_t fbo_id) override {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
    }

    void blit_framebuffer(
        FramebufferHandle* src,
        FramebufferHandle* dst,
        int src_x0, int src_y0, int src_x1, int src_y1,
        int dst_x0, int dst_y0, int dst_x1, int dst_y1,
        bool blit_color = true,
        bool blit_depth = false
    ) override {
        GLuint src_fbo = src ? src->get_fbo_id() : 0;
        GLuint dst_fbo = dst ? dst->get_fbo_id() : 0;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);

        GLbitfield mask = 0;
        if (blit_color) mask |= GL_COLOR_BUFFER_BIT;
        if (blit_depth) mask |= GL_DEPTH_BUFFER_BIT;

        if (mask != 0) {
            glBlitFramebuffer(
                src_x0, src_y0, src_x1, src_y1,
                dst_x0, dst_y0, dst_x1, dst_y1,
                mask,
                GL_NEAREST
            );
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void blit_framebuffer_to_id(
        FramebufferHandle& src,
        uint32_t dst_fbo_id,
        const Rect2i& src_rect,
        const Rect2i& dst_rect,
        bool blit_color = true,
        bool blit_depth = false
    ) override {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src.get_fbo_id());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo_id);

        GLbitfield mask = 0;
        if (blit_color) mask |= GL_COLOR_BUFFER_BIT;
        if (blit_depth) mask |= GL_DEPTH_BUFFER_BIT;

        if (mask != 0) {
            glBlitFramebuffer(
                src_rect.x0, src_rect.y0, src_rect.x1, src_rect.y1,
                dst_rect.x0, dst_rect.y0, dst_rect.x1, dst_rect.y1,
                mask,
                GL_NEAREST
            );
        }

        // Re-bind destination FBO (important for WPF where dst is not FBO 0)
        glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo_id);
    }

    // --- Read operations ---

    std::array<float, 4> read_pixel(FramebufferHandle* fbo, int x, int y) override {
        bind_framebuffer(fbo);

        uint8_t data[4];
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);

        return {
            data[0] / 255.0f,
            data[1] / 255.0f,
            data[2] / 255.0f,
            data[3] / 255.0f
        };
    }

    std::optional<float> read_depth_pixel(FramebufferHandle* fbo, int x, int y) override {
        if (fbo == nullptr) return std::nullopt;

        bind_framebuffer(fbo);

        float depth;
        glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

        return depth;
    }

    bool read_depth_buffer(FramebufferHandle* fbo, float* out_data) override {
        if (fbo == nullptr || out_data == nullptr) return false;

        int width = fbo->get_width();
        int height = fbo->get_height();
        if (width <= 0 || height <= 0) return false;

        bind_framebuffer(fbo);

        // Read into temporary buffer (OpenGL origin is bottom-left)
        std::vector<float> temp(width * height);
        glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, temp.data());

        // Flip vertically to top-left origin
        for (int y = 0; y < height; ++y) {
            int src_row = height - 1 - y;
            std::memcpy(
                out_data + y * width,
                temp.data() + src_row * width,
                width * sizeof(float)
            );
        }

        return true;
    }

    bool read_color_buffer_float(FramebufferHandle* fbo, float* out_data) override {
        if (fbo == nullptr || out_data == nullptr) return false;

        int width = fbo->get_width();
        int height = fbo->get_height();
        if (width <= 0 || height <= 0) return false;

        GLuint read_fbo = fbo->get_fbo_id();
        GLuint temp_fbo = 0;
        GLuint temp_tex = 0;

        // If MSAA, resolve to temporary non-MSAA FBO first
        if (fbo->is_msaa()) {
            // Create temporary texture
            glGenTextures(1, &temp_tex);
            glBindTexture(GL_TEXTURE_2D, temp_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            // Create temporary FBO
            glGenFramebuffers(1, &temp_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, temp_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, temp_tex, 0);

            // Blit from MSAA to non-MSAA (resolve)
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->get_fbo_id());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, temp_fbo);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            read_fbo = temp_fbo;
        }

        // Bind the FBO to read from
        glBindFramebuffer(GL_FRAMEBUFFER, read_fbo);

        // Read into temporary buffer (OpenGL origin is bottom-left)
        // RGBA = 4 floats per pixel
        std::vector<float> temp(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, temp.data());

        // Flip vertically to top-left origin
        size_t row_size = width * 4 * sizeof(float);
        for (int y = 0; y < height; ++y) {
            int src_row = height - 1 - y;
            std::memcpy(
                out_data + y * width * 4,
                temp.data() + src_row * width * 4,
                row_size
            );
        }

        // Cleanup temporary resources
        if (temp_fbo != 0) {
            glDeleteFramebuffers(1, &temp_fbo);
        }
        if (temp_tex != 0) {
            glDeleteTextures(1, &temp_tex);
        }

        return true;
    }

    // --- UI drawing ---

    void draw_ui_vertices(const float* vertices, int vertex_count) override {
        ensure_ui_buffers();

        glBindVertexArray(ui_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_count * 2 * sizeof(float), vertices, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDisableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, vertex_count);
        glBindVertexArray(0);
    }

    void draw_ui_textured_quad() override {
        static const float fs_verts[] = {
            -1, -1, 0, 0,
             1, -1, 1, 0,
            -1,  1, 0, 1,
             1,  1, 1, 1
        };
        draw_ui_textured_quad_impl(fs_verts, 4);
    }

    void draw_ui_textured_quad(const float* vertices, int vertex_count) {
        draw_ui_textured_quad_impl(vertices, vertex_count);
    }

    // --- Immediate mode rendering ---

    void draw_immediate_lines(
        const float* vertices,
        int vertex_count
    ) override {
        draw_immediate_impl(vertices, vertex_count, GL_LINES);
    }

    void draw_immediate_triangles(
        const float* vertices,
        int vertex_count
    ) override {
        draw_immediate_impl(vertices, vertex_count, GL_TRIANGLES);
    }

    bool check_gl_error(const char* location) override {
        GLenum err = glGetError();
        if (err == GL_NO_ERROR) {
            return false;
        }

        const char* name = "UNKNOWN";
        switch (err) {
            case GL_INVALID_ENUM: name = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: name = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: name = "GL_OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: name = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        }

        GLint fbo = 0, program = 0, vao = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);

        tgfx::Log::error("GL error %s (0x%x) at '%s' [FBO=%d, program=%d, VAO=%d]",
                       name, err, location, fbo, program, vao);

        // Validate current shader program for additional info
        if (program > 0) {
            glValidateProgram(program);
            GLint validate_status = 0;
            glGetProgramiv(program, GL_VALIDATE_STATUS, &validate_status);
            if (validate_status == GL_FALSE) {
                char info_log[1024];
                GLsizei log_length = 0;
                glGetProgramInfoLog(program, sizeof(info_log), &log_length, info_log);
                if (log_length > 0) {
                    tgfx::Log::error("  Shader validation failed: %s", info_log);
                }
            }
        }

        // Check framebuffer completeness
        if (fbo > 0) {
            GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
                const char* fb_err = "UNKNOWN";
                switch (fb_status) {
                    case GL_FRAMEBUFFER_UNDEFINED: fb_err = "UNDEFINED"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: fb_err = "INCOMPLETE_ATTACHMENT"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: fb_err = "MISSING_ATTACHMENT"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: fb_err = "INCOMPLETE_DRAW_BUFFER"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: fb_err = "INCOMPLETE_READ_BUFFER"; break;
                    case GL_FRAMEBUFFER_UNSUPPORTED: fb_err = "UNSUPPORTED"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: fb_err = "INCOMPLETE_MULTISAMPLE"; break;
                    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: fb_err = "INCOMPLETE_LAYER_TARGETS"; break;
                }
                tgfx::Log::error("  Framebuffer incomplete: %s", fb_err);
            }
        }

        return true;
    }

    void clear_gl_errors() override {
        while (glGetError() != GL_NO_ERROR) {}
    }

    uint32_t get_bound_texture(int unit) override {
        GLint current_unit;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &current_unit);
        glActiveTexture(GL_TEXTURE0 + unit);
        GLint tex_id;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex_id);
        glActiveTexture(current_unit);
        return static_cast<uint32_t>(tex_id);
    }

    // --- Synchronization ---

    void flush() override {
        glFlush();
    }

    void finish() override {
        glFinish();
    }

    // --- GPU Timer Queries ---

    void begin_gpu_query(const char* name) override {
        std::string key(name);

        // Get or create query object
        auto it = gpu_queries_.find(key);
        if (it == gpu_queries_.end()) {
            GLuint query;
            glGenQueries(1, &query);
            gpu_queries_[key] = {query, 0.0, false};
            it = gpu_queries_.find(key);
        }

        // Begin query
        glBeginQuery(GL_TIME_ELAPSED, it->second.query_id);
        current_gpu_query_ = key;
    }

    void end_gpu_query() override {
        if (current_gpu_query_.empty()) return;
        glEndQuery(GL_TIME_ELAPSED);
        gpu_queries_[current_gpu_query_].pending = true;
        current_gpu_query_.clear();
    }

    double get_gpu_query_ms(const char* name) override {
        auto it = gpu_queries_.find(name);
        if (it == gpu_queries_.end()) return -1.0;

        auto& q = it->second;

        // If pending, try to get result
        if (q.pending) {
            GLint available = 0;
            glGetQueryObjectiv(q.query_id, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 elapsed_ns;
                glGetQueryObjectui64v(q.query_id, GL_QUERY_RESULT, &elapsed_ns);
                q.result_ms = elapsed_ns / 1000000.0;
                q.pending = false;
            }
        }

        return q.pending ? -1.0 : q.result_ms;
    }

    void sync_gpu_queries() override {
        for (auto& [name, q] : gpu_queries_) {
            if (q.pending) {
                GLuint64 elapsed_ns;
                glGetQueryObjectui64v(q.query_id, GL_QUERY_RESULT, &elapsed_ns);
                q.result_ms = elapsed_ns / 1000000.0;
                q.pending = false;
            }
        }
    }

private:
    void draw_ui_textured_quad_impl(const float* vertices, int vertex_count) {
        ensure_ui_buffers();

        glBindVertexArray(ui_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_count * 4 * sizeof(float), vertices, GL_DYNAMIC_DRAW);

        constexpr GLsizei stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, vertex_count);
        glBindVertexArray(0);
    }

    // Simplified: no tc_gpu_context dependency, uses simple member variables
    void ensure_ui_buffers() {
        if (ui_vao_ != 0 && glIsVertexArray(ui_vao_)) {
            return;
        }

        glGenVertexArrays(1, &ui_vao_);
        glGenBuffers(1, &ui_vbo_);
    }

    void draw_immediate_impl(const float* vertices, int vertex_count, GLenum mode) {
        if (vertex_count <= 0 || !vertices) return;

        ensure_immediate_buffers();

        glBindVertexArray(immediate_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, immediate_vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_count * 7 * sizeof(float), vertices, GL_DYNAMIC_DRAW);

        glDrawArrays(mode, 0, vertex_count);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // Simplified: no tc_gpu_context dependency, uses simple member variables
    void ensure_immediate_buffers() {
        if (immediate_vao_ != 0 && glIsVertexArray(immediate_vao_)) {
            return;
        }

        glGenBuffers(1, &immediate_vbo_);

        glGenVertexArrays(1, &immediate_vao_);

        glBindVertexArray(immediate_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, immediate_vbo_);

        constexpr GLsizei stride = 7 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
};

} // namespace termin
