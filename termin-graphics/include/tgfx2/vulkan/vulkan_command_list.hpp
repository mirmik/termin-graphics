#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/vulkan/vulkan_render_device.hpp"

namespace tgfx {

class TGFX2_API VulkanCommandList : public ICommandList {
public:
    explicit VulkanCommandList(VulkanRenderDevice& device);
    ~VulkanCommandList() override;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDesc& pass) override;
    void end_render_pass() override;

    void bind_pipeline(PipelineHandle pipeline) override;
    void bind_resource_set(ResourceSetHandle set) override;
    void set_push_constants(const void* data, uint32_t size) override;

    void bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0) override;
    void bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset = 0) override;

    void draw(uint32_t vertex_count, uint32_t first_vertex = 0) override;
    void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t vertex_offset = 0) override;
    void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

    void copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                     uint64_t src_offset = 0, uint64_t dst_offset = 0) override;
    void copy_texture(TextureHandle src, TextureHandle dst) override;

    void set_viewport(int x, int y, int width, int height) override;
    void set_scissor(int x, int y, int width, int height) override;

    VkCommandBuffer command_buffer() const { return cmd_; }

private:
    VulkanRenderDevice& device_;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkPipelineLayout current_layout_ = VK_NULL_HANDLE;
    bool in_render_pass_ = false;
};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
