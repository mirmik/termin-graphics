#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/internal/image_transition_sync.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#include "vulkan_stats.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <tcbase/tc_log.hpp>

namespace tgfx {

    void VulkanCommandList::framebuffer_local_barrier() {
        if (!in_render_pass_) {
            tc::Log::error("VulkanCommandList::framebuffer_local_barrier requires an active render pass");
            return;
        }
        const VkPipelineStageFlags stages =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | vulkan_detail::depth_stencil_attachment_stages();
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask =
            vulkan_detail::color_attachment_accesses() | vulkan_detail::depth_stencil_attachment_accesses();
        barrier.dstAccessMask = barrier.srcAccessMask;
        vkCmdPipelineBarrier(cmd_, stages, stages, VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    VulkanCommandList::VulkanCommandList(VulkanRenderDevice& device)
        : device_(device) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = device_.command_pool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        vkAllocateCommandBuffers(device_.device(), &ai, &cmd_);
    }

    VulkanCommandList::~VulkanCommandList() {
        // Don't free immediately — the command buffer may still be in-flight
        // on the graphics queue. Hand it off to the device's pending-destroy
        // queue; it'll be released after the next fence signals in
        // `VulkanRenderDevice::submit()`.
        if (cmd_) {
            device_.defer_cmd_buffer_free(cmd_);
            cmd_ = VK_NULL_HANDLE;
        }
    }

    void VulkanCommandList::begin() {
        vkResetCommandBuffer(cmd_, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd_, &bi);
        gpu_timing_frame_number_ = -1;
        gpu_timing_open_ = false;
        if (vulkan_stats_enabled()) {
            record_start_ = std::chrono::steady_clock::now();
        }
    }

    void VulkanCommandList::end() {
        if (gpu_timing_open_) {
            tc_log(TC_LOG_ERROR, "[tgfx2/vulkan] GPU frame timing was not closed before command-list end");
            end_gpu_frame_timing();
        }
        vkEndCommandBuffer(cmd_);
        if (vulkan_stats_enabled()) {
            auto dt = std::chrono::steady_clock::now() - record_start_;
            vulkan_stats_increment(g_record_us, std::chrono::duration_cast<std::chrono::microseconds>(dt).count());
        }
    }

    bool VulkanCommandList::begin_gpu_frame_timing(std::int64_t frame_number) {
        if (gpu_timing_open_ || frame_number < 0) {
            tc_log(TC_LOG_ERROR,
                   "[tgfx2/vulkan] Refusing invalid GPU frame timing begin: frame=%lld open=%d",
                   static_cast<long long>(frame_number),
                   gpu_timing_open_ ? 1 : 0);
            return false;
        }
        if (!device_.capabilities().supports_timestamp_queries) {
            return false;
        }
        gpu_timing_frame_number_ = frame_number;
        gpu_timing_open_ = true;
        return true;
    }

    void VulkanCommandList::end_gpu_frame_timing() {
        if (!gpu_timing_open_) {
            return;
        }
        gpu_timing_open_ = false;
    }

    // --- Render pass ---

    void VulkanCommandList::begin_render_pass(const RenderPassDesc& pass) {
        begin_render_pass_impl(pass, 1, MultiviewColorFinalState::ShaderRead);
    }

    void VulkanCommandList::begin_multiview_render_pass(const MultiviewRenderPassDesc& pass) {
        RenderPassDesc base;
        base.colors = pass.colors;
        base.depth = pass.depth;
        base.has_depth = pass.has_depth;
        begin_render_pass_impl(base, pass.view_count, pass.color_final_state);
    }

    void VulkanCommandList::begin_render_pass_impl(const RenderPassDesc& pass,
                                                   uint32_t view_count,
                                                   MultiviewColorFinalState color_final_state) {
        if (pass.colors.size() > TGFX2_MAX_COLOR_ATTACHMENTS ||
            pass.colors.size() > device_.capabilities().max_color_attachments) {
            tc::Log::error("VulkanCommandList::begin_render_pass: color attachment count exceeds backend limit");
            return;
        }

        uint32_t expected_width = 0;
        uint32_t expected_height = 0;
        uint32_t expected_samples = 0;
        uint32_t expected_layers = 0;
        auto validate_extent = [&](const TextureDesc& desc, const char* attachment_kind) -> bool {
            if (expected_width == 0) {
                expected_width = desc.width;
                expected_height = desc.height;
                expected_samples = desc.sample_count == 0 ? 1 : desc.sample_count;
                expected_layers = desc.array_layers;
                return true;
            }
            const uint32_t samples = desc.sample_count == 0 ? 1 : desc.sample_count;
            if (desc.width != expected_width || desc.height != expected_height || samples != expected_samples ||
                desc.array_layers != expected_layers) {
                tc::Log::error("VulkanCommandList::begin_render_pass: %s attachment extent/sample mismatch",
                               attachment_kind);
                return false;
            }
            return true;
        };

        for (const auto& color : pass.colors) {
            if (!color.texture) {
                tc::Log::error("VulkanCommandList::begin_render_pass: null color attachment in ordered MRT set");
                return;
            }
            const auto* texture = device_.get_texture(color.texture);
            if (!texture) {
                tc::Log::error("VulkanCommandList::begin_render_pass: unknown color attachment handle");
                return;
            }
            if (!has_flag(texture->desc.usage, TextureUsage::ColorAttachment)) {
                tc::Log::error("VulkanCommandList::begin_render_pass: texture lacks ColorAttachment usage");
                return;
            }
            if (!validate_extent(texture->desc, "color")) {
                return;
            }
        }
        if (pass.has_depth) {
            if (!pass.depth.texture) {
                tc::Log::error("VulkanCommandList::begin_render_pass: depth pass has no depth texture");
                return;
            }
            const auto* texture = device_.get_texture(pass.depth.texture);
            if (!texture) {
                tc::Log::error("VulkanCommandList::begin_render_pass: unknown depth attachment handle");
                return;
            }
            if (!has_flag(texture->desc.usage, TextureUsage::DepthStencilAttachment)) {
                tc::Log::error("VulkanCommandList::begin_render_pass: texture lacks DepthStencilAttachment usage");
                return;
            }
            if (!validate_extent(texture->desc, "depth")) {
                return;
            }
        }
        if (expected_width == 0) {
            tc::Log::error("VulkanCommandList::begin_render_pass: pass has no attachments");
            return;
        }
        if ((view_count == 1 && expected_layers != 1) || (view_count > 1 && expected_layers < view_count)) {
            tc::Log::error("VulkanCommandList::begin_render_pass: attachment layers are incompatible with view count");
            return;
        }

        // Determine formats and get render pass
        std::vector<PixelFormat> color_fmts;
        std::vector<LoadOp> color_loads;
        std::vector<StoreOp> color_stores;
        std::vector<VkImageView> views;
        uint32_t width = 0, height = 0;

        uint32_t sample_count = 1;
        current_pass_color_attachments_.clear();
        current_pass_color_final_state_ = color_final_state;
        // The ordered color list is authoritative: entry N maps to fragment
        // output location N. Validation above deliberately rejects missing
        // entries instead of compacting and silently renumbering later outputs.
        for (const auto& c : pass.colors) {
            auto* tex = device_.get_texture(c.texture);

            color_fmts.push_back(tex->desc.format);
            color_loads.push_back(c.load);
            color_stores.push_back(c.store);
            views.push_back(tex->view);
            width = tex->desc.width;
            height = tex->desc.height;
            sample_count = tex->desc.sample_count;

            // Transition to color attachment if needed
            if (tex->current_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                device_.transition_image_layout(cmd_,
                                                tex->image,
                                                tex->current_layout,
                                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                VK_IMAGE_ASPECT_COLOR_BIT,
                                                tex->desc.array_layers);
                tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            current_pass_color_attachments_.push_back(c.texture);
        }

        PixelFormat depth_fmt = PixelFormat::D24_UNorm_S8_UInt;
        LoadOp depth_load = LoadOp::Clear;
        current_pass_depth_attachment_ = {};
        if (pass.has_depth) {
            auto* tex = device_.get_texture(pass.depth.texture);
            depth_fmt = tex->desc.format;
            depth_load = pass.depth.load;
            views.push_back(tex->view);
            if (width == 0) {
                width = tex->desc.width;
                height = tex->desc.height;
            }
            // Depth attachment must match the color sample count. If we
            // have no color, take it from here.
            if (sample_count == 1 && tex->desc.sample_count > 1) {
                sample_count = tex->desc.sample_count;
            }
            // Transition depth to DEPTH_STENCIL_ATTACHMENT_OPTIMAL before
            // the pass starts. Without this, a shadow-depth texture left
            // in SHADER_READ_ONLY_OPTIMAL by a previous sampler use
            // survives into the next shadow pass's render-pass load op.
            VkImageAspectFlags dep_aspect = vk::format_aspect_flags(tex->desc.format);
            if (tex->current_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                device_.transition_image_layout(cmd_,
                                                tex->image,
                                                tex->current_layout,
                                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                dep_aspect,
                                                tex->desc.array_layers);
                tex->current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
            current_pass_depth_attachment_ = pass.depth.texture;
        }
        if (sample_count == 0)
            sample_count = 1;

        auto rp = device_.get_or_create_render_pass(color_fmts,
                                                    color_loads,
                                                    color_stores,
                                                    depth_fmt,
                                                    pass.has_depth,
                                                    sample_count,
                                                    depth_load,
                                                    pass.depth.store,
                                                    view_count > 1 ? ((1u << view_count) - 1u) : 0u);
        if (rp == VK_NULL_HANDLE) {
            tc::Log::error("VulkanCommandList::begin_render_pass: failed to resolve render-pass contract");
            return;
        }
        auto fb = device_.get_or_create_framebuffer(rp, views, width, height);
        if (fb == VK_NULL_HANDLE) {
            tc::Log::error("VulkanCommandList::begin_render_pass: failed to resolve framebuffer");
            return;
        }

        // Clear values: one per attachment in the same order as the render
        // pass was built. Colors first, then the optional depth slot.
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
        in_render_pass_ = true;

        // Auto-set viewport. No Y-flip trick here: it makes the render-pass
        // output memory read-compatible with OpenGL conventions for on-screen
        // presentation, but it breaks inter-pass sampling (shader writes to
        // pixel Y=h-1 when it thinks it's writing to Y=0, then a later pass
        // samples with UV.y=0 and reads the actual top texel — producing e.g.
        // shadow-map lookups that always land outside the rendered frustum).
        // Instead, every render-target memory is in native Vulkan top-left
        // layout, and the final display composite (Viewport3D) skips its
        // flip_v on Vulkan to keep the presented image upright.
        VkViewport vp{};
        vp.x = 0;
        vp.y = 0;
        vp.width = static_cast<float>(width);
        vp.height = static_cast<float>(height);
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
            if (!tex || tex->image == VK_NULL_HANDLE)
                continue;
            if (current_pass_color_final_state_ == MultiviewColorFinalState::ColorAttachment)
                continue;
            if (!has_flag(tex->desc.usage, TextureUsage::Sampled))
                continue;
            if (tex->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                continue;
            device_.transition_image_layout(cmd_,
                                            tex->image,
                                            tex->current_layout,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_IMAGE_ASPECT_COLOR_BIT,
                                            tex->desc.array_layers);
            tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        current_pass_color_attachments_.clear();
        current_pass_color_final_state_ = MultiviewColorFinalState::ShaderRead;

        // Do the same for the depth attachment so shadow-depth textures are
        // directly samplable afterwards (ShadowPass → any pass that binds the
        // shadow map as a SampledTexture).
        if (current_pass_depth_attachment_) {
            auto* tex = device_.get_texture(current_pass_depth_attachment_);
            if (tex && tex->image != VK_NULL_HANDLE && has_flag(tex->desc.usage, TextureUsage::Sampled) &&
                tex->current_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                device_.transition_image_layout(cmd_,
                                                tex->image,
                                                tex->current_layout,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                vk::format_aspect_flags(tex->desc.format),
                                                tex->desc.array_layers);
                tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            current_pass_depth_attachment_ = {};
        }
    }

    // --- Pipeline ---

    void VulkanCommandList::bind_pipeline(PipelineHandle pipeline) {
        auto* p = device_.get_pipeline(pipeline);
        if (!p)
            return;

        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
        current_layout_ = p->layout;
        vulkan_stats_increment(g_bind_pipeline_count);
    }

    void VulkanCommandList::bind_resource_set(ResourceSetHandle set,
                                              uint32_t set_index,
                                              const uint32_t* dynamic_offsets,
                                              uint32_t dynamic_offset_count) {
        auto* rs = device_.get_resource_set(set);
        if (!rs || !current_layout_)
            return;

        // Use the stored dynamic offsets when the caller doesn't supply any.
        const uint32_t* offsets_ptr = dynamic_offsets;
        uint32_t count = dynamic_offset_count;
        if (!dynamic_offsets || dynamic_offset_count == 0) {
            offsets_ptr = rs->dynamic_offsets;
            count = rs->dynamic_offset_count;
        }

        vkCmdBindDescriptorSets(cmd_,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                current_layout_,
                                set_index,
                                1,
                                &rs->descriptor_set,
                                count,
                                offsets_ptr);
        vulkan_stats_increment(g_bind_rset_count);
    }

    void VulkanCommandList::set_push_constants(const void* data, uint32_t size) {
        if (!data || size == 0 || !current_layout_)
            return;
        // Push to all shader stages for now. Pipeline layouts must declare
        // a VkPushConstantRange that covers [0, size] for all stages; the
        // Vulkan backend's pipeline creation path is responsible for that
        // (TBD — Vulkan backend has no push constants wiring yet).
        vkCmdPushConstants(cmd_, current_layout_, VK_SHADER_STAGE_ALL_GRAPHICS, 0, size, data);
        vulkan_stats_increment(g_push_constants_count);
    }

    // --- Vertex / index ---

    void VulkanCommandList::bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset) {
        auto* buf = device_.get_buffer(buffer);
        if (!buf)
            return;

        VkDeviceSize vk_offset = offset;
        vkCmdBindVertexBuffers(cmd_, slot, 1, &buf->buffer, &vk_offset);
        vulkan_stats_increment(g_bind_vbo_count);
    }

    void VulkanCommandList::bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset) {
        auto* buf = device_.get_buffer(buffer);
        if (!buf)
            return;

        vkCmdBindIndexBuffer(cmd_, buf->buffer, offset, vk::to_vk_index_type(type));
        vulkan_stats_increment(g_bind_ibo_count);
    }

    // --- Draw ---

    void VulkanCommandList::draw(uint32_t vertex_count, uint32_t first_vertex) {
        vkCmdDraw(cmd_, vertex_count, 1, first_vertex, 0);
        vulkan_stats_increment(g_draw_count);
    }

    void VulkanCommandList::draw_instanced(uint32_t vertex_count,
                                           uint32_t instance_count,
                                           uint32_t first_vertex,
                                           uint32_t first_instance) {
        vkCmdDraw(cmd_, vertex_count, instance_count, first_vertex, first_instance);
        vulkan_stats_increment(g_draw_count);
    }

    void VulkanCommandList::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
        vkCmdDrawIndexed(cmd_, index_count, 1, first_index, vertex_offset, 0);
        vulkan_stats_increment(g_draw_count);
    }

    void VulkanCommandList::draw_indexed_instanced(uint32_t index_count,
                                                   uint32_t instance_count,
                                                   uint32_t first_index,
                                                   int32_t vertex_offset,
                                                   uint32_t first_instance) {
        vkCmdDrawIndexed(cmd_, index_count, instance_count, first_index, vertex_offset, first_instance);
        vulkan_stats_increment(g_draw_count);
    }

    void VulkanCommandList::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
        vkCmdDispatch(cmd_, group_x, group_y, group_z);
    }

    // --- Copy ---

    void VulkanCommandList::copy_buffer(
        BufferHandle src, BufferHandle dst, uint64_t size, uint64_t src_offset, uint64_t dst_offset) {
        auto* s = device_.get_buffer(src);
        auto* d = device_.get_buffer(dst);
        if (!s || !d)
            return;

        VkBufferCopy region{};
        region.srcOffset = src_offset;
        region.dstOffset = dst_offset;
        region.size = size;
        vkCmdCopyBuffer(cmd_, s->buffer, d->buffer, 1, &region);
    }

    void VulkanCommandList::copy_texture(TextureHandle src, TextureHandle dst) {
        auto* s = device_.get_texture(src);
        auto* d = device_.get_texture(dst);
        if (!s || !d)
            return;

        // Self-blit is a caller-side logic bug — Vulkan disallows
        // vkCmdBlitImage/CopyImage where src and dst are the same image with
        // overlapping regions, and there's no meaningful work to do. Skip
        // quietly instead of emitting conflicting TRANSFER_SRC/DST barriers.
        if (s == d)
            return;
        if (s->desc.array_layers != d->desc.array_layers) {
            tc::Log::error("VulkanCommandList::copy_texture: source and destination layer counts differ");
            return;
        }

        // Transfer commands must be recorded outside of a render pass. Callers
        // using ctx2.blit() between begin_frame/end_frame are fine as long as
        // no begin_render_pass is active.
        if (in_render_pass_) {
            fprintf(stderr,
                    "[Vulkan] copy_texture called inside an active "
                    "render pass — skipping (call blit outside begin/end_pass)\n");
            return;
        }

        VkImageAspectFlags src_aspect = vk::format_aspect_flags(s->desc.format);
        VkImageAspectFlags dst_aspect = vk::format_aspect_flags(d->desc.format);

        VkImageLayout prev_src = s->current_layout;
        VkImageLayout prev_dst = d->current_layout;

        device_.transition_image_layout(
            cmd_, s->image, prev_src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src_aspect, s->desc.array_layers);
        device_.transition_image_layout(
            cmd_, d->image, prev_dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_aspect, d->desc.array_layers);

        uint32_t w = std::min(s->desc.width, d->desc.width);
        uint32_t h = std::min(s->desc.height, d->desc.height);

        // Pick the right transfer op:
        //   MSAA src + single dst, same format  → vkCmdResolveImage
        //   Same extent + same fmt + same samp  → vkCmdCopyImage (fastest)
        //   Different extent or format          → vkCmdBlitImage (scales)
        //   MSAA src + format conversion        → not supported by Vulkan in one step.
        //
        // The OpenGL counterpart uses glBlitFramebuffer, which *always*
        // rescales source to destination. A capture texture sized to the
        // viewport vs. a shadow map sized to shadow-map resolution is a
        // common case for Frame Debugger: vkCmdCopyImage would only copy
        // the overlapping rectangle and leave the rest garbage, while
        // vkCmdBlitImage stretches — matching the GL path.
        bool same_format = s->desc.format == d->desc.format;
        bool same_samples = s->desc.sample_count == d->desc.sample_count;
        bool same_extent = s->desc.width == d->desc.width && s->desc.height == d->desc.height;
        bool msaa_to_single = s->desc.sample_count > 1 && d->desc.sample_count == 1;

        if (msaa_to_single && same_format) {
            VkImageResolve resolve{};
            resolve.srcSubresource = {src_aspect, 0, 0, s->desc.array_layers};
            resolve.dstSubresource = {dst_aspect, 0, 0, d->desc.array_layers};
            resolve.extent = {w, h, 1};
            vkCmdResolveImage(cmd_,
                              s->image,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              d->image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              1,
                              &resolve);
        } else if (same_samples && same_format && same_extent) {
            VkImageCopy region{};
            region.srcSubresource = {src_aspect, 0, 0, s->desc.array_layers};
            region.dstSubresource = {dst_aspect, 0, 0, d->desc.array_layers};
            region.extent = {w, h, 1};
            vkCmdCopyImage(cmd_,
                           s->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           d->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);
        } else if (same_samples) {
            VkImageBlit blit{};
            blit.srcSubresource = {src_aspect, 0, 0, s->desc.array_layers};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {int32_t(s->desc.width), int32_t(s->desc.height), 1};
            blit.dstSubresource = {dst_aspect, 0, 0, d->desc.array_layers};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {int32_t(d->desc.width), int32_t(d->desc.height), 1};
            // Linear for color (LDR/HDR filtering ok), nearest for depth — depth
            // formats forbid linear filtering in blit.
            VkFilter filter = (src_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            vkCmdBlitImage(cmd_,
                           s->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           d->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           filter);
        } else {
            fprintf(stderr,
                    "[Vulkan] copy_texture cannot MSAA-resolve with "
                    "format conversion in one step (src samples=%u fmt=%d, "
                    "dst samples=%u fmt=%d) — skipping\n",
                    s->desc.sample_count,
                    (int)s->desc.format,
                    d->desc.sample_count,
                    (int)d->desc.format);
            device_.transition_image_layout(cmd_,
                                            s->image,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            src_aspect,
                                            s->desc.array_layers);
            device_.transition_image_layout(cmd_,
                                            d->image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            dst_aspect,
                                            d->desc.array_layers);
            s->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            d->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return;
        }

        // Leave both images in SHADER_READ_ONLY_OPTIMAL. Downstream
        // bind_resource_set requires sampled textures to be in this layout
        // already (it cannot transition inside a render pass), and most
        // callers of copy_texture intend to sample the dst next.
        device_.transition_image_layout(cmd_,
                                        s->image,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        src_aspect,
                                        s->desc.array_layers);
        device_.transition_image_layout(cmd_,
                                        d->image,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        dst_aspect,
                                        d->desc.array_layers);

        s->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        d->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // --- Dynamic state ---

    void VulkanCommandList::set_viewport(int x, int y, int width, int height) {
        // Vulkan-native viewport — no Y-flip here. Projection matrices
        // (termin-base/geom/mat44.hpp) target clip-space Y-down already,
        // so clip_y=-1 maps to the top row of framebuffer memory. OpenGL
        // reaches the same convention via glClipControl(GL_UPPER_LEFT).
        VkViewport vp{};
        vp.x = static_cast<float>(x);
        vp.y = static_cast<float>(y);
        vp.width = static_cast<float>(width);
        vp.height = static_cast<float>(height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_, 0, 1, &vp);
    }

    void VulkanCommandList::set_scissor(int x, int y, int width, int height) {
        // Negative width/height crossed into `uint32_t` becomes a huge value
        // that trips `offset + extent > INT32_MAX` in validation. Clamp here
        // so caller bugs (e.g. a widget with negative size after layout)
        // produce an empty-clip no-op rather than a Vulkan error.
        if (x < 0) {
            width += x;
            x = 0;
        }
        if (y < 0) {
            height += y;
            y = 0;
        }
        if (width < 0)
            width = 0;
        if (height < 0)
            height = 0;
        VkRect2D scissor{};
        scissor.offset = {x, y};
        scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        vkCmdSetScissor(cmd_, 0, 1, &scissor);
    }

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
