#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/handles.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/capabilities.hpp"
#include "tgfx2/i_command_list.hpp"

namespace tgfx {

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    // Backend identity — lets callers branch on GL-only vs Vulkan-only
    // host integration (FBO invalidation, legacy gpu_ops interop, …)
    // without a dynamic_cast to the concrete device class.
    virtual BackendType backend_type() const = 0;

    virtual BackendCapabilities capabilities() const = 0;
    virtual void wait_idle() = 0;

    // Drop any cached backend-side render target objects (OpenGL FBOs,
    // Vulkan VkFramebuffers) keyed on texture identity. Called after
    // host code destroys + re-creates textures whose gl_id / VkImage may
    // be reused for attachments of a different size or format. Default
    // is a no-op for backends that don't cache render targets.
    virtual void invalidate_render_target_cache() {}

    // --- Resource creation ---
    virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;
    virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    virtual SamplerHandle create_sampler(const SamplerDesc& desc) = 0;
    virtual ShaderHandle create_shader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
    virtual ResourceSetHandle create_resource_set(const ResourceSetDesc& desc) = 0;

    // --- Resource destruction ---
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;
    virtual void destroy(ResourceSetHandle handle) = 0;

    // --- Data upload ---
    virtual void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) = 0;
    virtual void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) = 0;

    // Upload a rectangular sub-region of a texture. `data` is a tightly
    // packed `w * h * bytes_per_pixel` buffer in the texture's format.
    // Useful for incremental overlay updates (e.g. paint strokes).
    virtual void upload_texture_region(TextureHandle dst,
                                       uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h,
                                       std::span<const uint8_t> data,
                                       uint32_t mip = 0) = 0;

    // --- Data readback ---
    virtual void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) = 0;

    // --- Introspection ---
    // Query the descriptor (width/height/format/...) that a texture was
    // created or registered with. Returns a default-initialised
    // TextureDesc if the handle is invalid or unknown to this device.
    virtual TextureDesc texture_desc(TextureHandle handle) const = 0;

    // --- Command submission ---
    virtual std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) = 0;
    virtual void submit(ICommandList& cmd) = 0;

    // --- Present / sync ---
    virtual void present() = 0;

    // --- External resource interop ---
    // Wrap a backend-native GPU object (created by host code outside
    // tgfx2) as a non-owning tgfx2 handle. `native_handle` is
    // backend-specific: for OpenGL it is a GLuint texture/buffer
    // object id, for Vulkan a VkImage/VkBuffer cast to uintptr_t.
    // Destroying the returned handle does NOT free the underlying
    // GPU object. Backends that don't support external wrapping
    // throw std::runtime_error.
    virtual TextureHandle register_external_texture(
        uintptr_t native_handle, const TextureDesc& desc) {
        (void)native_handle; (void)desc;
        throw std::runtime_error("register_external_texture: not supported on this backend");
    }
    virtual BufferHandle register_external_buffer(
        uintptr_t native_handle, const BufferDesc& desc) {
        (void)native_handle; (void)desc;
        throw std::runtime_error("register_external_buffer: not supported on this backend");
    }

    // --- Transient vertex ring (optional) ---
    // A persistent vertex buffer the backend can sub-upload small draw
    // streams into (immediate-mode rects, debug lines). Lets
    // RenderContext2::draw_immediate_* skip per-draw create_buffer /
    // upload_buffer / destroy, which is ~free on Vulkan but costs
    // glGenBuffers + glBufferData + glBufferSubData + glDeleteBuffers
    // on OpenGL (the delete happens later via deferred_destroy_buffers_,
    // but the GL-driver still pays the per-alloc hit).
    //
    // Returns UINT64_MAX → caller falls back to create_buffer +
    // upload_buffer (Vulkan keeps doing that; it's fast there).
    // Non-max offset → use `transient_vertex_buffer()` with that
    // offset.  Ring wraps with an orphaning glBufferData() so there's
    // no stall on overflow.
    virtual BufferHandle transient_vertex_buffer() { return {}; }

    virtual uint64_t transient_vertex_write(
        const void* data, uint32_t size) {
        (void)data; (void)size;
        return UINT64_MAX;
    }

    // Return the backend-native object id backing a tgfx2 handle.
    // For OpenGL: GLuint texture id. For Vulkan: VkImage as uintptr_t.
    // Returns 0 on unknown / invalid handle.
    virtual uintptr_t native_texture_handle(TextureHandle handle) const {
        (void)handle;
        return 0;
    }

    // --- External target presentation ---
    // Copy a tgfx2 color texture onto a host-owned target (default
    // framebuffer / swapchain image). `dst` is backend-specific
    // (OpenGL: GL FBO id, 0 = default; Vulkan: swapchain image
    // index). Throws on backends without external-target support.
    virtual void blit_to_external_target(
        uintptr_t dst,
        TextureHandle src_color,
        int src_x, int src_y, int src_w, int src_h,
        int dst_x, int dst_y, int dst_w, int dst_h) {
        (void)dst; (void)src_color;
        (void)src_x; (void)src_y; (void)src_w; (void)src_h;
        (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
        throw std::runtime_error("blit_to_external_target: not supported on this backend");
    }

    // Bind a host-owned target and clear it to the given color /
    // depth, setting the viewport in the process.
    virtual void clear_external_target(
        uintptr_t dst,
        float r, float g, float b, float a,
        float depth,
        int viewport_x, int viewport_y,
        int viewport_w, int viewport_h) {
        (void)dst; (void)r; (void)g; (void)b; (void)a;
        (void)depth;
        (void)viewport_x; (void)viewport_y;
        (void)viewport_w; (void)viewport_h;
        throw std::runtime_error("clear_external_target: not supported on this backend");
    }

    // Copy a source tgfx2 color texture into a rect of a destination
    // tgfx2 color texture (both owned by this device). Backend-neutral
    // counterpart of `blit_to_external_target` — used by render
    // surfaces that store their composite target as a TextureHandle
    // instead of a raw FBO id (so both OpenGL and Vulkan can drive them).
    // Filtering: linear.
    virtual void blit_to_texture(
        TextureHandle dst,
        TextureHandle src,
        int src_x, int src_y, int src_w, int src_h,
        int dst_x, int dst_y, int dst_w, int dst_h) {
        (void)dst; (void)src;
        (void)src_x; (void)src_y; (void)src_w; (void)src_h;
        (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
        throw std::runtime_error("blit_to_texture: not supported on this backend");
    }

    // Clear a tgfx2 color texture to `(r,g,b,a)` inside the given
    // viewport rect (scissor). Backend-neutral counterpart of
    // `clear_external_target`; the destination is a TextureHandle.
    virtual void clear_texture(
        TextureHandle dst,
        float r, float g, float b, float a,
        int viewport_x, int viewport_y,
        int viewport_w, int viewport_h) {
        (void)dst; (void)r; (void)g; (void)b; (void)a;
        (void)viewport_x; (void)viewport_y;
        (void)viewport_w; (void)viewport_h;
        throw std::runtime_error("clear_texture: not supported on this backend");
    }

    // --- Readback helpers ---
    // Read a single RGBA8 pixel from a color texture into `out_rgba`
    // (four floats in [0,1]). Returns false on failure. Used by the
    // editor picking code.
    virtual bool read_pixel_rgba8(TextureHandle tex, int x, int y, float out_rgba[4]) {
        (void)tex; (void)x; (void)y; (void)out_rgba;
        return false;
    }
    // Read a full color texture as tightly-packed RGBA float32 into
    // `out` (>= width*height*4 floats). Used by the framegraph
    // debugger's HDR stats.
    virtual bool read_texture_rgba_float(TextureHandle tex, float* out) {
        (void)tex; (void)out;
        return false;
    }
    // Read a full depth texture as tightly-packed float32 depth values
    // into `out` (>= width*height floats). Used by the framegraph
    // debugger's depth preview.
    virtual bool read_texture_depth_float(TextureHandle tex, float* out) {
        (void)tex; (void)out;
        return false;
    }

    // --- Dynamic-offset ring UBO ------------------------------------
    //
    // A backend-managed host-visible ring buffer used by the
    // `RenderContext2::bind_uniform_buffer_ring()` API. Pass code writes
    // per-draw UBO data into the ring via `ring_ubo_write(data, size)`
    // which returns an aligned byte offset; the offset is then attached
    // to a normal UniformBuffer binding on `ring_ubo_handle()`, letting
    // one shared backing store replace a per-draw `vkCreateBuffer` +
    // `vkUpdateDescriptorSets` pair.
    //
    // Default returns on backends without a ring implementation are safe
    // ({}/0) — callers should treat `ring_ubo_handle().id == 0` as "no
    // ring, fall back to the classic per-draw UBO path".
    virtual BufferHandle ring_ubo_handle() const { return {}; }
    virtual uint32_t ring_ubo_write(const void* /*data*/, uint32_t /*size*/) {
        return 0;
    }
    virtual uint32_t ubo_alignment() const { return 1; }

    // --- Legacy state / sync helpers ---
    // Restore the canonical render-state baseline between frame-graph
    // passes. On OpenGL: glEnable(DEPTH_TEST)/glDisable(BLEND)/etc.
    // On backends with explicit state (Vulkan), a no-op.
    virtual void reset_state() {}
    // Flush / finish — no-ops on backends with explicit command
    // submission. On OpenGL: glFlush / glFinish.
    virtual void flush() {}
    virtual void finish() {}
};

} // namespace tgfx
