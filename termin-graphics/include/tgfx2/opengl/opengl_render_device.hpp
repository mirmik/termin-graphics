#pragma once

#include <glad/glad.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_render_device.hpp"

namespace tgfx2 {

// Internal GL resource types

struct GLBuffer {
    GLuint gl_id = 0;
    BufferDesc desc;
    GLenum target = GL_ARRAY_BUFFER;
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
    void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) override;

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

    // Wrap an externally-owned GL texture object as a tgfx2::TextureHandle.
    //
    // Use this to interop with legacy tgfx FBOs during Phase 2 migration:
    // pass the GL texture id from a legacy color/depth attachment together
    // with the texture's logical description. The returned TextureHandle
    // can be used with RenderContext2::begin_pass() and bind_texture() just
    // like any tgfx2 texture. When the handle is destroyed the underlying
    // GL object is NOT deleted — ownership stays with the original creator.
    TextureHandle register_external_texture(GLuint gl_id, const TextureDesc& desc);

private:
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
};

} // namespace tgfx2
