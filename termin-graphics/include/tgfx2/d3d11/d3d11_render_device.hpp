#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    struct D3D11Buffer {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        BufferDesc desc;
    };

    struct D3D11Texture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        TextureDesc desc;
    };

    struct D3D11Sampler {
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };

    struct D3D11ShaderModule {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometry_shader;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> compute_shader;
        ShaderStage stage = ShaderStage::Vertex;
        std::string debug_name;
        std::vector<uint8_t> bytecode;
    };

    struct D3D11Pipeline {
        PipelineDesc desc;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_state;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_state;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state;
    };

    struct D3D11ResourceSet {
        BoundResourceSetStorage bound_resources;
    };

    template <typename T> class D3D11HandlePool {
    public:
        uint32_t add(T&& resource) {
            const uint32_t id = next_id_++;
            pool_.emplace(id, std::move(resource));
            return id;
        }

        T* get(uint32_t id) {
            auto it = pool_.find(id);
            return it == pool_.end() ? nullptr : &it->second;
        }

        const T* get_const(uint32_t id) const {
            auto it = pool_.find(id);
            return it == pool_.end() ? nullptr : &it->second;
        }

        void remove(uint32_t id) {
            pool_.erase(id);
        }

    private:
        std::unordered_map<uint32_t, T> pool_;
        uint32_t next_id_ = 1;
    };

    class TGFX2_TYPE_API D3D11RenderDevice : public IRenderDevice {
    public:
        D3D11RenderDevice();
        ~D3D11RenderDevice() override;

        BackendType backend_type() const override {
            return BackendType::D3D11;
        }
        BackendCapabilities capabilities() const override {
            return caps_;
        }
        void wait_idle() override;

        BufferHandle create_buffer(const BufferDesc& desc) override;
        TextureHandle create_texture(const TextureDesc& desc) override;
        SamplerHandle create_sampler(const SamplerDesc& desc) override;
        ShaderHandle create_shader(const ShaderDesc& desc) override;
        PipelineHandle create_pipeline(const PipelineDesc& desc) override;
        ResourceSetHandle create_bound_resource_set(const BoundResourceSetDesc& desc) override;
        uintptr_t pipeline_resource_layout_token(PipelineHandle pipeline) const override;
        uintptr_t pipeline_descriptor_set_layout(PipelineHandle pipeline) const override;

        void destroy(BufferHandle handle) override;
        void destroy(TextureHandle handle) override;
        void destroy(SamplerHandle handle) override;
        void destroy(ShaderHandle handle) override;
        void destroy(PipelineHandle handle) override;
        void destroy(ResourceSetHandle handle) override;

        void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) override;
        void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) override;
        void upload_texture_region(TextureHandle dst,
                                   uint32_t x,
                                   uint32_t y,
                                   uint32_t w,
                                   uint32_t h,
                                   std::span<const uint8_t> data,
                                   uint32_t mip = 0) override;
        void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) override;
        TextureDesc texture_desc(TextureHandle handle) const override;

        std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) override;
        void submit(ICommandList& cmd) override;
        void present() override;
        TextureHandle register_external_texture(uintptr_t native_handle, const TextureDesc& desc) override;
        void blit_to_texture(TextureHandle dst,
                             TextureHandle src,
                             termin::Bounds2i src_rect,
                             termin::Bounds2i dst_rect) override;
        void clear_texture(TextureHandle dst, termin::LinearColor color, termin::Bounds2i viewport) override;

        bool read_pixel_rgba8(TextureHandle tex, int x, int y, float out_rgba[4]) override;
        bool read_pixel_depth_float(TextureHandle tex, int x, int y, float* out_depth) override;
        uint64_t request_pixel_rgba8(TextureHandle tex, int x, int y) override;
        bool poll_pixel_rgba8(uint64_t request_id, float out_rgba[4]) override;
        uint64_t request_pixel_depth_float(TextureHandle tex, int x, int y) override;
        bool poll_pixel_depth_float(uint64_t request_id, float* out_depth) override;
        bool read_texture_rgba_float(TextureHandle tex, float* out) override;
        bool read_texture_depth_float(TextureHandle tex, float* out) override;

        void reset_state() override;
        void flush() override;
        void finish() override;

        BufferHandle transient_vertex_buffer() override;
        uint64_t transient_vertex_write(const void* data, uint32_t size) override;
        bool transient_uniform_write(const void* data,
                                     uint32_t size,
                                     BufferHandle& out_buffer,
                                     uint32_t& out_offset) override;
        void reset_transient_uploads();

        bool ensure_tc_shader(tc_shader* shader, ShaderHandle* out_vs, ShaderHandle* out_fs) override;
        void invalidate_tc_shader_cache(uint32_t pool_index) override;
        TextureHandle ensure_tc_texture(tc_texture* tex) override;
        void invalidate_tc_texture_cache(uint32_t pool_index) override;
        std::pair<BufferHandle, BufferHandle> ensure_tc_mesh(tc_mesh* mesh) override;
        void invalidate_tc_mesh_cache(uint32_t pool_index) override;

        ID3D11Device* native_device() const {
            return device_.Get();
        }
        ID3D11DeviceContext* immediate_context() const {
            return context_.Get();
        }
        ID3D11SamplerState* default_sampler_state() const {
            return default_sampler_.Get();
        }

        D3D11Buffer* get_buffer(BufferHandle h) {
            return buffers_.get(h.id);
        }
        D3D11Texture* get_texture(TextureHandle h) {
            return textures_.get(h.id);
        }
        D3D11Sampler* get_sampler(SamplerHandle h) {
            return samplers_.get(h.id);
        }
        D3D11ShaderModule* get_shader(ShaderHandle h) {
            return shaders_.get(h.id);
        }
        D3D11Pipeline* get_pipeline(PipelineHandle h) {
            return pipelines_.get(h.id);
        }
        D3D11ResourceSet* get_resource_set(ResourceSetHandle h) {
            return resource_sets_.get(h.id);
        }

    private:
        TextureHandle register_external_texture(ID3D11Texture2D* texture, const TextureDesc& desc);
        void create_device();
        void configure_debug_layer(UINT requested_flags, bool debug_retry_disabled);
        void drain_info_queue(const char* origin);
        void create_default_sampler();
        bool ensure_blit_resources();
        void query_capabilities();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> create_staging_texture(const D3D11Texture& src) const;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> resolve_texture_for_readback(const D3D11Texture& src);
        enum class PixelReadbackKind : uint8_t {
            Rgba8,
            DepthF32,
        };
        struct PixelReadbackSlot {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            Microsoft::WRL::ComPtr<ID3D11Query> completion;
            uint64_t request_id = 0;
            uint64_t issue_sequence = 0;
            PixelReadbackKind kind = PixelReadbackKind::Rgba8;
            PixelFormat format = PixelFormat::Undefined;
            bool active = false;
        };
        static constexpr size_t kPixelReadbackSlotCount = 16;
        uint64_t request_pixel_readback(TextureHandle tex, int x, int y, PixelReadbackKind kind);
        PixelReadbackSlot* acquire_pixel_readback_slot(PixelReadbackKind kind, PixelFormat format);
        PixelReadbackSlot* find_pixel_readback_slot(uint64_t request_id);
        bool pixel_readback_ready(PixelReadbackSlot& slot, bool log_failure);
        void release_pixel_readback_slot(PixelReadbackSlot& slot);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> info_queue_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> default_sampler_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> blit_vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> blit_pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> blit_constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> blit_raster_state_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> blit_depth_stencil_state_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blit_blend_state_;
        D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;
        BackendCapabilities caps_;
        bool debug_layer_enabled_ = false;
        bool log_info_queue_ = false;
        std::array<PixelReadbackSlot, kPixelReadbackSlotCount> pixel_readback_slots_{};
        TextureHandle pixel_readback_depth_target_{};
        uint64_t next_pixel_readback_id_ = 1;
        uint64_t pixel_readback_issue_sequence_ = 1;

        D3D11HandlePool<D3D11Buffer> buffers_;
        D3D11HandlePool<D3D11Texture> textures_;
        D3D11HandlePool<D3D11Sampler> samplers_;
        D3D11HandlePool<D3D11ShaderModule> shaders_;
        D3D11HandlePool<D3D11Pipeline> pipelines_;
        D3D11HandlePool<D3D11ResourceSet> resource_sets_;

        static constexpr uint32_t kTransientVertexBufferSize = 2 * 1024 * 1024;
        BufferHandle transient_vertex_buffer_{};
        uint32_t transient_vertex_offset_ = 0;
        bool transient_vertex_discard_ = true;

        struct TransientUniformBuffer {
            BufferHandle handle{};
            uint32_t capacity = 0;
        };
        std::vector<TransientUniformBuffer> transient_uniform_buffers_;
        size_t transient_uniform_cursor_ = 0;

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
