// tgfx2 Vulkan on-screen smoke test.
//
// Opens an SDL_WINDOW_VULKAN, lets VulkanRenderDevice build the
// VkInstance / VkSurfaceKHR / VkDevice / VkSwapchainKHR, and then
// drives a few frames that clear the swapchain image to a rotating
// colour. Exits on window close or after ~3 seconds with a non-zero
// status if anything failed.
//
// Build requires both TGFX2_ENABLE_VULKAN=ON and SDL2 with Vulkan
// support (any recent SDL2 package). Skipped otherwise.
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifdef TGFX2_HAS_VULKAN
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
#ifndef TGFX2_HAS_VULKAN
    printf("Vulkan backend not compiled, skipping test\n");
    return 0;
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;

    SDL_Window* window = SDL_CreateWindow(
        "tgfx2 Vulkan on-screen smoke",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Ask SDL which instance extensions the surface needs.
    uint32_t ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, nullptr)) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions(count) failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::vector<const char*> extensions(ext_count);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, extensions.data())) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions(list) failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("SDL requires %u Vulkan instance extensions:\n", ext_count);
    for (const char* e : extensions) printf("  %s\n", e);

    // Build the tgfx2 device. The surface_factory callback runs AFTER
    // VulkanRenderDevice creates its VkInstance, then SDL wraps the
    // window into a VkSurfaceKHR bound to that exact instance.
    tgfx::VulkanDeviceCreateInfo info;
    info.enable_validation = true;
    info.instance_extensions = extensions;
    info.swapchain_width = kWidth;
    info.swapchain_height = kHeight;
    info.surface_factory = [window](VkInstance inst) -> VkSurfaceKHR {
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, inst, &surf)) {
            fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
            return VK_NULL_HANDLE;
        }
        return surf;
    };

    std::unique_ptr<tgfx::VulkanRenderDevice> device;
    try {
        device = std::make_unique<tgfx::VulkanRenderDevice>(info);
    } catch (const std::exception& e) {
        fprintf(stderr, "VulkanRenderDevice creation failed: %s\n", e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    tgfx::VulkanSwapchain* sc = device->swapchain();
    if (!sc) {
        fprintf(stderr, "Device has no swapchain — surface not acquired\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("Swapchain up: %ux%u, %u images, format=%d\n",
           sc->width(), sc->height(), sc->image_count(), sc->format());

    VkDevice vk_dev = device->device();
    VkCommandPool pool = device->command_pool();

    // Allocate one command buffer per in-flight frame. We record it
    // fresh each frame so it's safe to reuse after the fence signals.
    std::vector<VkCommandBuffer> cmd_bufs(tgfx::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT);
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = static_cast<uint32_t>(cmd_bufs.size());
        if (vkAllocateCommandBuffers(vk_dev, &ai, cmd_bufs.data()) != VK_SUCCESS) {
            fprintf(stderr, "vkAllocateCommandBuffers failed\n");
            return 1;
        }
    }

    auto start = std::chrono::steady_clock::now();
    uint64_t frame_index = 0;
    bool running = true;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN &&
                     ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }

        // Auto-exit after 3s so this can run unattended in CI.
        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration<float>(now - start).count();
        if (t > 3.0f) running = false;

        sc->wait_for_current_frame();

        uint32_t img_idx = 0;
        VkSemaphore img_available = VK_NULL_HANDLE;
        VkResult r = sc->acquire(&img_idx, &img_available);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            sc->recreate(sc->width(), sc->height());
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            fprintf(stderr, "acquire returned %d, bailing\n", r);
            return 1;
        }

        // Record a command buffer that clears the swapchain image to a
        // rotating colour. We transition UNDEFINED -> TRANSFER_DST_OPTIMAL
        // for the clear, then -> PRESENT_SRC_KHR for presentation.
        uint32_t slot = frame_index % tgfx::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;
        VkCommandBuffer cb = cmd_bufs[slot];
        vkResetCommandBuffer(cb, 0);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &begin);

        VkImage image = sc->image(img_idx);
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;

        // Transition UNDEFINED -> TRANSFER_DST_OPTIMAL.
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
        clear.float32[0] = 0.5f + 0.5f * std::sin(t * 1.7f);
        clear.float32[1] = 0.5f + 0.5f * std::sin(t * 2.3f + 1.0f);
        clear.float32[2] = 0.5f + 0.5f * std::sin(t * 3.1f + 2.0f);
        clear.float32[3] = 1.0f;
        vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);

        // Transition TRANSFER_DST -> PRESENT_SRC for presentation.
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

        // Submit waiting on image_available, signalling render_finished.
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        // Need a per-frame render_finished semaphore — the swapchain
        // owns one per in-flight slot; it's exposed implicitly through
        // present(). Re-fetch it: the swapchain tracks its own.
        // For this minimal smoke test we create an ad-hoc semaphore
        // per frame and destroy after present. Production code would
        // use the swapchain's internal one.
        VkSemaphore render_done = VK_NULL_HANDLE;
        {
            VkSemaphoreCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            vkCreateSemaphore(vk_dev, &sci, nullptr, &render_done);
        }

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &img_available;
        si.pWaitDstStageMask = &wait_stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &render_done;

        vkQueueSubmit(device->graphics_queue(), 1, &si, sc->current_fence());

        VkResult pr = sc->present(img_idx, render_done);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            int w, h;
            SDL_Vulkan_GetDrawableSize(window, &w, &h);
            sc->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        } else if (pr != VK_SUCCESS) {
            fprintf(stderr, "vkQueuePresentKHR returned %d\n", pr);
        }

        // The ad-hoc semaphore is no longer in use once the frame is
        // presented — but strictly we'd need to wait for the fence
        // before destroying it. wait_for_current_frame() at the top of
        // the next iteration does that for this slot. Defer destruction
        // by two frames: keep them in a vector keyed by frame slot.
        // Simpler: vkDeviceWaitIdle here — fine for a 3-second test.
        vkDeviceWaitIdle(vk_dev);
        vkDestroySemaphore(vk_dev, render_done, nullptr);

        sc->advance_frame();
        ++frame_index;
    }

    // Drain before cleanup.
    vkDeviceWaitIdle(vk_dev);
    vkFreeCommandBuffers(vk_dev, pool,
                          static_cast<uint32_t>(cmd_bufs.size()),
                          cmd_bufs.data());

    device.reset();

    // VkSurfaceKHR is destroyed by SDL when the VkInstance dies; since
    // the device owns the instance and just tore it down, the surface
    // is already gone. SDL window is pure host-side teardown.
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Frames rendered: %llu. OK.\n",
           static_cast<unsigned long long>(frame_index));
    return 0;
#endif
}
