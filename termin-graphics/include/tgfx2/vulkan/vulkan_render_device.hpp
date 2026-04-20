#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>

// VMA: forward-declare opaque handle (full header only needed in .cpp)
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

#include <atomic>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_render_device.hpp"

namespace tgfx {

class VulkanSwapchain;

// Internal Vulkan resource types

struct VkBufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    BufferDesc desc;
    // Non-null for host-visible buffers created with
    // VMA_ALLOCATION_CREATE_MAPPED_BIT — upload_buffer just memcpy's
    // into this pointer, no map/unmap churn per upload.
    void* mapped_ptr = nullptr;
};

struct VkTextureResource {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    TextureDesc desc;
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VkSamplerResource {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VkShaderResource {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage;
    std::string entry_point = "main";
};

struct VkPipelineResource {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    PipelineDesc desc;
};

struct VkResourceSetResource {
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    ResourceSetDesc desc;
    // Dynamic offsets emitted at bind time, in the order the layout
    // declares DYNAMIC bindings (currently 0, 1, 2, 3, 16 → 5 offsets).
    // Populated by create_resource_set() from ResourceBinding::offset on
    // the matching slots; unset slots stay at 0 so the descriptor is
    // always valid (ring buffer is range-checked against WHOLE_SIZE).
    static constexpr uint32_t DYNAMIC_UBO_COUNT = 5;
    uint32_t dynamic_offsets[DYNAMIC_UBO_COUNT] = {0, 0, 0, 0, 0};
};

// Reuse HandlePool from OpenGL backend concept
template<typename T>
class VkHandlePool {
public:
    uint32_t add(T&& resource) {
        uint32_t id = next_id_++;
        pool_.emplace(id, std::move(resource));
        return id;
    }
    T* get(uint32_t id) {
        auto it = pool_.find(id);
        return (it != pool_.end()) ? &it->second : nullptr;
    }
    bool remove(uint32_t id) { return pool_.erase(id) > 0; }
    auto begin() { return pool_.begin(); }
    auto end() { return pool_.end(); }
private:
    std::unordered_map<uint32_t, T> pool_;
    uint32_t next_id_ = 1;
};

// Initialization params
struct VulkanDeviceCreateInfo {
    bool enable_validation = true;

    // Required instance extensions. The typical SDL / GLFW flow fills
    // this from SDL_Vulkan_GetInstanceExtensions / glfwGetRequiredInstanceExtensions.
    std::vector<const char*> instance_extensions;

    // Pre-existing VkSurfaceKHR (rare — mostly for embedded hosts that
    // create their own VkInstance). Usually left null; use `surface_factory`
    // instead.
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // Called AFTER the device creates its VkInstance, to produce a
    // surface bound to the host window. SDL clients set this to a
    // lambda wrapping SDL_Vulkan_CreateSurface(win, inst, &surf).
    // If null (and `surface` is also null), the device stays
    // offscreen-only — no swapchain.
    std::function<VkSurfaceKHR(VkInstance)> surface_factory;

    // Initial swapchain extent in physical pixels. Only used when a
    // surface is present. Ignored otherwise. The swapchain may clamp
    // this to the surface's min/max caps.
    uint32_t swapchain_width = 0;
    uint32_t swapchain_height = 0;
};

class TGFX2_API VulkanRenderDevice : public IRenderDevice {
public:
    explicit VulkanRenderDevice(const VulkanDeviceCreateInfo& info);
    ~VulkanRenderDevice() override;

    BackendType backend_type() const override { return BackendType::Vulkan; }
    BackendCapabilities capabilities() const override;
    void wait_idle() override;

    BufferHandle create_buffer(const BufferDesc& desc) override;
    TextureHandle create_texture(const TextureDesc& desc) override;
    TextureDesc texture_desc(TextureHandle handle) const override;
    SamplerHandle create_sampler(const SamplerDesc& desc) override;
    ShaderHandle create_shader(const ShaderDesc& desc) override;
    PipelineHandle create_pipeline(const PipelineDesc& desc) override;
    ResourceSetHandle create_resource_set(const ResourceSetDesc& desc) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(ResourceSetHandle handle) override;

    void upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset = 0) override;
    void upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip = 0) override;
    void upload_texture_region(TextureHandle dst,
                               uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h,
                               std::span<const uint8_t> data,
                               uint32_t mip = 0) override;
    void read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset = 0) override;

    std::unique_ptr<ICommandList> create_command_list(QueueType queue = QueueType::Graphics) override;
    void submit(ICommandList& cmd) override;
    void present() override;

    // Backend-neutral replacements for blit_to_external_target /
    // clear_external_target. The destination is a tgfx2 TextureHandle,
    // letting render surfaces own their composite target as a texture
    // instead of a raw GL FBO id (which has no Vulkan analogue).
    void blit_to_texture(
        TextureHandle dst,
        TextureHandle src,
        int src_x, int src_y, int src_w, int src_h,
        int dst_x, int dst_y, int dst_w, int dst_h) override;

    void clear_texture(
        TextureHandle dst,
        float r, float g, float b, float a,
        int viewport_x, int viewport_y,
        int viewport_w, int viewport_h) override;

    // Internal access for command list
    VkDevice device() const { return device_; }
    VkBufferResource* get_buffer(BufferHandle h) { return buffers_.get(h.id); }
    VkTextureResource* get_texture(TextureHandle h) { return textures_.get(h.id); }
    VkSamplerResource* get_sampler(SamplerHandle h) { return samplers_.get(h.id); }
    VkShaderResource* get_shader(ShaderHandle h) { return shaders_.get(h.id); }
    VkPipelineResource* get_pipeline(PipelineHandle h) { return pipelines_.get(h.id); }
    VkResourceSetResource* get_resource_set(ResourceSetHandle h) { return resource_sets_.get(h.id); }

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    VkQueue present_queue() const { return present_queue_; }
    uint32_t graphics_queue_family() const { return graphics_family_; }
    uint32_t present_queue_family() const { return present_family_; }
    VkCommandPool command_pool() const { return command_pool_; }
    VmaAllocator allocator() const { return allocator_; }

    // Get or create a VkRenderPass for the given format configuration
    VkRenderPass get_or_create_render_pass(
        const std::vector<PixelFormat>& color_formats,
        PixelFormat depth_format, bool has_depth,
        uint32_t sample_count,
        LoadOp color_load, LoadOp depth_load);

    // Get or create a VkFramebuffer
    VkFramebuffer get_or_create_framebuffer(
        VkRenderPass render_pass,
        const std::vector<VkImageView>& attachments,
        uint32_t width, uint32_t height);

    // Execute a one-shot command buffer (for uploads, layout transitions)
    void execute_immediate(std::function<void(VkCommandBuffer)> fn);

    // Transition image layout
    void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout old_layout, VkImageLayout new_layout,
                                  VkImageAspectFlags aspect);

    // Shared descriptor set layout (MVP: fixed layout for all pipelines)
    VkDescriptorSetLayout descriptor_set_layout() const { return descriptor_set_layout_; }
    VkPipelineLayout shared_pipeline_layout() const { return shared_pipeline_layout_; }

    // Lazy-create a default linear/clamp sampler used when a caller
    // binds a SampledTexture without an explicit sampler. One per
    // device; destroyed with the device. Returned as raw VkSampler so
    // the descriptor write path can drop it into VkDescriptorImageInfo
    // without translating through SamplerHandle.
    VkSampler ensure_default_sampler();

    // --- Ring UBO (for dynamic-offset descriptor path) ------------------
    //
    // A single large host-visible, persistently-mapped VkBuffer that
    // accumulates per-draw UBO data across one frame. Callers (render-pass
    // code, SkinnedMeshRenderer, material params) write their block via
    // `ring_ubo_write(data, size)` which returns an aligned byte offset;
    // the offset is then handed to `vkCmdBindDescriptorSets` as a dynamic
    // offset for bindings declared UNIFORM_BUFFER_DYNAMIC.
    //
    // Replaces per-draw UBO buffers + per-draw descriptor-set allocations.
    // Expected to drop resource-sets/sec by ~2 orders of magnitude — see
    // memory/vulkan_perf_dynamic_ubo_plan.md for the rationale.
    //
    // Head pointer is reset at the start of every submit() after the
    // previous frame's fence signals (GPU has finished reading the ring's
    // previous contents). Wraparound mid-frame would corrupt in-flight
    // data, so the buffer is sized generously (16 MB ≈ 10 KB × 1600 draws)
    // and an overflow is an error we log rather than silently wrap.
    uint32_t ring_ubo_write(const void* data, uint32_t size) override;
    VkBuffer ring_ubo_buffer() const { return ring_ubo_buffer_; }
    // The ring buffer exposed as a normal BufferHandle so that ring-backed
    // UBO bindings route through the existing ResourceBinding path
    // (create_resource_set treats any binding on this handle as a DYNAMIC
    // UBO and emits the offset as a dynamic descriptor offset).
    BufferHandle ring_ubo_handle() const override { return ring_ubo_handle_; }
    // minUniformBufferOffsetAlignment from VkPhysicalDeviceLimits. 256 on
    // NVIDIA desktop, 64 on AMD desktop, up to 256 on mobile. Used by the
    // ring writer to round each allocation up to a legal dynamic offset,
    // and by the shared descriptor-set creator to set buffer range = max
    // UBO block size (cannot exceed buffer range at bind time).
    uint32_t ubo_alignment() const override { return ubo_alignment_; }

    // Non-null when the device was created with a surface (via
    // `info.surface` or `info.surface_factory`). Hosts drive on-screen
    // frames through this — acquire() at start of frame, present() at
    // end. Offscreen-only devices return nullptr.
    VulkanSwapchain* swapchain() const { return swapchain_.get(); }

private:
    void init_instance(const VulkanDeviceCreateInfo& info);
    void pick_physical_device();
    void create_logical_device();
    void create_allocator();
    void create_command_pool();
    void create_descriptor_pool();
    void create_shared_layouts();
    void create_ring_ubo();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_family_ = 0;
    uint32_t present_family_ = 0;

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;

    // Double-buffered descriptor pools. Each frame allocates descriptor
    // sets out of `descriptor_pools_[current_pool_idx_]`; at `submit()`
    // (after the fence of the *other* pool signals → its sets are no
    // longer referenced by the GPU), we swap and `vkResetDescriptorPool`
    // the freshly-retired pool in one call — cheaper than freeing each
    // set with `vkFreeDescriptorSets`, and completely removes the "pool
    // fills up across the frame" failure mode.
    VkDescriptorPool descriptor_pools_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint32_t current_pool_idx_ = 0;

    // Per-pool descriptor-set cache keyed on the hash of
    // ResourceSetDesc::bindings. A draw that binds the same UBOs and
    // samplers as an earlier draw in the same frame reuses the already-
    // allocated VkDescriptorSet instead of paying for another
    // vkAllocateDescriptorSets + vkUpdateDescriptorSets. Cleared when
    // the corresponding pool is reset in `submit()`.
    std::unordered_map<uint64_t, ResourceSetHandle> descriptor_cache_[2];

    // Shared layouts (MVP: one universal layout)
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout shared_pipeline_layout_ = VK_NULL_HANDLE;

    // Lazy-created default sampler (see ensure_default_sampler()).
    VkSampler default_sampler_ = VK_NULL_HANDLE;

    BackendCapabilities caps_;

    VkHandlePool<VkBufferResource> buffers_;
    VkHandlePool<VkTextureResource> textures_;
    VkHandlePool<VkSamplerResource> samplers_;
    VkHandlePool<VkShaderResource> shaders_;
    VkHandlePool<VkPipelineResource> pipelines_;
    VkHandlePool<VkResourceSetResource> resource_sets_;

    std::unique_ptr<VulkanSwapchain> swapchain_;

    // RenderPass cache (key: format config hash)
    std::map<std::vector<VkFormat>, VkRenderPass> render_pass_cache_;
    // Framebuffer cache
    std::map<std::vector<VkImageView>, VkFramebuffer> framebuffer_cache_;

    bool validation_enabled_ = false;

    // --- Frame sync / deferred destroy -----------------------------------
    //
    // One fence tracks the most-recent `submit()`. Every `destroy(XxxHandle)`
    // queues the handle into `pending_destroy_current_`; at the next submit
    // we wait on the in-flight fence (usually no real wait — GPU has caught
    // up), then drain `pending_destroy_in_flight_` (resources from the
    // previous frame, now GPU-safe to release). The current-frame queue
    // becomes the next in-flight queue.
    //
    // This replaces the previous `vkDeviceWaitIdle`-per-destroy + `vkQueue-
    // WaitIdle`-per-submit pattern, which stalled the CPU on every frame
    // end and every deferred-destroyed resource set. On a typical editor
    // frame that's 50-200 device-wide waits serialised into the hot path.
    // With fence-based sync, rendering can overlap with CPU work and
    // destroys only block if the GPU really hasn't finished yet.
    //
    // Callers must keep handles untouched after `destroy()` returns — the
    // actual Vk objects are freed later, but `HandlePool` entries stay
    // reserved until then, so reading through a destroyed handle returns
    // stale (but valid memory) pointers. That's fine: it matches the
    // previous behavior except for the lack of immediate wait.
    struct PendingDestroyQueue {
        std::vector<BufferHandle> buffers;
        std::vector<TextureHandle> textures;
        std::vector<SamplerHandle> samplers;
        std::vector<ShaderHandle> shaders;
        std::vector<PipelineHandle> pipelines;
        std::vector<ResourceSetHandle> resource_sets;
        // Raw VkCommandBuffers freed via `defer_cmd_buffer_free()` — used
        // by VulkanCommandList's destructor to avoid freeing while the
        // buffer is still in-flight on the queue.
        std::vector<VkCommandBuffer> cmd_buffers;
        // Raw (VkBuffer, VmaAllocation) pairs from staging buffers that
        // the immediate-cb batch is still going to read. Freed after the
        // frame fence signals.
        std::vector<std::pair<VkBuffer, VmaAllocation>> vma_buffers;
        bool empty() const {
            return buffers.empty() && textures.empty() && samplers.empty()
                && shaders.empty() && pipelines.empty()
                && resource_sets.empty() && cmd_buffers.empty()
                && vma_buffers.empty();
        }
    };
    VkFence frame_fence_ = VK_NULL_HANDLE;
    bool frame_fence_in_flight_ = false;
    PendingDestroyQueue pending_destroy_current_;
    PendingDestroyQueue pending_destroy_in_flight_;

    // Drain `q` now — actually free the underlying Vk objects. Caller is
    // responsible for ensuring GPU has finished using these resources.
    void drain_pending_destroy(PendingDestroyQueue& q);

public:
    // Queue a VkCommandBuffer for deferred release. Called from
    // VulkanCommandList's destructor — freeing a command buffer while its
    // work is still in-flight on the queue is UB. The buffer will be
    // released together with other in-flight resources after the next
    // fence signal in `submit()`.
    void defer_cmd_buffer_free(VkCommandBuffer cb) {
        if (cb != VK_NULL_HANDLE) pending_destroy_current_.cmd_buffers.push_back(cb);
    }

    // Queue a staging-style VMA buffer for deferred destroy after the
    // frame fence signals. Used by upload_buffer / upload_texture /
    // blit_to_texture / read_buffer — they fill a staging buffer, batch
    // a copy into immediate_cb_, and must NOT destroy the staging
    // synchronously because the GPU hasn't executed the copy yet.
    void defer_vma_buffer_destroy(VkBuffer buffer, VmaAllocation alloc) {
        if (buffer != VK_NULL_HANDLE) {
            pending_destroy_current_.vma_buffers.emplace_back(buffer, alloc);
        }
    }

    // Get (and lazily open) the shared immediate command buffer. Every
    // copy / layout-transition / clear that used to be its own submit
    // now records into this single cb; it's ended and flushed inside
    // `submit()` as the first entry of the frame's multi-cb submit.
    // One vkQueueSubmit per frame replaces the previous ~200 tiny
    // submit + vkQueueWaitIdle pairs that dominated Vulkan CPU time.
    VkCommandBuffer ensure_immediate_cb();

private:
    VkCommandBuffer immediate_cb_ = VK_NULL_HANDLE;
    bool immediate_cb_open_ = false;

    // Ring UBO — backing storage for UNIFORM_BUFFER_DYNAMIC bindings.
    // Created once in create_ring_ubo(), freed in ~VulkanRenderDevice.
    //
    // Split into two halves (slots). Frame N records into slot X; while
    // its GPU work is in-flight, frame N+1 records into slot 1-X. After
    // the fence for frame N signals (next submit's wait), slot X becomes
    // reusable. The flip / reset pattern is identical to descriptor_pools_
    // above and piggybacks on the same frame_fence_ guarantee. Writing
    // into a single head would race: the just-submitted frame's GPU read
    // overlaps the next frame's host write into the same offset range.
    VkBuffer      ring_ubo_buffer_     = VK_NULL_HANDLE;
    VmaAllocation ring_ubo_allocation_ = VK_NULL_HANDLE;
    void*         ring_ubo_mapped_     = nullptr;
    uint64_t      ring_ubo_size_       = 0;  // total (both slots)
    uint64_t      ring_ubo_slot_size_  = 0;  // = ring_ubo_size_ / 2
    // Per-slot head — advances with every write into that slot, reset to
    // 0 when we flip INTO the slot in submit(). Atomic for forward-compat
    // with multi-threaded recording; only the render thread writes today,
    // so relaxed ordering is sufficient.
    std::atomic<uint64_t> ring_ubo_heads_[2] = {};
    uint32_t ring_ubo_slot_idx_ = 0;
    // BufferHandle that aliases ring_ubo_buffer_ in buffers_. Used by the
    // RenderContext2::bind_uniform_buffer_ring() path and recognised by
    // create_resource_set() / bind_resource_set() to emit a dynamic offset
    // rather than update the descriptor on each bind.
    BufferHandle ring_ubo_handle_ = {};
    // Cached VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment.
    uint32_t ubo_alignment_ = 256;
};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
