#pragma once

#include "tgfx2/d3d11/d3d11_render_device.hpp"
#include "tgfx2/i_command_list.hpp"

namespace tgfx {

class TGFX2_TYPE_API D3D11CommandList : public ICommandList {
public:
    explicit D3D11CommandList(D3D11RenderDevice& device);
    ~D3D11CommandList() override = default;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDesc& pass) override;
    void end_render_pass() override;

    void bind_pipeline(PipelineHandle pipeline) override;
    void bind_resource_set(ResourceSetHandle set,
                           uint32_t set_index = 0,
                           const uint32_t* dynamic_offsets = nullptr,
                           uint32_t dynamic_offset_count = 0) override;
    void set_push_constants(const void* data, uint32_t size) override;

    void bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0) override;
    void bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset = 0) override;

    void draw(uint32_t vertex_count, uint32_t first_vertex = 0) override;
    void draw_instanced(uint32_t vertex_count,
                        uint32_t instance_count,
                        uint32_t first_vertex = 0,
                        uint32_t first_instance = 0) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) override;
    void draw_indexed_instanced(uint32_t index_count,
                                uint32_t instance_count,
                                uint32_t first_index = 0,
                                int32_t vertex_offset = 0,
                                uint32_t first_instance = 0) override;
    void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                     uint64_t src_offset = 0, uint64_t dst_offset = 0) override;
    void copy_texture(TextureHandle src, TextureHandle dst) override;

    void set_viewport(int x, int y, int width, int height) override;
    void set_scissor(int x, int y, int width, int height) override;

private:
    D3D11RenderDevice& device_;
    ID3D11DeviceContext* ctx_ = nullptr;
    PipelineHandle current_pipeline_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> push_constant_buffer_;
    uint32_t push_constant_buffer_size_ = 0;
};

} // namespace tgfx
