#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"
#include "tgfx2/internal/shader_preprocess.hpp"

#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <set>

namespace tgfx {

// --- Debug callback ---

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

// --- Constructor / Destructor ---

VulkanRenderDevice::VulkanRenderDevice(const VulkanDeviceCreateInfo& info) {
    validation_enabled_ = info.enable_validation;
    init_instance(info);

    if (info.surface != VK_NULL_HANDLE) {
        surface_ = info.surface;
    }

    pick_physical_device();
    create_logical_device();
    create_allocator();
    create_command_pool();
    create_descriptor_pool();
    create_shared_layouts();

    // Query capabilities
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device_, &props);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical_device_, &features);

    caps_.backend = BackendType::Vulkan;
    caps_.max_texture_dimension_2d = props.limits.maxImageDimension2D;
    caps_.max_color_attachments = props.limits.maxColorAttachments;
    caps_.max_texture_units = props.limits.maxBoundDescriptorSets;
    caps_.supports_compute = true;
    caps_.supports_geometry_shaders = features.geometryShader;
    caps_.supports_timestamp_queries = (props.limits.timestampComputeAndGraphics != 0);
    caps_.supports_multisample_resolve = true;
}

VulkanRenderDevice::~VulkanRenderDevice() {
    if (device_) vkDeviceWaitIdle(device_);

    // Destroy cached framebuffers
    for (auto& [k, fb] : framebuffer_cache_)
        vkDestroyFramebuffer(device_, fb, nullptr);

    // Destroy cached render passes
    for (auto& [k, rp] : render_pass_cache_)
        vkDestroyRenderPass(device_, rp, nullptr);

    // Destroy resources
    for (auto& [id, r] : buffers_) {
        if (r.buffer) vmaDestroyBuffer(allocator_, r.buffer, r.allocation);
    }
    for (auto& [id, r] : textures_) {
        if (r.view) vkDestroyImageView(device_, r.view, nullptr);
        if (r.image) vmaDestroyImage(allocator_, r.image, r.allocation);
    }
    for (auto& [id, r] : samplers_) {
        if (r.sampler) vkDestroySampler(device_, r.sampler, nullptr);
    }
    for (auto& [id, r] : shaders_) {
        if (r.module) vkDestroyShaderModule(device_, r.module, nullptr);
    }
    for (auto& [id, r] : pipelines_) {
        if (r.pipeline) vkDestroyPipeline(device_, r.pipeline, nullptr);
        // layout and render_pass are shared/cached, cleaned separately
    }

    if (shared_pipeline_layout_) vkDestroyPipelineLayout(device_, shared_pipeline_layout_, nullptr);
    if (descriptor_set_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) vkDestroyDevice(device_, nullptr);

    if (debug_messenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(instance_, debug_messenger_, nullptr);
    }
    // Note: surface_ is NOT destroyed here — it's owned by the caller
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

// --- Instance ---

void VulkanRenderDevice::init_instance(const VulkanDeviceCreateInfo& info) {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "tgfx2";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "tgfx2";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = info.instance_extensions;
    std::vector<const char*> layers;

    if (validation_enabled_) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    if (validation_enabled_) {
        VkDebugUtilsMessengerCreateInfoEXT dbg_ci{};
        dbg_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg_ci.pfnUserCallback = debug_callback;

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (func) func(instance_, &dbg_ci, nullptr, &debug_messenger_);
    }
}

// --- Physical device ---

void VulkanRenderDevice::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Pick first discrete GPU, or first device
    physical_device_ = devices[0];
    for (auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physical_device_ = dev;
            break;
        }
    }

    // Find queue families
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, qf_props.data());

    bool found_graphics = false;
    bool found_present = false;

    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family_ = i;
            found_graphics = true;
        }
        if (surface_) {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
            if (present_support) {
                present_family_ = i;
                found_present = true;
            }
        }
        if (found_graphics && (found_present || !surface_)) break;
    }

    if (!found_graphics) throw std::runtime_error("No graphics queue family found");
}

// --- Logical device ---

void VulkanRenderDevice::create_logical_device() {
    std::set<uint32_t> unique_families = {graphics_family_};
    if (surface_) unique_families.insert(present_family_);

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis;
    for (uint32_t fam : unique_families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        queue_cis.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};
    features.fillModeNonSolid = VK_TRUE; // for wireframe

    std::vector<const char*> extensions;
    if (surface_) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos = queue_cis.data();
    ci.pEnabledFeatures = &features;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateDevice(physical_device_, &ci, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    if (surface_) {
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
    }
}

// --- VMA ---

void VulkanRenderDevice::create_allocator() {
    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice = physical_device_;
    ci.device = device_;
    ci.instance = instance_;
    ci.vulkanApiVersion = VK_API_VERSION_1_3;

    if (vmaCreateAllocator(&ci, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

// --- Command pool ---

void VulkanRenderDevice::create_command_pool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = graphics_family_;

    if (vkCreateCommandPool(device_, &ci, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

// --- Descriptor pool ---

void VulkanRenderDevice::create_descriptor_pool() {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
    };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets = 256;
    ci.poolSizeCount = 5;
    ci.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

// --- Shared layouts (MVP: universal layout) ---

void VulkanRenderDevice::create_shared_layouts() {
    // Universal descriptor set layout: binding 0-3 = UBO, 4-7 = sampled image, 8-11 = sampler
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < 4; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_ALL;
        bindings.push_back(b);
    }
    for (uint32_t i = 4; i < 8; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }
    for (uint32_t i = 8; i < 12; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_ci.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layout_ci, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &descriptor_set_layout_;

    if (vkCreatePipelineLayout(device_, &pl_ci, nullptr, &shared_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

// --- Capabilities ---

BackendCapabilities VulkanRenderDevice::capabilities() const { return caps_; }
void VulkanRenderDevice::wait_idle() { vkDeviceWaitIdle(device_); }

// --- Immediate command execution ---

void VulkanRenderDevice::execute_immediate(std::function<void(VkCommandBuffer)> fn) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    fn(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

// --- Image layout transition ---

void VulkanRenderDevice::transition_image_layout(
    VkCommandBuffer cmd, VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);
}

// --- Buffer ---

BufferHandle VulkanRenderDevice::create_buffer(const BufferDesc& desc) {
    VkBufferResource res;
    res.desc = desc;

    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = desc.size;
    ci.usage = vk::to_vk_buffer_usage(desc.usage);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = desc.cpu_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator_, &ci, &alloc_ci, &res.buffer, &res.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }

    return {buffers_.add(std::move(res))};
}

// --- Texture ---

TextureHandle VulkanRenderDevice::create_texture(const TextureDesc& desc) {
    VkTextureResource res;
    res.desc = desc;

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = vk::to_vk_format(desc.format);
    ci.extent = {desc.width, desc.height, 1};
    ci.mipLevels = desc.mip_levels;
    ci.arrayLayers = 1;
    ci.samples = static_cast<VkSampleCountFlagBits>(desc.sample_count);
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = vk::to_vk_image_usage(desc.usage);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, &ci, &alloc_ci, &res.image, &res.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image");
    }

    // Create image view
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = res.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = ci.format;
    view_ci.subresourceRange.aspectMask = vk::format_aspect_flags(desc.format);
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = desc.mip_levels;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_ci, nullptr, &res.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, res.image, res.allocation);
        throw std::runtime_error("Failed to create image view");
    }

    res.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return {textures_.add(std::move(res))};
}

// --- Sampler ---

SamplerHandle VulkanRenderDevice::create_sampler(const SamplerDesc& desc) {
    VkSamplerResource res;

    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = vk::to_vk_filter(desc.mag_filter);
    ci.minFilter = vk::to_vk_filter(desc.min_filter);
    ci.mipmapMode = vk::to_vk_mipmap_mode(desc.mip_filter);
    ci.addressModeU = vk::to_vk_address_mode(desc.address_u);
    ci.addressModeV = vk::to_vk_address_mode(desc.address_v);
    ci.addressModeW = vk::to_vk_address_mode(desc.address_w);
    ci.maxAnisotropy = desc.max_anisotropy;
    ci.anisotropyEnable = desc.max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    ci.compareEnable = desc.compare_enable ? VK_TRUE : VK_FALSE;
    ci.compareOp = vk::to_vk_compare(desc.compare_op);
    ci.maxLod = VK_LOD_CLAMP_NONE;

    if (vkCreateSampler(device_, &ci, nullptr, &res.sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create sampler");
    }

    return {samplers_.add(std::move(res))};
}

// --- Shader ---

ShaderHandle VulkanRenderDevice::create_shader(const ShaderDesc& desc) {
    std::vector<uint32_t> spirv;

    if (!desc.bytecode.empty()) {
        // Use provided SPIR-V
        spirv.resize(desc.bytecode.size() / 4);
        std::memcpy(spirv.data(), desc.bytecode.data(), desc.bytecode.size());
    } else if (!desc.source.empty()) {
        // Resolve #include / @features etc. through the shared
        // preprocessor hook, then compile the GLSL to SPIR-V via
        // shaderc. OpenGL runs the same preprocess step — shaders
        // stay identical across backends.
        std::string resolved = internal::preprocess_shader_source(desc.source);
        auto result = vk::compile_glsl_to_spirv(resolved, desc.stage, desc.entry_point);
        if (!result.success) {
            throw std::runtime_error("Shader compilation failed: " + result.error_message);
        }
        spirv = std::move(result.spirv);
    } else {
        return {0};
    }

    VkShaderResource res;
    res.stage = desc.stage;
    res.entry_point = desc.entry_point;

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();

    if (vkCreateShaderModule(device_, &ci, nullptr, &res.module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    return {shaders_.add(std::move(res))};
}

// --- Pipeline ---

PipelineHandle VulkanRenderDevice::create_pipeline(const PipelineDesc& desc) {
    VkPipelineResource res;
    res.desc = desc;
    res.layout = shared_pipeline_layout_;

    // Get or create render pass from formats
    std::vector<PixelFormat> color_fmts = desc.color_formats;
    if (color_fmts.empty()) color_fmts.push_back(PixelFormat::RGBA8_UNorm);
    bool needs_depth = desc.depth_stencil.depth_test || desc.depth_stencil.depth_write;
    res.render_pass = get_or_create_render_pass(
        color_fmts, desc.depth_format, needs_depth, desc.sample_count,
        LoadOp::Clear, LoadOp::Clear);

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    auto add_stage = [&](ShaderHandle h, VkShaderStageFlagBits vk_stage) {
        auto* sh = get_shader(h);
        if (!sh) return;
        VkPipelineShaderStageCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage = vk_stage;
        si.module = sh->module;
        si.pName = sh->entry_point.c_str();
        stages.push_back(si);
    };
    add_stage(desc.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
    add_stage(desc.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);
    if (desc.geometry_shader) add_stage(desc.geometry_shader, VK_SHADER_STAGE_GEOMETRY_BIT);

    // Vertex input
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (uint32_t i = 0; i < desc.vertex_layouts.size(); ++i) {
        const auto& vl = desc.vertex_layouts[i];
        VkVertexInputBindingDescription bd{};
        bd.binding = i;
        bd.stride = vl.stride;
        bd.inputRate = vl.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(bd);

        for (const auto& attr : vl.attributes) {
            VkVertexInputAttributeDescription ad{};
            ad.location = attr.location;
            ad.binding = i;
            ad.format = vk::to_vk_vertex_format(attr.format);
            ad.offset = attr.offset;
            attributes.push_back(ad);
        }
    }

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertex_input.pVertexBindingDescriptions = bindings.data();
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = vk::to_vk_topology(desc.topology);

    // Dynamic viewport/scissor
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = vk::to_vk_polygon_mode(desc.raster.polygon_mode);
    raster.cullMode = vk::to_vk_cull_mode(desc.raster.cull);
    raster.frontFace = vk::to_vk_front_face(desc.raster.front_face);
    raster.lineWidth = 1.0f;

    // Multisample
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.sample_count);

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = desc.depth_stencil.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = vk::to_vk_compare(desc.depth_stencil.depth_compare);

    // Blend
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable = desc.blend.enabled ? VK_TRUE : VK_FALSE;
    blend_att.srcColorBlendFactor = vk::to_vk_blend_factor(desc.blend.src_color);
    blend_att.dstColorBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_color);
    blend_att.colorBlendOp = vk::to_vk_blend_op(desc.blend.color_op);
    blend_att.srcAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.src_alpha);
    blend_att.dstAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_alpha);
    blend_att.alphaBlendOp = vk::to_vk_blend_op(desc.blend.alpha_op);
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    // Create pipeline
    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = static_cast<uint32_t>(stages.size());
    pi.pStages = stages.data();
    pi.pVertexInputState = &vertex_input;
    pi.pInputAssemblyState = &input_assembly;
    pi.pViewportState = &viewport_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &multisample;
    pi.pDepthStencilState = &depth_stencil;
    pi.pColorBlendState = &blend;
    pi.pDynamicState = &dynamic_state;
    pi.layout = res.layout;
    pi.renderPass = res.render_pass;
    pi.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &res.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    return {pipelines_.add(std::move(res))};
}

// --- Resource set ---

ResourceSetHandle VulkanRenderDevice::create_resource_set(const ResourceSetDesc& desc) {
    VkResourceSetResource res;
    res.desc = desc;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptor_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptor_set_layout_;

    if (vkAllocateDescriptorSets(device_, &ai, &res.descriptor_set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Write descriptors
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> buf_infos;
    std::vector<VkDescriptorImageInfo> img_infos;
    buf_infos.reserve(desc.bindings.size());
    img_infos.reserve(desc.bindings.size());

    for (const auto& b : desc.bindings) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = res.descriptor_set;
        w.dstBinding = b.binding;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;

        switch (b.kind) {
            case ResourceBinding::Kind::UniformBuffer:
            case ResourceBinding::Kind::StorageBuffer: {
                auto* buf = get_buffer(b.buffer);
                if (!buf) continue;
                buf_infos.push_back({buf->buffer, b.offset, b.range ? b.range : VK_WHOLE_SIZE});
                w.descriptorType = (b.kind == ResourceBinding::Kind::UniformBuffer)
                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.pBufferInfo = &buf_infos.back();
                break;
            }
            case ResourceBinding::Kind::SampledTexture: {
                auto* tex = get_texture(b.texture);
                if (!tex) continue;
                img_infos.push_back({VK_NULL_HANDLE, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                w.pImageInfo = &img_infos.back();
                break;
            }
            case ResourceBinding::Kind::Sampler: {
                auto* samp = get_sampler(b.sampler);
                if (!samp) continue;
                img_infos.push_back({samp->sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                w.pImageInfo = &img_infos.back();
                break;
            }
        }
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return {resource_sets_.add(std::move(res))};
}

// --- Destroy ---

TextureDesc VulkanRenderDevice::texture_desc(TextureHandle handle) const {
    // Re-declare a local VkTextureResource* lookup because the member
    // accessor is non-const; this is a read-only query so const_cast
    // is safe — the pool's map is the only thing we touch.
    auto* self = const_cast<VulkanRenderDevice*>(this);
    if (auto* r = self->textures_.get(handle.id)) {
        return r->desc;
    }
    return {};
}

void VulkanRenderDevice::destroy(BufferHandle h) {
    if (auto* r = buffers_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->buffer) vmaDestroyBuffer(allocator_, r->buffer, r->allocation);
        buffers_.remove(h.id);
    }
}

void VulkanRenderDevice::destroy(TextureHandle h) {
    if (auto* r = textures_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->view) vkDestroyImageView(device_, r->view, nullptr);
        if (r->image) vmaDestroyImage(allocator_, r->image, r->allocation);
        textures_.remove(h.id);
    }
}

void VulkanRenderDevice::destroy(SamplerHandle h) {
    if (auto* r = samplers_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->sampler) vkDestroySampler(device_, r->sampler, nullptr);
        samplers_.remove(h.id);
    }
}

void VulkanRenderDevice::destroy(ShaderHandle h) {
    if (auto* r = shaders_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->module) vkDestroyShaderModule(device_, r->module, nullptr);
        shaders_.remove(h.id);
    }
}

void VulkanRenderDevice::destroy(PipelineHandle h) {
    if (auto* r = pipelines_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->pipeline) vkDestroyPipeline(device_, r->pipeline, nullptr);
        pipelines_.remove(h.id);
    }
}

void VulkanRenderDevice::destroy(ResourceSetHandle h) {
    if (auto* r = resource_sets_.get(h.id)) {
        vkDeviceWaitIdle(device_);
        if (r->descriptor_set) {
            vkFreeDescriptorSets(device_, descriptor_pool_, 1, &r->descriptor_set);
        }
        resource_sets_.remove(h.id);
    }
}

// --- Upload ---

void VulkanRenderDevice::upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset) {
    auto* res = buffers_.get(dst.id);
    if (!res) return;

    if (res->desc.cpu_visible) {
        void* mapped;
        vmaMapMemory(allocator_, res->allocation, &mapped);
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, data.data(), data.size());
        vmaUnmapMemory(allocator_, res->allocation);
    } else {
        // Staging buffer
        VkBufferCreateInfo stage_ci{};
        stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_ci.size = data.size();
        stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stage_alloc{};
        stage_alloc.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer staging;
        VmaAllocation staging_alloc;
        vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc, &staging, &staging_alloc, nullptr);

        void* mapped;
        vmaMapMemory(allocator_, staging_alloc, &mapped);
        std::memcpy(mapped, data.data(), data.size());
        vmaUnmapMemory(allocator_, staging_alloc);

        execute_immediate([&](VkCommandBuffer cmd) {
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = offset;
            region.size = data.size();
            vkCmdCopyBuffer(cmd, staging, res->buffer, 1, &region);
        });

        vmaDestroyBuffer(allocator_, staging, staging_alloc);
    }
}

void VulkanRenderDevice::upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip) {
    auto* res = textures_.get(dst.id);
    if (!res) return;

    uint32_t w = std::max(1u, res->desc.width >> mip);
    uint32_t h = std::max(1u, res->desc.height >> mip);

    // Staging buffer
    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = data.size();
    stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stage_alloc_ci{};
    stage_alloc_ci.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer staging;
    VmaAllocation staging_alloc;
    vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc_ci, &staging, &staging_alloc, nullptr);

    void* mapped;
    vmaMapMemory(allocator_, staging_alloc, &mapped);
    std::memcpy(mapped, data.data(), data.size());
    vmaUnmapMemory(allocator_, staging_alloc);

    execute_immediate([&](VkCommandBuffer cmd) {
        transition_image_layout(cmd, res->image,
            res->current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            vk::format_aspect_flags(res->desc.format));

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = vk::format_aspect_flags(res->desc.format);
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {w, h, 1};

        vkCmdCopyBufferToImage(cmd, staging, res->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transition_image_layout(cmd, res->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            vk::format_aspect_flags(res->desc.format));
    });

    res->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vmaDestroyBuffer(allocator_, staging, staging_alloc);
}

void VulkanRenderDevice::upload_texture_region(TextureHandle /*dst*/,
                                               uint32_t /*x*/, uint32_t /*y*/,
                                               uint32_t /*w*/, uint32_t /*h*/,
                                               std::span<const uint8_t> /*data*/,
                                               uint32_t /*mip*/) {
    // TODO: implement proper region upload via staging buffer +
    // vkCmdCopyBufferToImage with VkBufferImageCopy.imageOffset /
    // imageExtent. The OpenGL path (glTexSubImage2D) is the primary
    // consumer right now; Vulkan gets a stub so the interface stays
    // abstract-class compatible.
}

// --- Readback ---

void VulkanRenderDevice::read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset) {
    auto* res = buffers_.get(src.id);
    if (!res) return;

    if (res->desc.cpu_visible) {
        void* mapped;
        vmaMapMemory(allocator_, res->allocation, &mapped);
        std::memcpy(data.data(), static_cast<uint8_t*>(mapped) + offset, data.size());
        vmaUnmapMemory(allocator_, res->allocation);
    } else {
        // Staging readback
        VkBufferCreateInfo stage_ci{};
        stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_ci.size = data.size();
        stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo stage_alloc{};
        stage_alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

        VkBuffer staging;
        VmaAllocation staging_alloc_h;
        vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc, &staging, &staging_alloc_h, nullptr);

        execute_immediate([&](VkCommandBuffer cmd) {
            VkBufferCopy region{};
            region.srcOffset = offset;
            region.size = data.size();
            vkCmdCopyBuffer(cmd, res->buffer, staging, 1, &region);
        });

        void* mapped;
        vmaMapMemory(allocator_, staging_alloc_h, &mapped);
        std::memcpy(data.data(), mapped, data.size());
        vmaUnmapMemory(allocator_, staging_alloc_h);
        vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
    }
}

// --- Command list ---

std::unique_ptr<ICommandList> VulkanRenderDevice::create_command_list(QueueType /*queue*/) {
    return std::make_unique<VulkanCommandList>(*this);
}

void VulkanRenderDevice::submit(ICommandList& cmd) {
    auto& vcmd = static_cast<VulkanCommandList&>(cmd);
    VkCommandBuffer cb = vcmd.command_buffer();

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;

    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_); // MVP: simple sync
}

void VulkanRenderDevice::present() {
    // Swapchain present — will be implemented with VulkanSwapchain
}

// --- RenderPass cache ---

VkRenderPass VulkanRenderDevice::get_or_create_render_pass(
    const std::vector<PixelFormat>& color_formats,
    PixelFormat depth_format, bool has_depth,
    uint32_t sample_count,
    LoadOp color_load, LoadOp depth_load)
{
    // Build key
    std::vector<VkFormat> key;
    for (auto f : color_formats) key.push_back(vk::to_vk_format(f));
    if (has_depth) key.push_back(vk::to_vk_format(depth_format));
    key.push_back(static_cast<VkFormat>(sample_count)); // encode sample count in key

    auto it = render_pass_cache_.find(key);
    if (it != render_pass_cache_.end()) return it->second;

    // Create render pass
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    auto to_vk_load = [](LoadOp op) -> VkAttachmentLoadOp {
        switch (op) {
            case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    for (size_t i = 0; i < color_formats.size(); ++i) {
        VkAttachmentDescription att{};
        att.format = vk::to_vk_format(color_formats[i]);
        att.samples = static_cast<VkSampleCountFlagBits>(sample_count);
        att.loadOp = to_vk_load(color_load);
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);

        color_refs.push_back({static_cast<uint32_t>(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    }

    VkAttachmentReference depth_ref{};
    if (has_depth) {
        VkAttachmentDescription att{};
        att.format = vk::to_vk_format(depth_format);
        att.samples = static_cast<VkSampleCountFlagBits>(sample_count);
        att.loadOp = to_vk_load(depth_load);
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);

        depth_ref = {static_cast<uint32_t>(attachments.size() - 1),
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    subpass.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = dep.srcStageMask;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci{};
    rp_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rp_ci.pAttachments = attachments.data();
    rp_ci.subpassCount = 1;
    rp_ci.pSubpasses = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies = &dep;

    VkRenderPass rp;
    if (vkCreateRenderPass(device_, &rp_ci, nullptr, &rp) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }

    render_pass_cache_[key] = rp;
    return rp;
}

// --- Framebuffer cache ---

VkFramebuffer VulkanRenderDevice::get_or_create_framebuffer(
    VkRenderPass render_pass,
    const std::vector<VkImageView>& attachments,
    uint32_t width, uint32_t height)
{
    auto it = framebuffer_cache_.find(attachments);
    if (it != framebuffer_cache_.end()) return it->second;

    VkFramebufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass = render_pass;
    ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    ci.pAttachments = attachments.data();
    ci.width = width;
    ci.height = height;
    ci.layers = 1;

    VkFramebuffer fb;
    if (vkCreateFramebuffer(device_, &ci, nullptr, &fb) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
    }

    framebuffer_cache_[attachments] = fb;
    return fb;
}

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
