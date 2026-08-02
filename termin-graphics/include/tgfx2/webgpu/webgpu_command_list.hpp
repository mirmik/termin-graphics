#pragma once

#include <webgpu/webgpu_cpp.h>

#include "tgfx2/i_command_list.hpp"

namespace tgfx {

class WebGpuRenderDevice;

class WebGpuCommandList final : public ICommandList {
public:
    explicit WebGpuCommandList(WebGpuRenderDevice& device);

    void begin() override;
    void end() override;
    void begin_render_pass(const RenderPassDesc& pass) override;
    void end_render_pass() override;
    void bind_pipeline(PipelineHandle pipeline) override;
    void bind_resource_set(ResourceSetHandle set, uint32_t set_index,
                           const uint32_t* dynamic_offsets,
                           uint32_t dynamic_offset_count) override;
    void set_push_constants(const void* data, uint32_t size) override;
    void bind_vertex_buffer(uint32_t slot, BufferHandle buffer,
                            uint64_t offset) override;
    void bind_index_buffer(BufferHandle buffer, IndexType type,
                           uint64_t offset) override;
    void draw(uint32_t vertex_count, uint32_t first_vertex) override;
    void draw_instanced(uint32_t vertex_count, uint32_t instance_count,
                        uint32_t first_vertex, uint32_t first_instance) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index,
                      int32_t vertex_offset) override;
    void draw_indexed_instanced(uint32_t index_count, uint32_t instance_count,
                                uint32_t first_index, int32_t vertex_offset,
                                uint32_t first_instance) override;
    void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;
    void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                     uint64_t src_offset, uint64_t dst_offset) override;
    void copy_texture(TextureHandle src, TextureHandle dst) override;
    void set_viewport(int x, int y, int width, int height) override;
    void set_scissor(int x, int y, int width, int height) override;

    wgpu::CommandBuffer take_command_buffer();

private:
    WebGpuRenderDevice& device_;
    wgpu::CommandEncoder encoder_;
    wgpu::RenderPassEncoder render_pass_;
    wgpu::CommandBuffer command_buffer_;
    bool recording_ = false;
    bool in_render_pass_ = false;
    uintptr_t current_layout_token_ = 0;
};

} // namespace tgfx
