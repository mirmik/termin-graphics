#pragma once

#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>

// VMA: forward-declare opaque handle (full header only needed in .cpp)
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

#include <map>
#include <unordered_map>
#include <vector>
#include <functional>

#include "tgfx2/tgfx2_api.h"
#include "tgfx2/i_render_device.hpp"

namespace tgfx {

// Internal Vulkan resource types

struct VkBufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    BufferDesc desc;
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
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    bool enable_validation = true;
    // Required instance extensions (e.g. from GLFW)
    std::vector<const char*> instance_extensions;
};

class TGFX2_API VulkanRenderDevice : public IRenderDevice {
public:
    explicit VulkanRenderDevice(const VulkanDeviceCreateInfo& info);
    ~VulkanRenderDevice() override;

    BackendCapabilities capabilities() const override;
    void wait_idle() override;

    BufferHandle create_buffer(const BufferDesc& desc) override;
    TextureHandle create_texture(const TextureDesc& desc) override;
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

    // Internal access for command list
    VkDevice device() const { return device_; }
    VkBufferResource* get_buffer(BufferHandle h) { return buffers_.get(h.id); }
    VkTextureResource* get_texture(TextureHandle h) { return textures_.get(h.id); }
    VkSamplerResource* get_sampler(SamplerHandle h) { return samplers_.get(h.id); }
    VkShaderResource* get_shader(ShaderHandle h) { return shaders_.get(h.id); }
    VkPipelineResource* get_pipeline(PipelineHandle h) { return pipelines_.get(h.id); }
    VkResourceSetResource* get_resource_set(ResourceSetHandle h) { return resource_sets_.get(h.id); }

    VkQueue graphics_queue() const { return graphics_queue_; }
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

private:
    void init_instance(const VulkanDeviceCreateInfo& info);
    void pick_physical_device();
    void create_logical_device();
    void create_allocator();
    void create_command_pool();
    void create_descriptor_pool();
    void create_shared_layouts();

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
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

    // Shared layouts (MVP: one universal layout)
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout shared_pipeline_layout_ = VK_NULL_HANDLE;

    BackendCapabilities caps_;

    VkHandlePool<VkBufferResource> buffers_;
    VkHandlePool<VkTextureResource> textures_;
    VkHandlePool<VkSamplerResource> samplers_;
    VkHandlePool<VkShaderResource> shaders_;
    VkHandlePool<VkPipelineResource> pipelines_;
    VkHandlePool<VkResourceSetResource> resource_sets_;

    // RenderPass cache (key: format config hash)
    std::map<std::vector<VkFormat>, VkRenderPass> render_pass_cache_;
    // Framebuffer cache
    std::map<std::vector<VkImageView>, VkFramebuffer> framebuffer_cache_;

    bool validation_enabled_ = false;
};

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
