#pragma once

#include <glad/glad.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_render_device.hpp"

namespace tgfx {

// Internal GL resource types

struct GLBuffer {
    GLuint gl_id = 0;
    BufferDesc desc;
    GLenum target = GL_ARRAY_BUFFER;
    // When true the GL buffer object is owned externally (e.g. by legacy
    // tgfx mesh code) and must NOT be glDeleteBuffers'd when this handle
    // is destroyed. Set by register_external_buffer() during the Phase 2
    // migration so that tgfx2 passes can draw against legacy-owned VBOs
    // and EBOs without taking ownership of them.
    bool external = false;
};

struct GLTexture {
    GLuint gl_id = 0;
    TextureDesc desc;
    GLenum target = GL_TEXTURE_2D;
    // When true the GL texture object is owned externally (e.g. by legacy
    // tgfx code) and must NOT be glDeleteTextures'd when this handle is
    // destroyed. Used by register_external_texture() to wrap existing GL
    // textures as tgfx2 handles for interop during the Phase 2 migration.
    bool external = false;
};

struct GLSampler {
    GLuint gl_id = 0;
};

struct GLShaderModule {
    GLuint gl_shader = 0;
    ShaderStage stage;
};

struct GLPipeline {
    GLuint program = 0;
    PipelineDesc desc;
};

struct GLProgramKey {
    uint32_t vertex_shader = 0;
    uint32_t fragment_shader = 0;
    uint32_t geometry_shader = 0;

    bool operator==(const GLProgramKey& o) const {
        return vertex_shader == o.vertex_shader &&
               fragment_shader == o.fragment_shader &&
               geometry_shader == o.geometry_shader;
    }
};

struct GLSharedProgram {
    GLuint program = 0;
    uint32_t ref_count = 0;
};

struct GLProgramKeyHash {
    size_t operator()(const GLProgramKey& k) const noexcept {
        size_t h = std::hash<uint32_t>{}(k.vertex_shader);
        h ^= std::hash<uint32_t>{}(k.fragment_shader) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.geometry_shader) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct GLResourceSet {
    ResourceSetDesc desc;
};

// Handle pool: maps uint32_t id -> T
template<typename T>
class HandlePool {
public:
    uint32_t add(T&& resource) {
        uint32_t id = next_id_++;
        pool_.emplace(id, std::move(resource));
        return id;
    }

    T* get(uint32_t id) {
        auto it = pool_.find(id);
        return (it != pool_.end()) ? &it->second : nullptr;
    }

    const T* get_const(uint32_t id) const {
        auto it = pool_.find(id);
        return (it != pool_.end()) ? &it->second : nullptr;
    }

    bool remove(uint32_t id) {
        return pool_.erase(id) > 0;
    }

    auto begin() { return pool_.begin(); }
    auto end() { return pool_.end(); }

private:
    std::unordered_map<uint32_t, T> pool_;
    uint32_t next_id_ = 1;
};

class TGFX2_API OpenGLRenderDevice : public IRenderDevice {
public:
    OpenGLRenderDevice();
    ~OpenGLRenderDevice() override;

    BackendCapabilities capabilities() const override;
    void wait_idle() override;

    BufferHandle create_buffer(const BufferDesc& desc) override;
    TextureHandle create_texture(const TextureDesc& desc) override;
    SamplerHandle create_sampler(const SamplerDesc& desc) override;
    ShaderHandle create_shader(const ShaderDesc& desc) override;
    PipelineHandle create_pipeline(const PipelineDesc& desc) override;
    ResourceSetHandle create_resource_set(const ResourceSetDesc& desc) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(ResourceSetHandle handle) override;

    void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) override;
    void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) override;
    void upload_texture_region(TextureHandle dst,
                               uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h,
                               std::span<const uint8_t> data,
                               uint32_t mip = 0) override;
    void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) override;

    TextureDesc texture_desc(TextureHandle handle) const override;

    std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) override;
    void submit(ICommandList& cmd) override;
    void present() override;

    // Internal access for command list
    GLBuffer* get_buffer(BufferHandle h) { return buffers_.get(h.id); }
    GLTexture* get_texture(TextureHandle h) { return textures_.get(h.id); }
    GLSampler* get_sampler(SamplerHandle h) { return samplers_.get(h.id); }
    GLShaderModule* get_shader(ShaderHandle h) { return shaders_.get(h.id); }
    GLPipeline* get_pipeline(PipelineHandle h) { return pipelines_.get(h.id); }
    GLResourceSet* get_resource_set(ResourceSetHandle h) { return resource_sets_.get(h.id); }

    // Get or create an FBO for the given attachment combination.
    // Key: sorted list of (attachment_point, texture_gl_id).
    // Returns GL FBO id. FBO 0 = default framebuffer (when no textures specified).
    GLuint get_or_create_fbo(const RenderPassDesc& pass);

    // Drop every cached GL FBO.
    //
    // TEMPORARY — exists for the duration of the tgfx2 migration only.
    // It papers over an ownership inconsistency: during Phase 2, render
    // targets are owned by the legacy FBOPool, and tgfx2 passes borrow
    // them for the duration of a single frame via register_external_texture
    // (see termin-render/src/tgfx2_bridge.cpp::wrap_fbo_color_as_tgfx2).
    // At end-of-frame those borrows are released; the fbo_cache_ entries
    // built around them are no longer safe because legacy code is free
    // to mutate the underlying GL attachment state between frames without
    // going through tgfx2. Rebuilding FBOs each frame costs 1–3 FBO
    // allocations per frame — negligible.
    //
    // Remove this once Phase 3 of migration-tgfx2.md (FBOPool moves to
    // tgfx2-backed allocation) ships: render targets will then be tgfx2
    // resources from birth, there is no bridge, cache entries live as
    // long as their texture handles live, and FBOs can be cached safely
    // across frames.
    //
    // Called from RenderContext2::end_frame().
    void invalidate_fbo_cache();

    // IRenderDevice readback / external-target / interop overrides.
    // Documentation lives on the base class — these are the OpenGL
    // implementations of the backend-neutral virtual interface.
    bool read_pixel_rgba8(TextureHandle tex, int x, int y, float out_rgba[4]) override;
    bool read_texture_rgba_float(TextureHandle tex, float* out) override;
    bool read_texture_depth_float(TextureHandle tex, float* out) override;

    void blit_to_external_target(
        uintptr_t dst,
        TextureHandle src_color,
        int src_x, int src_y, int src_w, int src_h,
        int dst_x, int dst_y, int dst_w, int dst_h) override;

    void clear_external_target(
        uintptr_t dst,
        float r, float g, float b, float a,
        float depth,
        int viewport_x, int viewport_y,
        int viewport_w, int viewport_h) override;

    void reset_state() override;
    void flush() override;
    void finish() override;

    uintptr_t native_texture_handle(TextureHandle handle) const override;

    TextureHandle register_external_texture(
        uintptr_t native_handle, const TextureDesc& desc) override;
    BufferHandle register_external_buffer(
        uintptr_t native_handle, const BufferDesc& desc) override;

    // GL-typed convenience overloads. Equivalent to the uintptr_t
    // versions above but spare the `static_cast<uintptr_t>(glid)` at
    // callsites that speak raw GL. Kept for GL-only code (picking,
    // tcplot, legacy tc_mesh bridge).
    GLuint gl_texture_id(TextureHandle handle) {
        return static_cast<GLuint>(native_texture_handle(handle));
    }
    TextureHandle register_external_texture(GLuint gl_id, const TextureDesc& desc) {
        return register_external_texture(static_cast<uintptr_t>(gl_id), desc);
    }
    BufferHandle register_external_buffer(GLuint gl_id, const BufferDesc& desc) {
        return register_external_buffer(static_cast<uintptr_t>(gl_id), desc);
    }

    // --- Push constants ring buffer ---
    //
    // OpenGL has no push constants; we emulate them via a single large
    // GL_UNIFORM_BUFFER used as a ring. Each set_push_constants() writes
    // a new aligned chunk and the next draw_indexed() binds it with
    // glBindBufferRange at TGFX2_PUSH_CONSTANTS_BINDING.
    //
    // The ring is reset once per frame when OpenGLCommandList::begin()
    // is called. On overflow within a single frame we orphan the
    // storage (glBufferData NULL) and restart — simple and stall-free
    // under normal draw counts. Max ~4096 pushes per frame at 256-byte
    // alignment, 1 MB ring. Larger scenes will wrap and re-orphan.

    // Allocate a push-constants slot and copy `size` bytes from `data`
    // into it. Returns the byte offset inside the ring buffer. The
    // caller must bind the range at offset [offset, size] to
    // TGFX2_PUSH_CONSTANTS_BINDING before the next draw. Returns 0 and
    // logs on failure (caller should skip the draw).
    GLintptr push_constants_write(const void* data, uint32_t size);

    // Current ring buffer object (0 if not yet allocated).
    GLuint push_constants_ring_buffer() const { return push_ring_buf_; }

    // Reset the ring offset to 0. Called once per frame from
    // OpenGLCommandList::begin().
    void push_constants_reset_frame();

private:
    GLuint acquire_program(const PipelineDesc& desc);
    void release_program(GLuint program);

    HandlePool<GLBuffer> buffers_;
    HandlePool<GLTexture> textures_;
    HandlePool<GLSampler> samplers_;
    HandlePool<GLShaderModule> shaders_;
    HandlePool<GLPipeline> pipelines_;
    HandlePool<GLResourceSet> resource_sets_;

    BackendCapabilities caps_;
    void query_capabilities();

    // FBO cache: key = sorted vector of (GL attachment enum, GL texture id)
    using FBOKey = std::vector<std::pair<GLenum, GLuint>>;
    std::map<FBOKey, GLuint> fbo_cache_;

    // Push constants ring buffer state (see push_constants_write).
    GLuint     push_ring_buf_       = 0;
    GLsizeiptr push_ring_size_      = 1 << 20;   // 1 MB
    GLintptr   push_ring_offset_    = 0;
    GLint      push_ring_alignment_ = 256;       // queried from GL at first use
    bool       push_ring_initialized_ = false;

    std::unordered_map<GLProgramKey, GLSharedProgram, GLProgramKeyHash> program_cache_;
    std::unordered_map<GLuint, GLProgramKey> program_to_key_;

    void ensure_push_ring();
};

} // namespace tgfx
