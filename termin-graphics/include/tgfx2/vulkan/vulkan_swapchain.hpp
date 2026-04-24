// vulkan_swapchain.hpp - VkSwapchainKHR wrapper for on-screen presentation.
//
// Owns the Vulkan swapchain, per-image views, and per-frame sync primitives
// (image_available / render_finished semaphores + in-flight fence). The
// host drives frames through acquire()/present(); the render device
// itself is swapchain-agnostic.
#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/handles.hpp"

namespace tgfx {

class VulkanRenderDevice;

class TGFX2_API VulkanSwapchain {
public:
    // Number of CPU/GPU frames in flight. Must be >=1. 2 is a sensible
    // default — one GPU frame is queued while the next is being recorded
    // on the CPU. Larger numbers trade memory for smoother latency at
    // the cost of input lag.
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

private:
    VulkanRenderDevice& device_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR color_space_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;

    // Per-in-flight-frame sync. Indexed by `current_frame_` which
    // wraps on MAX_FRAMES_IN_FLIGHT.
    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;

    // Per-in-flight-frame command buffer for compose_and_present.
    // Allocated once at swapchain construction, reused each cycle.
    std::vector<VkCommandBuffer> compose_command_buffers_;

    uint32_t current_frame_ = 0;

public:
    VulkanSwapchain(VulkanRenderDevice& dev,
                    VkSurfaceKHR surface,
                    uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Acquire the next image. On VK_SUCCESS `out_image_index` is set
    // and the returned semaphore is the one the caller must wait on
    // before using the acquired image. On VK_ERROR_OUT_OF_DATE_KHR /
    // VK_SUBOPTIMAL_KHR the caller should recreate() and retry next
    // frame. Any other error is fatal (logged + VK_NULL_HANDLE
    // returned).
    VkResult acquire(uint32_t* out_image_index, VkSemaphore* out_image_available);

    // Present the given image. `render_finished` is the semaphore that
    // the caller signalled when it submitted its command buffer —
    // presentation waits on it before actually putting the image on
    // screen. Returns the result of vkQueuePresentKHR; OUT_OF_DATE /
    // SUBOPTIMAL should trigger a recreate() on the next frame.
    VkResult present(uint32_t image_index, VkSemaphore render_finished);

    // Recreate the swapchain at a new size (e.g. on window resize
    // or after an OUT_OF_DATE from acquire/present). Waits for the
    // device to be idle first — safe to call while a previous frame
    // might still be in flight.
    void recreate(uint32_t width, uint32_t height);

    // In-flight frame management. The host calls wait_for_current_frame()
    // at the start of each frame before recording commands; this
    // blocks on the matching VkFence until the previous GPU submission
    // for this frame slot has completed, guaranteeing that the
    // semaphores and command buffers we're about to reuse are no
    // longer in use by the GPU. advance_frame() increments the slot
    // for the next frame and is called after present().
    void wait_for_current_frame();
    void advance_frame();
    VkFence current_fence() const { return in_flight_fences_[current_frame_]; }

    // One-shot helper: composite `color_tex` onto the next swapchain
    // image and publish the frame. Handles the full wait → acquire →
    // layout transitions → vkCmdBlitImage → submit → present →
    // advance cycle internally, so the host only needs one call per
    // frame. `color_tex` must have been created on the same device
    // as this swapchain and can be any size — it's scaled to the
    // swapchain extent with VK_FILTER_LINEAR.
    //
    // Returns whether a recreate is recommended (OUT_OF_DATE /
    // SUBOPTIMAL from acquire or present). True means the caller
    // should call recreate(w, h) before the next frame.
    bool compose_and_present(tgfx::TextureHandle color_tex);

    // Introspection
    VkSwapchainKHR handle() const { return swapchain_; }
    VkFormat format() const { return format_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t image_count() const { return static_cast<uint32_t>(images_.size()); }
    VkImage image(uint32_t i) const { return images_[i]; }
    VkImageView image_view(uint32_t i) const { return image_views_[i]; }

private:
    void create_swapchain();
    void destroy_swapchain();
    void create_sync_objects();
    void destroy_sync_objects();
};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
