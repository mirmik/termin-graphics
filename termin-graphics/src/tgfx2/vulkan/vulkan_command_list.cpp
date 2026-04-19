#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"

namespace tgfx {

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
    uint32_t sample_count = 1;
    current_pass_color_attachments_.clear();
    // Collect real color attachments. Entries with a null texture or a
    // missing device-side record are dropped — pushing a placeholder
    // format for them made the render-pass cache build a 1-color RP
    // that then mismatched a framebuffer carrying zero color views
    // (depth-only passes like ShadowPass/DepthPass). The pass description
    // `colors` list is authoritative for the attachment count.
    for (const auto& c : pass.colors) {
        if (!c.texture) continue;
        auto* tex = device_.get_texture(c.texture);
        if (!tex) continue;

        color_load = c.load;
        color_fmts.push_back(tex->desc.format);
        views.push_back(tex->view);
        width = tex->desc.width;
        height = tex->desc.height;
        sample_count = tex->desc.sample_count;

        // Transition to color attachment if needed
        if (tex->current_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            device_.transition_image_layout(cmd_, tex->image,
                tex->current_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT);
            tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        current_pass_color_attachments_.push_back(c.texture);
    }

    PixelFormat depth_fmt = PixelFormat::D24_UNorm_S8_UInt;
    LoadOp depth_load = LoadOp::Clear;
    if (pass.has_depth && pass.depth.texture) {
        auto* tex = device_.get_texture(pass.depth.texture);
        if (tex) {
            depth_fmt = tex->desc.format;
            depth_load = pass.depth.load;
            views.push_back(tex->view);
            if (width == 0) { width = tex->desc.width; height = tex->desc.height; }
            // Depth attachment must match the color sample count. If we
            // have no color, take it from here.
            if (sample_count == 1 && tex->desc.sample_count > 1) {
                sample_count = tex->desc.sample_count;
            }
        }
    }
    if (sample_count == 0) sample_count = 1;

    auto rp = device_.get_or_create_render_pass(color_fmts, depth_fmt, pass.has_depth,
                                                  sample_count, color_load, depth_load);
    auto fb = device_.get_or_create_framebuffer(rp, views, width, height);

    // Clear values: one per attachment in the same order as the render
    // pass was built. Colors first (only those actually attached —
    // entries dropped above must not push a clear), then the optional
    // depth slot.
    std::vector<VkClearValue> clears;
    {
        size_t color_idx = 0;
        for (const auto& c : pass.colors) {
            if (!c.texture) continue;
            if (!device_.get_texture(c.texture)) continue;
            if (color_idx >= color_fmts.size()) break;
            VkClearValue cv{};
            cv.color = {{c.clear_color[0], c.clear_color[1],
                         c.clear_color[2], c.clear_color[3]}};
            clears.push_back(cv);
            ++color_idx;
        }
    }
    if (pass.has_depth && pass.depth.texture &&
        device_.get_texture(pass.depth.texture)) {
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

    // Transition color attachments out of COLOR_ATTACHMENT_OPTIMAL and
    // into SHADER_READ_ONLY_OPTIMAL so a subsequent pass that samples
    // from them finds the layout the descriptor write expects. Without
    // this the validator flags "layout mismatch" as soon as the next
    // pass's draw uses the texture (the compositor's premul → unpremul
    // chain is the canonical case). If the next pass renders into the
    // same texture again, begin_render_pass will transition back to
    // COLOR_ATTACHMENT_OPTIMAL — cheap.
    for (auto h : current_pass_color_attachments_) {
        auto* tex = device_.get_texture(h);
        if (!tex || tex->image == VK_NULL_HANDLE) continue;
        if (tex->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
        device_.transition_image_layout(cmd_, tex->image,
            tex->current_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
        tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    current_pass_color_attachments_.clear();
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

    // Walk the set's SampledTexture bindings and transition each image
    // to SHADER_READ_ONLY_OPTIMAL. Textures that were just rendered into
    // (and so are sitting in COLOR_ATTACHMENT_OPTIMAL) fail validation
    // otherwise, because the descriptor write baked in SHADER_READ_ONLY_
    // OPTIMAL as the "expected" layout. This is the compositor case:
    // render into `_main_tex`, then sample from it in the unpremul
    // pass. Caller doesn't need to emit explicit barriers.
    for (const auto& b : rs->desc.bindings) {
        if (b.kind != ResourceBinding::Kind::SampledTexture) continue;
        auto* tex = device_.get_texture(b.texture);
        if (!tex || tex->image == VK_NULL_HANDLE) continue;
        if (tex->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
        device_.transition_image_layout(cmd_, tex->image,
            tex->current_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
        tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             current_layout_, 0, 1, &rs->descriptor_set, 0, nullptr);
}

void VulkanCommandList::set_push_constants(const void* data, uint32_t size) {
    if (!data || size == 0 || !current_layout_) return;
    // Push to all shader stages for now. Pipeline layouts must declare
    // a VkPushConstantRange that covers [0, size] for all stages; the
    // Vulkan backend's pipeline creation path is responsible for that
    // (TBD — Vulkan backend has no push constants wiring yet).
    vkCmdPushConstants(cmd_, current_layout_,
                       VK_SHADER_STAGE_ALL_GRAPHICS, 0, size, data);
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

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
