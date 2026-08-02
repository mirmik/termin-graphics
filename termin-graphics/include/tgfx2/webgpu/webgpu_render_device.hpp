#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "tgfx2/i_render_device.hpp"

namespace tgfx {

class WebGpuCommandList;

struct WebGpuDeviceRequest {
    const char* canvas_selector = "#termin-canvas";
    uint32_t width = 1;
    uint32_t height = 1;
};

using WebGpuDeviceCallback = std::function<void(
    std::unique_ptr<class WebGpuRenderDevice> device,
    std::string error)>;

struct WebGpuLayoutEntry {
    std::string name;
    ShaderResourceKind kind = ShaderResourceKind::None;
    uint32_t stage_mask = 0;
    uint32_t binding = 0;
    uint32_t size = 0;
    bool has_sampler_binding = false;
    uint32_t sampler_binding = 0;
};

template<typename T>
class WebGpuHandlePool {
public:
    uint32_t add(T resource) {
        const uint32_t id = next_id_++;
        resources_.emplace(id, std::move(resource));
        return id;
    }
    T* get(uint32_t id) {
        auto it = resources_.find(id);
        return it == resources_.end() ? nullptr : &it->second;
    }
    const T* get(uint32_t id) const {
        auto it = resources_.find(id);
        return it == resources_.end() ? nullptr : &it->second;
    }
    bool remove(uint32_t id) { return resources_.erase(id) != 0; }

private:
    std::unordered_map<uint32_t, T> resources_;
    uint32_t next_id_ = 1;
};

struct WebGpuBuffer {
    wgpu::Buffer object;
    BufferDesc desc;
};

struct WebGpuTexture {
    wgpu::Texture object;
    wgpu::TextureView view;
    TextureDesc desc;
    bool surface_texture = false;
};

struct WebGpuSampler { wgpu::Sampler object; };

struct WebGpuShader {
    wgpu::ShaderModule object;
    ShaderDesc desc;
    std::vector<WebGpuLayoutEntry> layout;
};

struct WebGpuPipeline {
    wgpu::RenderPipeline object;
    wgpu::BindGroupLayout bind_group_layout;
    PipelineDesc desc;
    uintptr_t layout_token = 0;
    std::vector<WebGpuLayoutEntry> layout;
};

struct WebGpuResourceSet {
    wgpu::BindGroup object;
    uintptr_t layout_token = 0;
};

struct WebGpuErrorState {
    bool failed = false;
    std::string message;
};

class WebGpuRenderDevice final : public IRenderDevice {
public:
    static void request_async(
        const WebGpuDeviceRequest& request,
        WebGpuDeviceCallback callback);

    ~WebGpuRenderDevice() override;

    BackendType backend_type() const override { return BackendType::WebGPU; }
    BackendCapabilities capabilities() const override { return caps_; }
    void wait_idle() override;

    BufferHandle create_buffer(const BufferDesc& desc) override;
    TextureHandle create_texture(const TextureDesc& desc) override;
    SamplerHandle create_sampler(const SamplerDesc& desc) override;
    ShaderHandle create_shader(const ShaderDesc& desc) override;
    PipelineHandle create_pipeline(const PipelineDesc& desc) override;
    ResourceSetHandle create_bound_resource_set(
        const BoundResourceSetDesc& desc) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(ResourceSetHandle handle) override;

    void upload_buffer(BufferHandle dst, std::span<const uint8_t> data,
                       uint64_t offset = 0) override;
    void upload_texture(TextureHandle dst, std::span<const uint8_t> data,
                        uint32_t mip = 0) override;
    void upload_texture_region(TextureHandle dst, uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height,
                               std::span<const uint8_t> data,
                               uint32_t mip = 0) override;
    void read_buffer(BufferHandle src, std::span<uint8_t> data,
                     uint64_t offset = 0) override;
    void blit_to_texture(
        TextureHandle dst,
        TextureHandle src,
        termin::Bounds2i src_rect,
        termin::Bounds2i dst_rect) override;
    void clear_texture(
        TextureHandle dst,
        termin::Color4 color,
        termin::Bounds2i viewport) override;

    uintptr_t pipeline_resource_layout_token(
        PipelineHandle pipeline) const override;
    TextureDesc texture_desc(TextureHandle handle) const override;
    std::unique_ptr<ICommandList> create_command_list(
        QueueType queue = QueueType::Graphics) override;
    void submit(ICommandList& cmd) override;
    void present() override;

    bool ensure_tc_shader(
        tc_shader* shader,
        ShaderHandle* out_vs,
        ShaderHandle* out_fs) override;
    void invalidate_tc_shader_cache(uint32_t pool_index) override;
    TextureHandle ensure_tc_texture(tc_texture* texture) override;
    void invalidate_tc_texture_cache(uint32_t pool_index) override;
    std::pair<BufferHandle, BufferHandle> ensure_tc_mesh(tc_mesh* mesh) override;
    void invalidate_tc_mesh_cache(uint32_t pool_index) override;

    void configure_surface(uint32_t width, uint32_t height);
    TextureHandle acquire_surface_texture();
    PixelFormat surface_pixel_format() const;
    bool has_device_error() const { return error_state_->failed; }
    const std::string& device_error_message() const { return error_state_->message; }

private:
    WebGpuRenderDevice(wgpu::Instance instance, wgpu::Adapter adapter,
                       wgpu::Device device, wgpu::Surface surface,
                       uint32_t width, uint32_t height,
                       std::shared_ptr<WebGpuErrorState> error_state);

    friend class WebGpuCommandList;

    // Must outlive device_: Dawn callbacks retain this raw userdata pointer
    // through device destruction.
    std::shared_ptr<WebGpuErrorState> error_state_;
    wgpu::Instance instance_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    wgpu::Surface surface_;
    wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::BGRA8Unorm;
    BackendCapabilities caps_;
    uint32_t surface_width_ = 1;
    uint32_t surface_height_ = 1;
    TextureHandle acquired_surface_texture_;

    WebGpuHandlePool<WebGpuBuffer> buffers_;
    WebGpuHandlePool<WebGpuTexture> textures_;
    WebGpuHandlePool<WebGpuSampler> samplers_;
    WebGpuHandlePool<WebGpuShader> shaders_;
    WebGpuHandlePool<WebGpuPipeline> pipelines_;
    WebGpuHandlePool<WebGpuResourceSet> resource_sets_;
    SamplerHandle default_sampler_;

    struct CachedTcShaderEntry {
        ShaderHandle vs;
        ShaderHandle fs;
        uint32_t version = 0;
        uint64_t resolver_revision = 0;
        bool has_vs = false;
    };
    std::unordered_map<uint32_t, CachedTcShaderEntry> tc_shader_cache_;

    struct CachedTcTextureEntry {
        TextureHandle handle;
        uint32_t version = 0;
    };
    std::unordered_map<uint32_t, CachedTcTextureEntry> tc_texture_cache_;

    struct CachedTcMeshEntry {
        BufferHandle vbo;
        BufferHandle ebo;
        uint32_t version = 0;
    };
    std::unordered_map<uint32_t, CachedTcMeshEntry> tc_mesh_cache_;
};

} // namespace tgfx
