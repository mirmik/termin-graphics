#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"

namespace tgfx2 {

VulkanCommandList::VulkanCommandList(VulkanRenderDevice& device)
    : device_(device)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = device_.command_pool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    vkAllocateCommandBuffers(device_.device(), &ai, &cmd_);
}

VulkanCommandList::~VulkanCommandList() {
    if (cmd_) {
        vkFreeCommandBuffers(device_.device(), device_.command_pool(), 1, &cmd_);
    }
}

void VulkanCommandList::begin() {
    vkResetCommandBuffer(cmd_, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd_, &bi);
}

void VulkanCommandList::end() {
    vkEndCommandBuffer(cmd_);
}

// --- Render pass ---

void VulkanCommandList::begin_render_pass(const RenderPassDesc& pass) {
    in_render_pass_ = true;

    // Determine formats and get render pass
    std::vector<PixelFormat> color_fmts;
    std::vector<VkImageView> views;
    uint32_t width = 0, height = 0;

    LoadOp color_load = LoadOp::Clear;
    for (const auto& c : pass.colors) {
        color_load = c.load;
        if (c.texture) {
            auto* tex = device_.get_texture(c.texture);
            if (tex) {
                color_fmts.push_back(tex->desc.format);
                views.push_back(tex->view);
                width = tex->desc.width;
                height = tex->desc.height;

                // Transition to color attachment if needed
                if (tex->current_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                    device_.transition_image_layout(cmd_, tex->image,
                        tex->current_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT);
                    tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
            }
        } else {
            color_fmts.push_back(PixelFormat::RGBA8_UNorm);
        }
    }
    if (color_fmts.empty()) color_fmts.push_back(PixelFormat::RGBA8_UNorm);

    PixelFormat depth_fmt = PixelFormat::D24_UNorm_S8_UInt;
    LoadOp depth_load = LoadOp::Clear;
    if (pass.has_depth && pass.depth.texture) {
        auto* tex = device_.get_texture(pass.depth.texture);
        if (tex) {
            depth_fmt = tex->desc.format;
            depth_load = pass.depth.load;
            views.push_back(tex->view);
            if (width == 0) { width = tex->desc.width; height = tex->desc.height; }
        }
    }

    auto rp = device_.get_or_create_render_pass(color_fmts, depth_fmt, pass.has_depth,
                                                  1, color_load, depth_load);
    auto fb = device_.get_or_create_framebuffer(rp, views, width, height);

    // Clear values
    std::vector<VkClearValue> clears;
    for (const auto& c : pass.colors) {
        VkClearValue cv{};
        cv.color = {{c.clear_color[0], c.clear_color[1], c.clear_color[2], c.clear_color[3]}};
        clears.push_back(cv);
    }
    if (pass.has_depth) {
        VkClearValue cv{};
        cv.depthStencil = {pass.depth.clear_depth, pass.depth.clear_stencil};
        clears.push_back(cv);
    }

    VkRenderPassBeginInfo rp_bi{};
    rp_bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_bi.renderPass = rp;
    rp_bi.framebuffer = fb;
    rp_bi.renderArea.extent = {width, height};
    rp_bi.clearValueCount = static_cast<uint32_t>(clears.size());
    rp_bi.pClearValues = clears.data();

    vkCmdBeginRenderPass(cmd_, &rp_bi, VK_SUBPASS_CONTENTS_INLINE);

    // Auto-set viewport (with Y-flip for Vulkan)
    VkViewport vp{};
    vp.x = 0;
    vp.y = static_cast<float>(height);
    vp.width = static_cast<float>(width);
    vp.height = -static_cast<float>(height); // Y-flip
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
}

void VulkanCommandList::end_render_pass() {
    vkCmdEndRenderPass(cmd_);
    in_render_pass_ = false;
}

// --- Pipeline ---

void VulkanCommandList::bind_pipeline(PipelineHandle pipeline) {
    auto* p = device_.get_pipeline(pipeline);
    if (!p) return;

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    current_layout_ = p->layout;
}

void VulkanCommandList::bind_resource_set(ResourceSetHandle set) {
    auto* rs = device_.get_resource_set(set);
    if (!rs || !current_layout_) return;

    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             current_layout_, 0, 1, &rs->descriptor_set, 0, nullptr);
}

// --- Vertex / index ---

void VulkanCommandList::bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    if (!buf) return;

    VkDeviceSize vk_offset = offset;
    vkCmdBindVertexBuffers(cmd_, slot, 1, &buf->buffer, &vk_offset);
}

void VulkanCommandList::bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    if (!buf) return;

    vkCmdBindIndexBuffer(cmd_, buf->buffer, offset, vk::to_vk_index_type(type));
}

// --- Draw ---

void VulkanCommandList::draw(uint32_t vertex_count, uint32_t first_vertex) {
    vkCmdDraw(cmd_, vertex_count, 1, first_vertex, 0);
}

void VulkanCommandList::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
    vkCmdDrawIndexed(cmd_, index_count, 1, first_index, vertex_offset, 0);
}

void VulkanCommandList::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    vkCmdDispatch(cmd_, group_x, group_y, group_z);
}

// --- Copy ---

void VulkanCommandList::copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                                     uint64_t src_offset, uint64_t dst_offset) {
    auto* s = device_.get_buffer(src);
    auto* d = device_.get_buffer(dst);
    if (!s || !d) return;

    VkBufferCopy region{};
    region.srcOffset = src_offset;
    region.dstOffset = dst_offset;
    region.size = size;
    vkCmdCopyBuffer(cmd_, s->buffer, d->buffer, 1, &region);
}

void VulkanCommandList::copy_texture(TextureHandle src, TextureHandle dst) {
    auto* s = device_.get_texture(src);
    auto* d = device_.get_texture(dst);
    if (!s || !d) return;

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {s->desc.width, s->desc.height, 1};

    vkCmdCopyImage(cmd_, s->image, s->current_layout,
                    d->image, d->current_layout, 1, &region);
}

// --- Dynamic state ---

void VulkanCommandList::set_viewport(int x, int y, int width, int height) {
    VkViewport vp{};
    vp.x = static_cast<float>(x);
    vp.y = static_cast<float>(y + height); // Y-flip
    vp.width = static_cast<float>(width);
    vp.height = -static_cast<float>(height); // Y-flip
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_, 0, 1, &vp);
}

void VulkanCommandList::set_scissor(int x, int y, int width, int height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
}

} // namespace tgfx2

#endif // TGFX2_HAS_VULKAN
