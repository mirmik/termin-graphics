#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <chrono>
#include <vector>
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
    void bind_resource_set(ResourceSetHandle set,
                           const uint32_t* dynamic_offsets = nullptr,
                           uint32_t dynamic_offset_count = 0) override;
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

    // Color attachments of the currently-open render pass. Stashed in
    // begin_render_pass, drained in end_render_pass — where each is
    // transitioned from COLOR_ATTACHMENT_OPTIMAL to SHADER_READ_ONLY_
    // OPTIMAL so the next pass can sample from it without the layout
    // mismatch validation error. Cheap even when the texture is not
    // sampled next — one barrier per attachment.
    std::vector<TextureHandle> current_pass_color_attachments_;
    TextureHandle current_pass_depth_attachment_{};

    // Timestamp of the most recent begin(); subtracted in end() to feed
    // the cumulative cmd-recording counter in the Vulkan hot-path summary.
    std::chrono::steady_clock::time_point record_start_{};
};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
