// tgfx2_gpu_ops.cpp - tgfx_gpu_ops vtable implementation backed by tgfx::IRenderDevice
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

static tgfx::IRenderDevice* get_device() {
    return static_cast<tgfx::IRenderDevice*>(g_tgfx2_device);
}

static tgfx::OpenGLRenderDevice* get_gl_device() {
    return static_cast<tgfx::OpenGLRenderDevice*>(g_tgfx2_device);
}

// Map channels to tgfx2 pixel format
static tgfx::PixelFormat channels_to_format(int channels) {
    switch (channels) {
        case 1: return tgfx::PixelFormat::R8_UNorm;
        case 2: return tgfx::PixelFormat::RG8_UNorm;
        case 3: return tgfx::PixelFormat::RGB8_UNorm;
        case 4: return tgfx::PixelFormat::RGBA8_UNorm;
        default: return tgfx::PixelFormat::RGBA8_UNorm;
    }
}

// Track tgfx2 handle for a given GL ID so we can destroy properly
// Key: gl_id, Value: tgfx2 handle id
// We maintain separate maps per resource type.
static std::unordered_map<uint32_t, uint32_t> g_texture_map;   // gl_id -> TextureHandle.id
static std::unordered_map<uint32_t, uint32_t> g_sampler_map;   // gl_id -> SamplerHandle.id
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

    tgfx::TextureDesc desc;
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = channels_to_format(channels);
    desc.usage = tgfx::TextureUsage::Sampled;
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

    tgfx::TextureDesc desc;
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = tgfx::PixelFormat::D24_UNorm_S8_UInt;
    desc.usage = tgfx::TextureUsage::DepthStencilAttachment | tgfx::TextureUsage::Sampled;
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
        dev->destroy(tgfx::TextureHandle{it->second});
        g_texture_map.erase(it);
    } else {
        // Stage 6 transition: textures created via the legacy gpu_ops
        // (before RenderEngine::ensure_tgfx2 swapped the vtable) are
        // not tracked by tgfx2. Delete directly so they don't leak.
        GLuint id = gpu_id;
        glDeleteTextures(1, &id);
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
    tgfx::BufferDesc vbo_desc;
    vbo_desc.size = vbo_size;
    vbo_desc.usage = tgfx::BufferUsage::Vertex;
    auto vbo_handle = dev->create_buffer(vbo_desc);
    if (!vbo_handle) return 0;

    dev->upload_buffer(vbo_handle,
        {reinterpret_cast<const uint8_t*>(vertex_data), vbo_size});

    // Create EBO through tgfx2
    tgfx::BufferDesc ebo_desc;
    ebo_desc.size = ebo_size;
    ebo_desc.usage = tgfx::BufferUsage::Index;
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
        dev->destroy(tgfx::BufferHandle{it->second});
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


    ops.mesh_upload = tgfx2_mesh_upload;
    ops.mesh_draw = tgfx2_mesh_draw;
    ops.mesh_delete = tgfx2_mesh_delete;
    ops.mesh_create_vao = tgfx2_mesh_create_vao;
    ops.buffer_delete = tgfx2_buffer_delete;

    ops.user_data = g_tgfx2_device;

    tgfx_gpu_set_ops(&ops);
}
