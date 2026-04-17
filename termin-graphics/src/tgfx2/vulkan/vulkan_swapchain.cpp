#ifdef TGFX2_HAS_VULKAN

#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#include "tgfx2/vulkan/vulkan_render_device.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace tgfx {

namespace {

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
}

VulkanSwapchain::~VulkanSwapchain() {
    if (device_.device()) {
        vkDeviceWaitIdle(device_.device());
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

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
