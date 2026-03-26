#pragma once

#include <array>
#include <memory>
#include <optional>
#include <cstdint>

#include "tgfx/tgfx_api.h"
#include "tgfx/render_state.hpp"
#include "tgfx/handles.hpp"
#include "tgfx/types.hpp"

extern "C" {
#include "tgfx/tgfx_types.h"
}

namespace termin {

// Abstract graphics backend interface.
// Provides GPU resource management and render state control.
class TGFX_API GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;

    // --- Initialization ---
    virtual void ensure_ready() = 0;

    // --- Viewport ---
    virtual void set_viewport(int x, int y, int width, int height) = 0;
    virtual void enable_scissor(int x, int y, int width, int height) = 0;
    virtual void disable_scissor() = 0;

    // --- Clear ---
    virtual void clear_color_depth(float r, float g, float b, float a) = 0;
    virtual void clear_color(float r, float g, float b, float a) = 0;
    virtual void clear_depth(float value = 1.0f) = 0;

    void clear_color_depth(const Color4& c) { clear_color_depth(c.r, c.g, c.b, c.a); }
    void clear_color(const Color4& c) { clear_color(c.r, c.g, c.b, c.a); }

    // --- Color mask ---
    virtual void set_color_mask(bool r, bool g, bool b, bool a) = 0;

    // --- Depth ---
    virtual void set_depth_test(bool enabled) = 0;
    virtual void set_depth_mask(bool enabled) = 0;
    virtual void set_depth_func(DepthFunc func) = 0;

    // --- Culling ---
    virtual void set_cull_face(bool enabled) = 0;

    // --- Blending ---
    virtual void set_blend(bool enabled) = 0;
    virtual void set_blend_func(BlendFactor src, BlendFactor dst) = 0;

    // --- Polygon mode ---
    virtual void set_polygon_mode(PolygonMode mode) = 0;

    // --- State management ---
    virtual void reset_state() = 0;
    virtual void apply_render_state(const RenderState& state) = 0;

    // Reset full GL state (textures, shader, etc.)
    virtual void reset_gl_state() = 0;

    // --- Resource creation ---
    virtual ShaderHandlePtr create_shader(
        const char* vertex_source,
        const char* fragment_source,
        const char* geometry_source = nullptr
    ) = 0;

    virtual GPUMeshHandlePtr create_mesh(
        const void* vertex_data,
        size_t vertex_count,
        const uint32_t* indices,
        size_t index_count,
        const tgfx_vertex_layout* layout,
        DrawMode mode = DrawMode::Triangles
    ) = 0;

    virtual GPUTextureHandlePtr create_texture(
        const uint8_t* data,
        int width,
        int height,
        int channels = 4,
        bool mipmap = true,
        bool clamp = false
    ) = 0;

    GPUTextureHandlePtr create_texture(
        const uint8_t* data,
        Size2i size,
        int channels = 4,
        bool mipmap = true,
        bool clamp = false
    ) {
        return create_texture(data, size.width, size.height, channels, mipmap, clamp);
    }

    virtual FramebufferHandlePtr create_framebuffer(int width, int height, int samples = 1, const std::string& format = "", TextureFilter filter = TextureFilter::LINEAR) = 0;
    virtual FramebufferHandlePtr create_shadow_framebuffer(int width, int height) = 0;
    virtual UniformBufferHandlePtr create_uniform_buffer(size_t size) = 0;

    virtual FramebufferHandlePtr create_external_framebuffer(uint32_t fbo_id, int width, int height) = 0;

    FramebufferHandlePtr create_framebuffer(Size2i size, int samples = 1, const std::string& format = "") {
        return create_framebuffer(size.width, size.height, samples, format);
    }
    FramebufferHandlePtr create_shadow_framebuffer(Size2i size) {
        return create_shadow_framebuffer(size.width, size.height);
    }

    // --- Framebuffer operations ---
    virtual void bind_framebuffer(FramebufferHandle* fbo) = 0;
    virtual void bind_framebuffer_id(uint32_t fbo_id) = 0;
    virtual void blit_framebuffer(
        FramebufferHandle* src,
        FramebufferHandle* dst,
        int src_x0, int src_y0, int src_x1, int src_y1,
        int dst_x0, int dst_y0, int dst_x1, int dst_y1,
        bool blit_color = true,
        bool blit_depth = false
    ) = 0;

    void blit_framebuffer(FramebufferHandle* src, FramebufferHandle* dst,
                          const Rect2i& src_rect, const Rect2i& dst_rect,
                          bool blit_color = true, bool blit_depth = false) {
        blit_framebuffer(src, dst,
            src_rect.x0, src_rect.y0, src_rect.x1, src_rect.y1,
            dst_rect.x0, dst_rect.y0, dst_rect.x1, dst_rect.y1,
            blit_color, blit_depth);
    }

    virtual void blit_framebuffer_to_id(
        FramebufferHandle& src,
        uint32_t dst_fbo_id,
        const Rect2i& src_rect,
        const Rect2i& dst_rect,
        bool blit_color = true,
        bool blit_depth = false
    ) = 0;

    // --- Read operations ---
    virtual std::array<float, 4> read_pixel(FramebufferHandle* fbo, int x, int y) = 0;
    virtual std::optional<float> read_depth_pixel(FramebufferHandle* fbo, int x, int y) = 0;
    virtual bool read_depth_buffer(FramebufferHandle* fbo, float* out_data) = 0;
    virtual bool read_color_buffer_float(FramebufferHandle* fbo, float* out_data) = 0;

    // --- UI drawing (for immediate mode) ---
    virtual void draw_ui_vertices(const float* vertices, int vertex_count) = 0;
    virtual void draw_ui_textured_quad() = 0;

    // --- Immediate mode rendering ---
    // Vertices: x, y, z, r, g, b, a (7 floats per vertex)
    virtual void draw_immediate_lines(const float* vertices, int vertex_count) = 0;
    virtual void draw_immediate_triangles(const float* vertices, int vertex_count) = 0;

    // --- Debug ---
    virtual bool check_gl_error(const char* location) = 0;
    virtual void clear_gl_errors() = 0;
    virtual uint32_t get_bound_texture(int unit) = 0;

    // --- Synchronization ---
    virtual void flush() = 0;
    virtual void finish() = 0;

    // --- GPU Timer Queries ---
    virtual void begin_gpu_query(const char* name) = 0;
    virtual void end_gpu_query() = 0;
    virtual double get_gpu_query_ms(const char* name) = 0;
    virtual void sync_gpu_queries() = 0;
};

using GraphicsBackendPtr = std::unique_ptr<GraphicsBackend>;

} // namespace termin
