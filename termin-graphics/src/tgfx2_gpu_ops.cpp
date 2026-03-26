// tgfx2_gpu_ops.cpp - tgfx_gpu_ops vtable implementation backed by tgfx2::IRenderDevice
// Routes resource creation through tgfx2 and extracts GL IDs for backward compatibility.

#include <tgfx/tgfx2_interop.h>
#include <tgfx/tgfx_gpu_ops.h>
#include <tgfx/tgfx_types.h>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tcbase/tc_log.h>

#include <cstring>
#include <unordered_map>

// ============================================================================
// Global tgfx2 device pointer
// ============================================================================

static void* g_tgfx2_device = nullptr;

void tgfx2_interop_set_device(void* device) {
    g_tgfx2_device = device;
}

void* tgfx2_interop_get_device(void) {
    return g_tgfx2_device;
}

// ============================================================================
// Helpers
// ============================================================================

static tgfx2::IRenderDevice* get_device() {
    return static_cast<tgfx2::IRenderDevice*>(g_tgfx2_device);
}

static tgfx2::OpenGLRenderDevice* get_gl_device() {
    return static_cast<tgfx2::OpenGLRenderDevice*>(g_tgfx2_device);
}

// Map channels to tgfx2 pixel format
static tgfx2::PixelFormat channels_to_format(int channels) {
    switch (channels) {
        case 1: return tgfx2::PixelFormat::R8_UNorm;
        case 2: return tgfx2::PixelFormat::RG8_UNorm;
        case 3: return tgfx2::PixelFormat::RGB8_UNorm;
        case 4: return tgfx2::PixelFormat::RGBA8_UNorm;
        default: return tgfx2::PixelFormat::RGBA8_UNorm;
    }
}

// Track tgfx2 handle for a given GL ID so we can destroy properly
// Key: gl_id, Value: tgfx2 handle id
// We maintain separate maps per resource type.
static std::unordered_map<uint32_t, uint32_t> g_texture_map;   // gl_id -> TextureHandle.id
static std::unordered_map<uint32_t, uint32_t> g_sampler_map;   // gl_id -> SamplerHandle.id
static std::unordered_map<uint32_t, uint32_t> g_shader_vs_map; // program gl_id -> vertex ShaderHandle.id
static std::unordered_map<uint32_t, uint32_t> g_shader_fs_map; // program gl_id -> fragment ShaderHandle.id
static std::unordered_map<uint32_t, uint32_t> g_shader_gs_map; // program gl_id -> geometry ShaderHandle.id
static std::unordered_map<uint32_t, uint32_t> g_pipeline_map;  // program gl_id -> PipelineHandle.id
static std::unordered_map<uint32_t, uint32_t> g_buffer_map;    // gl_id -> BufferHandle.id

// ============================================================================
// Texture operations
// ============================================================================

static uint32_t tgfx2_texture_upload(
    const uint8_t* data,
    int width, int height, int channels,
    bool mipmap, bool clamp
) {
    auto* dev = get_device();
    auto* gl_dev = get_gl_device();
    if (!dev || !gl_dev) return 0;

    tgfx2::TextureDesc desc;
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = channels_to_format(channels);
    desc.usage = tgfx2::TextureUsage::Sampled;
    desc.mip_levels = mipmap ? 0 : 1; // 0 = auto mipmap

    auto handle = dev->create_texture(desc);
    if (!handle) return 0;

    // Upload pixel data
    size_t byte_size = (size_t)width * height * channels;
    dev->upload_texture(handle, {data, byte_size});

    // Extract GL ID for backward compat
    auto* gl_tex = gl_dev->get_texture(handle);
    if (!gl_tex) {
        dev->destroy(handle);
        return 0;
    }

    uint32_t gl_id = gl_tex->gl_id;

    // Configure sampler state on the GL texture directly
    // (tgfx2 uses separate sampler objects, but legacy code expects
    // texture-bound filtering/wrapping)
    glBindTexture(GL_TEXTURE_2D, gl_id);

    GLenum wrap = clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    if (mipmap) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    g_texture_map[gl_id] = handle.id;
    return gl_id;
}

static uint32_t tgfx2_depth_texture_upload(
    const float* data,
    int width, int height,
    bool compare_mode
) {
    auto* dev = get_device();
    auto* gl_dev = get_gl_device();
    if (!dev || !gl_dev) return 0;

    tgfx2::TextureDesc desc;
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = tgfx2::PixelFormat::D24_UNorm_S8_UInt;
    desc.usage = tgfx2::TextureUsage::DepthStencilAttachment | tgfx2::TextureUsage::Sampled;
    desc.mip_levels = 1;

    auto handle = dev->create_texture(desc);
    if (!handle) return 0;

    // Upload depth data
    if (data) {
        size_t byte_size = (size_t)width * height * sizeof(float);
        dev->upload_texture(handle, {reinterpret_cast<const uint8_t*>(data), byte_size});
    }

    auto* gl_tex = gl_dev->get_texture(handle);
    if (!gl_tex) {
        dev->destroy(handle);
        return 0;
    }

    uint32_t gl_id = gl_tex->gl_id;

    // Configure depth texture sampling (legacy expects texture-bound state)
    glBindTexture(GL_TEXTURE_2D, gl_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (compare_mode) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    g_texture_map[gl_id] = handle.id;
    return gl_id;
}

static void tgfx2_texture_bind(uint32_t gpu_id, int unit) {
    // Direct GL — same as legacy, since legacy code expects this
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, gpu_id);
}

static void tgfx2_depth_texture_bind(uint32_t gpu_id, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, gpu_id);
}

static void tgfx2_texture_delete(uint32_t gpu_id) {
    auto* dev = get_device();
    auto it = g_texture_map.find(gpu_id);
    if (it != g_texture_map.end() && dev) {
        dev->destroy(tgfx2::TextureHandle{it->second});
        g_texture_map.erase(it);
    }
}

// ============================================================================
// Shader operations
// ============================================================================

static uint32_t tgfx2_shader_compile(
    const char* vertex_source,
    const char* fragment_source,
    const char* geometry_source
) {
    auto* dev = get_device();
    auto* gl_dev = get_gl_device();
    if (!dev || !gl_dev) return 0;

    // Create shader modules
    tgfx2::ShaderDesc vs_desc;
    vs_desc.stage = tgfx2::ShaderStage::Vertex;
    vs_desc.source = vertex_source;

    tgfx2::ShaderDesc fs_desc;
    fs_desc.stage = tgfx2::ShaderStage::Fragment;
    fs_desc.source = fragment_source;

    tgfx2::ShaderHandle vs, fs, gs;
    try {
        vs = dev->create_shader(vs_desc);
        fs = dev->create_shader(fs_desc);
    } catch (const std::exception& e) {
        tc_log_error("tgfx2_shader_compile: %s", e.what());
        if (vs) dev->destroy(vs);
        return 0;
    }

    if (geometry_source && geometry_source[0] != '\0') {
        tgfx2::ShaderDesc gs_desc;
        gs_desc.stage = tgfx2::ShaderStage::Geometry;
        gs_desc.source = geometry_source;
        try {
            gs = dev->create_shader(gs_desc);
        } catch (const std::exception& e) {
            tc_log_error("tgfx2_shader_compile (geometry): %s", e.what());
            dev->destroy(vs);
            dev->destroy(fs);
            return 0;
        }
    }

    // Create pipeline (links the program)
    tgfx2::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.geometry_shader = gs;

    tgfx2::PipelineHandle pipeline;
    try {
        pipeline = dev->create_pipeline(pipe_desc);
    } catch (const std::exception& e) {
        tc_log_error("tgfx2_shader_compile (link): %s", e.what());
        dev->destroy(vs);
        dev->destroy(fs);
        if (gs) dev->destroy(gs);
        return 0;
    }

    // Extract GL program ID
    auto* gl_pipe = gl_dev->get_pipeline(pipeline);
    if (!gl_pipe) {
        dev->destroy(pipeline);
        dev->destroy(vs);
        dev->destroy(fs);
        if (gs) dev->destroy(gs);
        return 0;
    }

    uint32_t program = gl_pipe->program;
    g_pipeline_map[program] = pipeline.id;
    g_shader_vs_map[program] = vs.id;
    g_shader_fs_map[program] = fs.id;
    if (gs) g_shader_gs_map[program] = gs.id;

    return program;
}

static void tgfx2_shader_use(uint32_t gpu_id) {
    glUseProgram(gpu_id);
}

static void tgfx2_shader_delete(uint32_t gpu_id) {
    auto* dev = get_device();
    if (!dev) return;

    auto pipe_it = g_pipeline_map.find(gpu_id);
    if (pipe_it != g_pipeline_map.end()) {
        dev->destroy(tgfx2::PipelineHandle{pipe_it->second});
        g_pipeline_map.erase(pipe_it);
    }

    auto vs_it = g_shader_vs_map.find(gpu_id);
    if (vs_it != g_shader_vs_map.end()) {
        dev->destroy(tgfx2::ShaderHandle{vs_it->second});
        g_shader_vs_map.erase(vs_it);
    }

    auto fs_it = g_shader_fs_map.find(gpu_id);
    if (fs_it != g_shader_fs_map.end()) {
        dev->destroy(tgfx2::ShaderHandle{fs_it->second});
        g_shader_fs_map.erase(fs_it);
    }

    auto gs_it = g_shader_gs_map.find(gpu_id);
    if (gs_it != g_shader_gs_map.end()) {
        dev->destroy(tgfx2::ShaderHandle{gs_it->second});
        g_shader_gs_map.erase(gs_it);
    }
}

// Uniform setters — direct GL calls (same as legacy)
// tgfx2 uniform model (UBO/resource sets) is for Phase 4

static void tgfx2_shader_set_int(uint32_t gpu_id, const char* name, int value) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniform1i(loc, value);
}

static void tgfx2_shader_set_float(uint32_t gpu_id, const char* name, float value) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniform1f(loc, value);
}

static void tgfx2_shader_set_vec2(uint32_t gpu_id, const char* name, float x, float y) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

static void tgfx2_shader_set_vec3(uint32_t gpu_id, const char* name, float x, float y, float z) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}

static void tgfx2_shader_set_vec4(uint32_t gpu_id, const char* name, float x, float y, float z, float w) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

static void tgfx2_shader_set_mat4(uint32_t gpu_id, const char* name, const float* data, bool transpose) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, transpose ? GL_TRUE : GL_FALSE, data);
}

static void tgfx2_shader_set_mat4_array(uint32_t gpu_id, const char* name, const float* data, int count, bool transpose) {
    GLint loc = glGetUniformLocation(gpu_id, name);
    if (loc >= 0) glUniformMatrix4fv(loc, count, transpose ? GL_TRUE : GL_FALSE, data);
}

static void tgfx2_shader_set_block_binding(uint32_t gpu_id, const char* block_name, int binding_point) {
    GLuint block_index = glGetUniformBlockIndex(gpu_id, block_name);
    if (block_index != GL_INVALID_INDEX) {
        glUniformBlockBinding(gpu_id, block_index, binding_point);
    }
}

// ============================================================================
// Mesh operations
// ============================================================================

static uint32_t tgfx2_mesh_upload(
    const void* vertex_data,
    size_t vertex_count,
    const uint32_t* indices,
    size_t index_count,
    const tgfx_vertex_layout* layout,
    uint32_t* out_vbo,
    uint32_t* out_ebo
) {
    auto* dev = get_device();
    auto* gl_dev = get_gl_device();
    if (!dev || !gl_dev) return 0;

    size_t vbo_size = vertex_count * layout->stride;
    size_t ebo_size = index_count * sizeof(uint32_t);

    // Create VBO through tgfx2
    tgfx2::BufferDesc vbo_desc;
    vbo_desc.size = vbo_size;
    vbo_desc.usage = tgfx2::BufferUsage::Vertex;
    auto vbo_handle = dev->create_buffer(vbo_desc);
    if (!vbo_handle) return 0;

    dev->upload_buffer(vbo_handle,
        {reinterpret_cast<const uint8_t*>(vertex_data), vbo_size});

    // Create EBO through tgfx2
    tgfx2::BufferDesc ebo_desc;
    ebo_desc.size = ebo_size;
    ebo_desc.usage = tgfx2::BufferUsage::Index;
    auto ebo_handle = dev->create_buffer(ebo_desc);
    if (!ebo_handle) {
        dev->destroy(vbo_handle);
        return 0;
    }

    dev->upload_buffer(ebo_handle,
        {reinterpret_cast<const uint8_t*>(indices), ebo_size});

    // Extract GL IDs
    auto* gl_vbo = gl_dev->get_buffer(vbo_handle);
    auto* gl_ebo = gl_dev->get_buffer(ebo_handle);
    if (!gl_vbo || !gl_ebo) {
        dev->destroy(vbo_handle);
        dev->destroy(ebo_handle);
        return 0;
    }

    *out_vbo = gl_vbo->gl_id;
    *out_ebo = gl_ebo->gl_id;

    g_buffer_map[*out_vbo] = vbo_handle.id;
    g_buffer_map[*out_ebo] = ebo_handle.id;

    // Create VAO (still direct GL — tgfx2 doesn't manage VAOs)
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, *out_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *out_ebo);

    for (uint8_t i = 0; i < layout->attrib_count; i++) {
        const tgfx_vertex_attrib* a = &layout->attribs[i];
        glEnableVertexAttribArray(a->location);

        GLenum gl_type = GL_FLOAT;
        bool is_int = false;
        switch ((tgfx_attrib_type)a->type) {
            case TGFX_ATTRIB_FLOAT32: gl_type = GL_FLOAT; break;
            case TGFX_ATTRIB_INT32:   gl_type = GL_INT; is_int = true; break;
            case TGFX_ATTRIB_UINT32:  gl_type = GL_UNSIGNED_INT; is_int = true; break;
            case TGFX_ATTRIB_INT16:   gl_type = GL_SHORT; is_int = true; break;
            case TGFX_ATTRIB_UINT16:  gl_type = GL_UNSIGNED_SHORT; is_int = true; break;
            case TGFX_ATTRIB_INT8:    gl_type = GL_BYTE; is_int = true; break;
            case TGFX_ATTRIB_UINT8:   gl_type = GL_UNSIGNED_BYTE; is_int = true; break;
        }

        if (is_int) {
            glVertexAttribIPointer(
                a->location, a->size, gl_type,
                layout->stride, (const void*)(uintptr_t)a->offset
            );
        } else {
            glVertexAttribPointer(
                a->location, a->size, gl_type, GL_FALSE,
                layout->stride, (const void*)(uintptr_t)a->offset
            );
        }
    }

    glBindVertexArray(0);
    return vao;
}

static void tgfx2_mesh_draw(uint32_t vao, size_t index_count, tgfx_draw_mode mode) {
    GLenum gl_mode = (mode == TGFX_DRAW_LINES) ? GL_LINES : GL_TRIANGLES;
    glBindVertexArray(vao);
    glDrawElements(gl_mode, (GLsizei)index_count, GL_UNSIGNED_INT, nullptr);
}

static void tgfx2_mesh_delete(uint32_t vao) {
    glDeleteVertexArrays(1, &vao);
}

static uint32_t tgfx2_mesh_create_vao(const tgfx_vertex_layout* layout, uint32_t vbo, uint32_t ebo) {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    for (uint8_t i = 0; i < layout->attrib_count; i++) {
        const tgfx_vertex_attrib* a = &layout->attribs[i];
        glEnableVertexAttribArray(a->location);

        GLenum gl_type = GL_FLOAT;
        bool is_int = false;
        switch ((tgfx_attrib_type)a->type) {
            case TGFX_ATTRIB_FLOAT32: gl_type = GL_FLOAT; break;
            case TGFX_ATTRIB_INT32:   gl_type = GL_INT; is_int = true; break;
            case TGFX_ATTRIB_UINT32:  gl_type = GL_UNSIGNED_INT; is_int = true; break;
            case TGFX_ATTRIB_INT16:   gl_type = GL_SHORT; is_int = true; break;
            case TGFX_ATTRIB_UINT16:  gl_type = GL_UNSIGNED_SHORT; is_int = true; break;
            case TGFX_ATTRIB_INT8:    gl_type = GL_BYTE; is_int = true; break;
            case TGFX_ATTRIB_UINT8:   gl_type = GL_UNSIGNED_BYTE; is_int = true; break;
        }

        if (is_int) {
            glVertexAttribIPointer(
                a->location, a->size, gl_type,
                layout->stride, (const void*)(uintptr_t)a->offset
            );
        } else {
            glVertexAttribPointer(
                a->location, a->size, gl_type, GL_FALSE,
                layout->stride, (const void*)(uintptr_t)a->offset
            );
        }
    }

    glBindVertexArray(0);
    return vao;
}

static void tgfx2_buffer_delete(uint32_t buffer_id) {
    auto* dev = get_device();
    auto it = g_buffer_map.find(buffer_id);
    if (it != g_buffer_map.end() && dev) {
        dev->destroy(tgfx2::BufferHandle{it->second});
        g_buffer_map.erase(it);
    } else {
        // Fallback: not tracked by tgfx2, delete directly
        glDeleteBuffers(1, &buffer_id);
    }
}

// ============================================================================
// Registration
// ============================================================================

void tgfx2_gpu_ops_register(void) {
    if (!g_tgfx2_device) {
        tc_log_error("tgfx2_gpu_ops_register: device not set, call tgfx2_interop_set_device first");
        return;
    }

    static tgfx_gpu_ops ops = {};

    ops.texture_upload = tgfx2_texture_upload;
    ops.depth_texture_upload = tgfx2_depth_texture_upload;
    ops.texture_bind = tgfx2_texture_bind;
    ops.depth_texture_bind = tgfx2_depth_texture_bind;
    ops.texture_delete = tgfx2_texture_delete;

    ops.shader_compile = tgfx2_shader_compile;
    ops.shader_use = tgfx2_shader_use;
    ops.shader_delete = tgfx2_shader_delete;

    ops.shader_set_int = tgfx2_shader_set_int;
    ops.shader_set_float = tgfx2_shader_set_float;
    ops.shader_set_vec2 = tgfx2_shader_set_vec2;
    ops.shader_set_vec3 = tgfx2_shader_set_vec3;
    ops.shader_set_vec4 = tgfx2_shader_set_vec4;
    ops.shader_set_mat4 = tgfx2_shader_set_mat4;
    ops.shader_set_mat4_array = tgfx2_shader_set_mat4_array;
    ops.shader_set_block_binding = tgfx2_shader_set_block_binding;

    ops.mesh_upload = tgfx2_mesh_upload;
    ops.mesh_draw = tgfx2_mesh_draw;
    ops.mesh_delete = tgfx2_mesh_delete;
    ops.mesh_create_vao = tgfx2_mesh_create_vao;
    ops.buffer_delete = tgfx2_buffer_delete;

    ops.user_data = g_tgfx2_device;

    tgfx_gpu_set_ops(&ops);
}
