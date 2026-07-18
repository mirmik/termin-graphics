#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "vulkan_stats.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>

extern "C" {
#include <tcbase/tc_log.h>
}

namespace tgfx {

namespace {

double us_between(std::chrono::steady_clock::time_point a,
                  std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}

// Pick a present mode. FIFO is guaranteed available and effectively
// vsync; MAILBOX would give lower latency but we take the safe default
// for portability.
VkPresentModeKHR pick_present_mode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Pick a surface format. Prefer 8-bit sRGB; fall back to the first
// format offered by the driver.
VkSurfaceFormatKHR pick_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM ||
             f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                 VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                            : formats[0];
}

} // namespace

// ---------------------------------------------------------------------------

VulkanSwapchain::VulkanSwapchain(VulkanRenderDevice& dev,
                                   VkSurfaceKHR surface,
                                   uint32_t width, uint32_t height)
    : device_(dev), surface_(surface), width_(width), height_(height)
{
    create_swapchain();
    create_sync_objects();

    // Pre-allocate one command buffer per in-flight slot for
    // compose_and_present; they're reset-and-recorded each frame.
    compose_command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = device_.command_pool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    if (vkAllocateCommandBuffers(device_.device(), &ai,
                                  compose_command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("VulkanSwapchain: failed to allocate compose command buffers");
    }
}

VulkanSwapchain::~VulkanSwapchain() {
    if (device_.device()) {
        vkDeviceWaitIdle(device_.device());
    }
    if (!compose_command_buffers_.empty()) {
        vkFreeCommandBuffers(device_.device(),
                              device_.command_pool(),
                              static_cast<uint32_t>(compose_command_buffers_.size()),
                              compose_command_buffers_.data());
        compose_command_buffers_.clear();
    }
    destroy_sync_objects();
    destroy_swapchain();
}

// ---------------------------------------------------------------------------
// Swapchain lifecycle
// ---------------------------------------------------------------------------

void VulkanSwapchain::create_swapchain() {
    VkPhysicalDevice physical_device = device_.physical_device();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface_, &caps);

    // Clamp requested extent to the surface's min/max. currentExtent
    // is authoritative on most platforms unless it's the special
    // 0xFFFFFFFF value (Wayland in particular) — in which case we
    // use our own size clamped to min/max.
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(width_, caps.minImageExtent.width,
                                    caps.maxImageExtent.width);
        extent.height = std::clamp(height_, caps.minImageExtent.height,
                                     caps.maxImageExtent.height);
    }
    width_ = extent.width;
    height_ = extent.height;

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, formats.data());
    VkSurfaceFormatKHR fmt = pick_surface_format(formats);
    format_ = fmt.format;
    color_space_ = fmt.colorSpace;

    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface_, &pm_count, nullptr);
    std::vector<VkPresentModeKHR> modes(pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface_, &pm_count, modes.data());
    present_mode_ = pick_present_mode(modes);

    // Aim for minImageCount + 1 so we always have an image ready for
    // the next frame while the current one is being presented. Clamp
    // to maxImageCount if the driver caps it (0 means no cap).
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = image_count;
    ci.imageFormat = format_;
    ci.imageColorSpace = color_space_;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    // TRANSFER_DST so we can blit into the swapchain image; COLOR_ATTACHMENT
    // so we can render directly into it through a VkRenderPass.
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present_mode_;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_.device(), &ci, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("VulkanSwapchain: vkCreateSwapchainKHR failed");
    }

    // Grab the actual images.
    uint32_t got = 0;
    vkGetSwapchainImagesKHR(device_.device(), swapchain_, &got, nullptr);
    images_.resize(got);
    vkGetSwapchainImagesKHR(device_.device(), swapchain_, &got, images_.data());

    // Create one image view per image — same format, color aspect,
    // full-mip/layer range.
    image_views_.resize(got);
    for (uint32_t i = 0; i < got; ++i) {
        VkImageViewCreateInfo v{};
        v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image = images_[i];
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = format_;
        v.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        v.subresourceRange.baseMipLevel = 0;
        v.subresourceRange.levelCount = 1;
        v.subresourceRange.baseArrayLayer = 0;
        v.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_.device(), &v, nullptr, &image_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("VulkanSwapchain: vkCreateImageView failed");
        }
    }
}

void VulkanSwapchain::destroy_swapchain() {
    VkDevice dev = device_.device();
    for (auto v : image_views_) {
        if (v) vkDestroyImageView(dev, v, nullptr);
    }
    image_views_.clear();
    images_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(dev, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Sync objects
// ---------------------------------------------------------------------------

void VulkanSwapchain::create_sync_objects() {
    VkDevice dev = device_.device();
    image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Start signalled so the very first wait_for_current_frame() doesn't block.
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(dev, &sem_ci, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(dev, &sem_ci, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(dev, &fence_ci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("VulkanSwapchain: failed to create frame sync objects");
        }
    }
}

void VulkanSwapchain::destroy_sync_objects() {
    VkDevice dev = device_.device();
    for (auto s : image_available_semaphores_) {
        if (s) vkDestroySemaphore(dev, s, nullptr);
    }
    for (auto s : render_finished_semaphores_) {
        if (s) vkDestroySemaphore(dev, s, nullptr);
    }
    for (auto f : in_flight_fences_) {
        if (f) vkDestroyFence(dev, f, nullptr);
    }
    image_available_semaphores_.clear();
    render_finished_semaphores_.clear();
    in_flight_fences_.clear();
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

void VulkanSwapchain::wait_for_current_frame() {
    VkFence f = in_flight_fences_[current_frame_];
    vkWaitForFences(device_.device(), 1, &f, VK_TRUE, UINT64_MAX);
    vkResetFences(device_.device(), 1, &f);
}

void VulkanSwapchain::advance_frame() {
    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkResult VulkanSwapchain::acquire(uint32_t* out_image_index, VkSemaphore* out_image_available) {
    VkSemaphore sem = image_available_semaphores_[current_frame_];
    VkResult r = vkAcquireNextImageKHR(
        device_.device(), swapchain_, UINT64_MAX,
        sem, VK_NULL_HANDLE, out_image_index);
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
        if (out_image_available) *out_image_available = sem;
    }
    return r;
}

VkResult VulkanSwapchain::present(uint32_t image_index, VkSemaphore render_finished) {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_index;
    return vkQueuePresentKHR(device_.present_queue(), &pi);
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(device_.device());
    destroy_swapchain();
    width_ = width;
    height_ = height;
    create_swapchain();
}

// ---------------------------------------------------------------------------
// Compose + present — the one-shot frame publisher
// ---------------------------------------------------------------------------

bool VulkanSwapchain::compose_and_present(tgfx::TextureHandle color_tex) {
    using Clock = std::chrono::steady_clock;
    const bool stats_enabled = vulkan_stats_enabled();
    const auto timestamp = [stats_enabled] {
        return stats_enabled ? Clock::now() : Clock::time_point{};
    };
    const auto total0 = timestamp();

    // 1. Wait until the command buffer + sync objects for this slot
    //    are free.
    const auto wait0 = timestamp();
    wait_for_current_frame();
    const auto wait1 = timestamp();

    // 2. Acquire an image from the swapchain.
    uint32_t image_idx = 0;
    VkSemaphore image_available = VK_NULL_HANDLE;
    const auto acquire0 = timestamp();
    VkResult ar = acquire(&image_idx, &image_available);
    const auto acquire1 = timestamp();
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        // Nothing submitted this frame — fence stays signalled after
        // we reset it above, so re-signal it to keep things balanced.
        VkSubmitInfo empty{};
        empty.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(device_.graphics_queue(), 1, &empty,
                      in_flight_fences_[current_frame_]);
        return true; // caller should recreate
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        return true;
    }
    bool suboptimal = (ar == VK_SUBOPTIMAL_KHR);

    // 3. Look up the color texture on the device.
    VkTextureResource* rt = device_.get_texture(color_tex);
    if (!rt || !rt->image) {
        tc_log(TC_LOG_ERROR,
               "[VulkanSwapchain] compose_and_present: missing source texture id=%u",
               color_tex.id);
        // Can't compose — just submit an empty batch to signal the
        // fence and move on. The screen stays on the previous frame
        // which is fine for a dropped render.
        VkSubmitInfo empty{};
        empty.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        empty.waitSemaphoreCount = 1;
        empty.pWaitSemaphores = &image_available;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        empty.pWaitDstStageMask = &stage;
        vkQueueSubmit(device_.graphics_queue(), 1, &empty,
                      in_flight_fences_[current_frame_]);
        present(image_idx, render_finished_semaphores_[current_frame_]);
        advance_frame();
        return false;
    }

    // 4. Record the compose command buffer: transitions + blit +
    //    present-src transition.
    const auto record0 = timestamp();
    VkCommandBuffer cb = compose_command_buffers_[current_frame_];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);

    VkImage sc_image = images_[image_idx];
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    // rt_tex: current layout → TRANSFER_SRC.
    VkImageMemoryBarrier rt_to_src{};
    rt_to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    rt_to_src.oldLayout = rt->current_layout;
    rt_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    rt_to_src.image = rt->image;
    rt_to_src.subresourceRange = range;
    rt_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_WRITE_BIT;
    rt_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &rt_to_src);

    // swapchain image: UNDEFINED → TRANSFER_DST.
    VkImageMemoryBarrier sc_to_dst{};
    sc_to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sc_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sc_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    sc_to_dst.image = sc_image;
    sc_to_dst.subresourceRange = range;
    sc_to_dst.srcAccessMask = 0;
    sc_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &sc_to_dst);

    // Blit rt_tex → swapchain image (linear filter for any scale).
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(rt->desc.width),
                           static_cast<int32_t>(rt->desc.height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(width_),
                           static_cast<int32_t>(height_), 1};
    vkCmdBlitImage(cb,
        rt->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        sc_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);

    // swapchain image: TRANSFER_DST → PRESENT_SRC.
    VkImageMemoryBarrier sc_to_present{};
    sc_to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sc_to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    sc_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    sc_to_present.image = sc_image;
    sc_to_present.subresourceRange = range;
    sc_to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sc_to_present.dstAccessMask = 0;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &sc_to_present);

    // Reflect the transition on the tgfx2 side — next render pass
    // will see TRANSFER_SRC and transition appropriately.
    rt->current_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    vkEndCommandBuffer(cb);
    const auto record1 = timestamp();

    // 5. Submit waiting on image_available, signalling render_finished
    //    for this slot and tripping the in-flight fence.
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSemaphore render_done = render_finished_semaphores_[current_frame_];

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_done;
    const auto submit0 = timestamp();
    vkQueueSubmit(device_.graphics_queue(), 1, &si,
                  in_flight_fences_[current_frame_]);
    const auto submit1 = timestamp();

    // 6. Present — returns OUT_OF_DATE / SUBOPTIMAL if the surface
    //    lost sync with the window (typical after resize).
    const auto present0 = timestamp();
    VkResult pr = present(image_idx, render_done);
    const auto present1 = timestamp();
    bool should_recreate = suboptimal ||
                           pr == VK_ERROR_OUT_OF_DATE_KHR ||
                           pr == VK_SUBOPTIMAL_KHR;

    advance_frame();
    const auto total1 = timestamp();

    if (!stats_enabled) {
        return should_recreate;
    }

    auto& stats = compose_stats_;
    ++stats.frames;
    stats.wait_us += us_between(wait0, wait1);
    stats.acquire_us += us_between(acquire0, acquire1);
    stats.record_us += us_between(record0, record1);
    stats.submit_us += us_between(submit0, submit1);
    stats.present_us += us_between(present0, present1);
    stats.total_us += us_between(total0, total1);

    auto now = Clock::now();
    if (std::chrono::duration<double>(now - stats.window_start).count() >= 1.0) {
        const double denom = stats.frames > 0 ? static_cast<double>(stats.frames) : 1.0;
        tc_log(TC_LOG_INFO,
                   "[tgfx2-vulkan] swapchain stats: frames=%llu "
                   "avg_total_ms=%.3f avg_wait_ms=%.3f avg_acquire_ms=%.3f "
                   "avg_record_ms=%.3f avg_submit_ms=%.3f avg_present_ms=%.3f",
                   static_cast<unsigned long long>(stats.frames),
                   (stats.total_us / denom) / 1000.0,
                   (stats.wait_us / denom) / 1000.0,
                   (stats.acquire_us / denom) / 1000.0,
                   (stats.record_us / denom) / 1000.0,
                   (stats.submit_us / denom) / 1000.0,
               (stats.present_us / denom) / 1000.0);
        stats.window_start = now;
        stats.frames = 0;
        stats.wait_us = 0.0;
        stats.acquire_us = 0.0;
        stats.record_us = 0.0;
        stats.submit_us = 0.0;
        stats.present_us = 0.0;
        stats.total_us = 0.0;
    }
    return should_recreate;
}

bool VulkanSwapchain::clear_and_present(float r, float g, float b, float a) {
    wait_for_current_frame();

    uint32_t image_idx = 0;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkResult ar = acquire(&image_idx, &image_available);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        VkSubmitInfo empty{};
        empty.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(device_.graphics_queue(), 1, &empty,
                      in_flight_fences_[current_frame_]);
        return true;
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        return true;
    }
    bool suboptimal = (ar == VK_SUBOPTIMAL_KHR);

    VkCommandBuffer cb = compose_command_buffers_[current_frame_];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkImage image = images_[image_idx];
    VkImageMemoryBarrier to_dst{};
    to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.image = image;
    to_dst.subresourceRange = range;
    to_dst.srcAccessMask = 0;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_dst);

    VkClearColorValue clear{};
    clear.float32[0] = r;
    clear.float32[1] = g;
    clear.float32[2] = b;
    clear.float32[3] = a;
    vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);

    VkImageMemoryBarrier to_present{};
    to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.image = image;
    to_present.subresourceRange = range;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_present);

    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSemaphore render_done = render_finished_semaphores_[current_frame_];
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_done;
    vkQueueSubmit(device_.graphics_queue(), 1, &si,
                  in_flight_fences_[current_frame_]);

    VkResult pr = present(image_idx, render_done);
    bool should_recreate = suboptimal ||
                           pr == VK_ERROR_OUT_OF_DATE_KHR ||
                           pr == VK_SUBOPTIMAL_KHR;
    advance_frame();
    return should_recreate;
}

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
