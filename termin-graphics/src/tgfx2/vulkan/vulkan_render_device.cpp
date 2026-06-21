#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"
#include "vulkan_spirv_reflection.hpp"
#include "vulkan_stats.hpp"
#include "tgfx2/internal/shader_preprocess.hpp"
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_texture.h>
#include <tgfx/resources/tc_texture_registry.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
}

#ifdef __ANDROID__
static constexpr uint32_t TGFX2_VULKAN_RUNTIME_API_VERSION = VK_API_VERSION_1_0;
#else
static constexpr uint32_t TGFX2_VULKAN_RUNTIME_API_VERSION = VK_API_VERSION_1_3;
#endif

// Trampolines for destroy-hook C callbacks. Defined at file scope (not in
// an anonymous namespace) so their addresses are stable identifiers that
// `tc_*_registry_remove_destroy_hook` can match against.
static void vulkan_invalidate_tc_texture_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::VulkanRenderDevice*>(user)->invalidate_tc_texture_cache(pool_index);
}

static void vulkan_invalidate_tc_mesh_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::VulkanRenderDevice*>(user)->invalidate_tc_mesh_cache(pool_index);
}

static void vulkan_invalidate_tc_shader_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::VulkanRenderDevice*>(user)->invalidate_tc_shader_cache(pool_index);
}

namespace tgfx {

namespace {

bool vulkan_stats_enabled() {
#ifdef __ANDROID__
    return true;
#else
    static const bool enabled = [] {
        const char* env = std::getenv("TGFX2_VULKAN_STATS");
        return env && env[0] == '1';
    }();
    return enabled;
#endif
}


} // namespace

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

static bool physical_device_supports_extension(
    VkPhysicalDevice physical_device,
    const char* extension_name)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(
            physical_device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, extension_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool contains_extension(
    const std::vector<const char*>& extensions,
    const char* extension_name)
{
    return std::find_if(
        extensions.begin(),
        extensions.end(),
        [extension_name](const char* current) {
            return current && std::strcmp(current, extension_name) == 0;
        }) != extensions.end();
}

// --- Constructor / Destructor ---

VulkanRenderDevice::VulkanRenderDevice(const VulkanDeviceCreateInfo& info) {
    validation_enabled_ = info.enable_validation;
    device_extensions_ = info.device_extensions;
    init_instance(info);

    // Resolve the surface. Two supply paths:
    //   1. info.surface — pre-made surface (embedded hosts with their
    //      own VkInstance via shared layers; rare).
    //   2. info.surface_factory — a callback invoked with our freshly
    //      created instance. This is the SDL/GLFW path: the host
    //      supplies required instance extensions via
    //      info.instance_extensions, then hands us a factory that
    //      wraps SDL_Vulkan_CreateSurface(window, instance, &surf)
    //      / glfwCreateWindowSurface(instance, window, ...).
    if (info.surface != VK_NULL_HANDLE) {
        surface_ = info.surface;
    } else if (info.surface_factory) {
        surface_ = info.surface_factory(instance_);
        if (surface_ == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanRenderDevice: surface_factory returned VK_NULL_HANDLE");
        }
    }

    if (info.physical_device_selector) {
        physical_device_ = info.physical_device_selector(instance_);
        if (physical_device_ == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanRenderDevice: physical_device_selector returned VK_NULL_HANDLE");
        }
    }
    pick_physical_device();
    create_logical_device();
    create_allocator();
    create_command_pool();
    create_descriptor_pool();
    create_ring_ubo();
    create_transient_vertex_ring();

    // Frame fences track in-flight frame slots. Created unsignaled; a slot
    // waits only after it has been submitted at least once.
    {
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (uint32_t i = 0; i < kFrameSlotCount; ++i) {
            if (vkCreateFence(device_, &fci, nullptr, &frame_fences_[i]) != VK_SUCCESS) {
                throw std::runtime_error("VulkanRenderDevice: vkCreateFence failed");
            }
        }
    }

    // Build the swapchain now that queues/allocator are ready. A
    // surface without a size is a hosting bug; refuse to guess.
    if (surface_ != VK_NULL_HANDLE) {
        if (info.swapchain_width == 0 || info.swapchain_height == 0) {
            throw std::runtime_error(
                "VulkanRenderDevice: surface provided but swapchain_width/height is 0");
        }
        swapchain_ = std::make_unique<VulkanSwapchain>(
            *this, surface_, info.swapchain_width, info.swapchain_height);
    }

    // Query capabilities
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device_, &props);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical_device_, &features);

    caps_.backend = BackendType::Vulkan;
    caps_.texture_origin_top_left = true;
    caps_.max_texture_dimension_2d = props.limits.maxImageDimension2D;
    caps_.max_color_attachments = props.limits.maxColorAttachments;
    caps_.max_texture_units = props.limits.maxBoundDescriptorSets;
    caps_.supports_compute = true;
    caps_.supports_geometry_shaders = features.geometryShader;
    caps_.supports_timestamp_queries = (props.limits.timestampComputeAndGraphics != 0);
    caps_.supports_multisample_resolve = true;

    // Subscribe to registry destroy-hooks so per-device tc_texture /
    // tc_mesh / tc_shader caches get invalidated before a slot is recycled. The
    // matching unregister calls live in the destructor.
    tc_texture_registry_add_destroy_hook(
        &vulkan_invalidate_tc_texture_trampoline, this);
    tc_mesh_registry_add_destroy_hook(
        &vulkan_invalidate_tc_mesh_trampoline, this);
    tc_shader_registry_add_destroy_hook(
        &vulkan_invalidate_tc_shader_trampoline, this);
}

VulkanRenderDevice::~VulkanRenderDevice() {
    // Unsubscribe from registry destroy-hooks before tearing anything down
    // so incoming tc_*_destroy calls after this point can't call into a
    // half-destroyed device.
    tc_shader_registry_remove_destroy_hook(
        &vulkan_invalidate_tc_shader_trampoline, this);
    tc_texture_registry_remove_destroy_hook(
        &vulkan_invalidate_tc_texture_trampoline, this);
    tc_mesh_registry_remove_destroy_hook(
        &vulkan_invalidate_tc_mesh_trampoline, this);

    // Shutdown is the one place where a full device-wide wait is actually
    // correct: everything queued must finish before we tear down VkImages,
    // VkBuffers, VkShaderModules etc. (freeing while in-flight is UB).
    // During normal frame lifecycle `destroy()` queues into
    // pending_destroy_{current,in_flight}_ and the per-submit fence keeps
    // them safe — see VulkanRenderDevice::submit.
    if (device_) vkDeviceWaitIdle(device_);

    // Tear down the swapchain first — its sync objects and image
    // views are bound to device_ which is still alive at this point.
    swapchain_.reset();

    // Drop per-device tc_texture / tc_mesh / tc_shader caches. The Vulkan
    // objects those caches point at are owned through the handle pools
    // (buffers_, textures_) and get released by the blanket per-pool loops
    // below, so we just need to clear the cache maps here — no explicit
    // destroy() calls required.
    tc_texture_cache_.clear();
    tc_mesh_cache_.clear();
    tc_shader_cache_.clear();

    // Deferred-destroy queues are now safe to release (GPU is idle).
    // Slot queues represent handles freed by submitted frames; `current`
    // holds handles freed during work that was never submitted. Handle-pool
    // lookups are still valid at this point, so drain them through the
    // normal per-type helper.
    for (uint32_t i = 0; i < kFrameSlotCount; ++i) {
        complete_pixel_readbacks(pixel_readbacks_slots_[i]);
        drain_pending_destroy(pending_destroy_slots_[i]);
    }
    destroy_pixel_readbacks(pixel_readbacks_current_);
    drain_pending_destroy(pending_destroy_current_);

    // Destroy cached framebuffers
    for (auto& [k, fb] : framebuffer_cache_)
        vkDestroyFramebuffer(device_, fb, nullptr);

    // Destroy cached render passes
    for (auto& [k, rp] : render_pass_cache_)
        vkDestroyRenderPass(device_, rp, nullptr);

    // Destroy any resources that were never explicitly `destroy()`-ed and
    // are still live in the handle pools. Keeps shutdown leak-free for
    // callers that forgot to clean up.
    for (auto& [id, r] : buffers_) {
        // Some pool entries alias device-owned buffers. The ring UBO
        // handle is the common case: descriptor binding needs a stable
        // BufferHandle, but the real VkBuffer/VMA allocation lifetime is
        // managed by ring_ubo_buffer_/ring_ubo_allocation_ below. VMA
        // must only destroy entries that actually own an allocation.
        if (r.buffer && r.allocation) {
            vmaDestroyBuffer(allocator_, r.buffer, r.allocation);
        }
    }
    for (auto& [id, r] : textures_) {
        if (r.view) vkDestroyImageView(device_, r.view, nullptr);
        if (r.image && !r.external) vmaDestroyImage(allocator_, r.image, r.allocation);
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

    if (immediate_cb_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_, command_pool_, 1, &immediate_cb_);
        immediate_cb_ = VK_NULL_HANDLE;
    }
    if (ring_ubo_buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, ring_ubo_buffer_, ring_ubo_allocation_);
        ring_ubo_buffer_ = VK_NULL_HANDLE;
        ring_ubo_allocation_ = VK_NULL_HANDLE;
        ring_ubo_mapped_ = nullptr;
    }
    if (transient_vb_buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, transient_vb_buffer_, transient_vb_allocation_);
        transient_vb_buffer_ = VK_NULL_HANDLE;
        transient_vb_allocation_ = VK_NULL_HANDLE;
        transient_vb_mapped_ = nullptr;
    }
    for (VkFence fence : frame_fences_) {
        if (fence) vkDestroyFence(device_, fence, nullptr);
    }
    if (default_sampler_) vkDestroySampler(device_, default_sampler_, nullptr);
    for (auto& [dsl, pl] : pipeline_layout_cache_) {
        if (pl) vkDestroyPipelineLayout(device_, pl, nullptr);
    }
    pipeline_layout_cache_.clear();
    for (auto& [hash, dsl] : descriptor_layout_cache_) {
        if (dsl) vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
    }
    descriptor_layout_cache_.clear();
    descriptor_layout_bindings_.clear();
    for (VkDescriptorPool pool : descriptor_pools_) {
        if (pool) vkDestroyDescriptorPool(device_, pool, nullptr);
    }
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_) vkDestroyDevice(device_, nullptr);

    if (debug_messenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(instance_, debug_messenger_, nullptr);
    }
    // Surface is a child of instance — must go BEFORE instance
    // destruction. We own it (either passed via info.surface, where
    // caller cedes ownership, or produced by surface_factory).
    if (surface_ && instance_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
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
    app_info.apiVersion = TGFX2_VULKAN_RUNTIME_API_VERSION;

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
    if (physical_device_ == VK_NULL_HANDLE) {
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
    if (surface_ && !found_present) {
        throw std::runtime_error("No present queue family found for Vulkan surface");
    }
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

    VkPhysicalDeviceFeatures supported_features{};
    vkGetPhysicalDeviceFeatures(physical_device_, &supported_features);

    VkPhysicalDeviceFeatures features{};
    if (supported_features.fillModeNonSolid) {
        features.fillModeNonSolid = VK_TRUE; // for wireframe
    } else {
        tc_log_info("VulkanRenderDevice: fillModeNonSolid unsupported; wireframe pipelines disabled");
    }
    // Shadow shaders index `sampler2DShadow u_shadow_map[N]` with a
    // runtime loop variable. In Vulkan that requires the
    // `shaderSampledImageArrayDynamicIndexing` feature — without it
    // access is undefined and shadow lookups silently return 1.0 (no
    // shadow) on most drivers. Matches GL's always-available dynamic
    // indexing of sampler arrays.
    if (supported_features.shaderSampledImageArrayDynamicIndexing) {
        features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    } else {
        tc_log_info(
            "VulkanRenderDevice: shaderSampledImageArrayDynamicIndexing unsupported; "
            "shadow sampler-array dynamic indexing disabled");
    }

    std::vector<const char*> extensions = device_extensions_;
    if (surface_) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // NDC-Z convention: Vulkan-native Z ∈ [0, 1]. OpenGL reaches the
    // same convention via a one-time glClipControl(GL_UPPER_LEFT,
    // GL_ZERO_TO_ONE) in OpenGLRenderDevice, and scene/shadow
    // projection matrices (see termin-base/geom/mat44.hpp) build
    // matrices that target exactly that. No VK_EXT_depth_clip_control
    // needed.
    VkPhysicalDeviceVulkan11Features vulkan11_features{};
    vulkan11_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceFeatures2 supported_features2{};
    supported_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported_features2.pNext = &vulkan11_features;
    auto get_physical_device_features2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2"));
    if (!get_physical_device_features2) {
        get_physical_device_features2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2KHR"));
    }
    if (get_physical_device_features2) {
        get_physical_device_features2(physical_device_, &supported_features2);
    } else {
        tc_log_info(
            "VulkanRenderDevice: vkGetPhysicalDeviceFeatures2 unavailable; "
            "Vulkan 1.1 feature query skipped");
    }

    VkPhysicalDeviceVulkan11Features enabled_vulkan11_features{};
    enabled_vulkan11_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    if (vulkan11_features.shaderDrawParameters) {
        enabled_vulkan11_features.shaderDrawParameters = VK_TRUE;
    } else if (physical_device_supports_extension(
                   physical_device_,
                   VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME)) {
        if (!contains_extension(extensions, VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
        }
    } else {
        tc_log_info(
            "VulkanRenderDevice: shaderDrawParameters unsupported; "
            "Slang shaders using SV_InstanceID with BaseInstance may fail validation");
    }

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos = queue_cis.data();
    ci.pEnabledFeatures = &features;
    ci.pNext = enabled_vulkan11_features.shaderDrawParameters
        ? &enabled_vulkan11_features
        : nullptr;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateDevice(physical_device_, &ci, nullptr, &device_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "Failed to create Vulkan logical device: vkCreateDevice result=" +
            std::to_string(static_cast<int>(result)));
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
    ci.vulkanApiVersion = TGFX2_VULKAN_RUNTIME_API_VERSION;

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
    // One pool per frame slot. Each frame's draws
    // allocate from `descriptor_pools_[current_pool_idx_]`; at submit(),
    // after the next slot's fence signals, we flip and
    // `vkResetDescriptorPool` it in one call — no individual
    // `vkFreeDescriptorSets`, no FREE_DESCRIPTOR_SET_BIT flag (the driver
    // can use a faster bump allocator internally).
    //
    // Pool size covers a single frame's worst case for chronosquad-like
    // scenes: ~30 skinned meshes × 4 shadow cascades + color/depth/id/
    // normal + UI draws ≈ 500-800 sets. Headroom to 2048 accommodates
    // heavier future scenes without re-sizing. Each set may touch up to
    // ~5 UBOs and up to the shared layout's sampled descriptors, hence
    // the pool-size multipliers below.
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,  8 * 2048},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          2 * 2048},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          512},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,           2 * 2048},
        {VK_DESCRIPTOR_TYPE_SAMPLER,                 2 * 2048},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 40 * 2048},
    };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = 0;  // No per-set free — pool reset frees everything at once.
    ci.maxSets = 2048;
    ci.poolSizeCount = 6;
    ci.pPoolSizes = pool_sizes;

    for (uint32_t i = 0; i < kFrameSlotCount; ++i) {
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptor_pools_[i])
                != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }
}

// --- Ring UBO ---

void VulkanRenderDevice::create_ring_ubo() {
    // Cache the alignment required by dynamic UBO offsets. Every write into
    // the ring rounds up to this granularity. Queried from the physical
    // device rather than hard-coded because it varies: 256 on NVIDIA desktop,
    // 64 on AMD desktop, 256 on most mobile GPUs.
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device_, &props);
    ubo_alignment_ = static_cast<uint32_t>(
        std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 1));

    // 8 MB per submit slot. Budget ~10 KB UBO data per draw ×
    // 1600 worst-case draws/frame = 16 MB needed, so per-slot headroom is
    // tight on adversarial scenes; logged (rare) overflow falls back to offset 0.
    // If that ever fires in practice, grow the ring or shrink per-pass UBOs.
    ring_ubo_size_ = static_cast<uint64_t>(kFrameSlotCount) * 8 * 1024 * 1024;
    ring_ubo_slot_size_ = ring_ubo_size_ / kFrameSlotCount;

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = ring_ubo_size_;
    // TRANSFER_DST kept cheap — we don't copy into the ring (host writes only),
    // but leaving it set costs nothing and makes future debug copies legal.
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info{};
    if (vmaCreateBuffer(allocator_, &bi, &ai, &ring_ubo_buffer_,
                        &ring_ubo_allocation_, &alloc_info) != VK_SUCCESS) {
        throw std::runtime_error("VulkanRenderDevice: failed to allocate ring UBO");
    }
    ring_ubo_mapped_ = alloc_info.pMappedData;

    // Query the memory type's coherency flag so writes can skip
    // vmaFlushAllocation on desktop Linux drivers (always coherent on
    // NVIDIA/AMD discrete).
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);
    ring_ubo_coherent_ =
        (mem_props.memoryTypes[alloc_info.memoryType].propertyFlags
         & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    for (auto& head : ring_ubo_heads_) {
        head.store(0, std::memory_order_relaxed);
    }
    ring_ubo_slot_idx_ = 0;

    // Expose the ring as a BufferHandle so callers can route per-draw UBO
    // bindings through it via the normal ResourceBinding path. The entry
    // in buffers_ carries no allocation of its own (VK_NULL_HANDLE alloc),
    // and destroy(ring_ubo_handle_) is a no-op guarded below — the real
    // buffer lives until ~VulkanRenderDevice.
    VkBufferResource ring_res{};
    ring_res.buffer = ring_ubo_buffer_;
    ring_res.allocation = VK_NULL_HANDLE; // not owned by the handle pool
    ring_res.desc.size = ring_ubo_size_;
    ring_res.desc.usage = BufferUsage::Uniform;
    ring_res.desc.cpu_visible = true;
    ring_res.mapped_ptr = ring_ubo_mapped_;
    ring_ubo_handle_.id = buffers_.add(std::move(ring_res));
}

uint32_t VulkanRenderDevice::ring_ubo_write(const void* data, uint32_t size) {
    // Reserve an aligned slice at the head. fetch_add returns the pre-update
    // value → that's our offset within the slot. The advance is aligned(size)
    // so the NEXT allocation starts on a legal dynamic-offset boundary.
    const uint32_t align = ubo_alignment_;
    const uint32_t padded = (size + align - 1) & ~(align - 1);

    const uint32_t slot = ring_ubo_slot_idx_;
    const uint64_t base = static_cast<uint64_t>(slot) * ring_ubo_slot_size_;

    uint64_t offset_in_slot =
        ring_ubo_heads_[slot].fetch_add(padded, std::memory_order_relaxed);

    if (offset_in_slot + padded > ring_ubo_slot_size_) {
        // Per-slot overflow: rest of the frame's UBO writes are invalid.
        // Log once so it's obvious in a profile run; return the slot base
        // so the caller still gets a legal (but stale) offset instead of
        // crashing. A real fix either raises ring_ubo_size_ or trims a pass.
        static thread_local bool s_warned = false;
        if (!s_warned) {
            tc_log(TC_LOG_ERROR,
                   "[RingUBO] slot %u overflow: head=%llu size=%u slot_cap=%llu — raise ring_ubo_size_",
                   slot, (unsigned long long)offset_in_slot, size,
                   (unsigned long long)ring_ubo_slot_size_);
            s_warned = true;
        }
        return static_cast<uint32_t>(base);
    }

    const uint64_t offset = base + offset_in_slot;
    // Persistent-mapped memcpy is the whole host-side cost on desktop
    // Linux (HOST_COHERENT → flush is a no-op). The coherent-aware skip
    // drops the vmaFlushAllocation call overhead on the hundreds of
    // ring_ubo_write()s per frame.
    std::memcpy(static_cast<uint8_t*>(ring_ubo_mapped_) + offset, data, size);
    if (!ring_ubo_coherent_) {
        vmaFlushAllocation(allocator_, ring_ubo_allocation_, offset, size);
    }
    return static_cast<uint32_t>(offset);
}

// --- Transient vertex ring ---

void VulkanRenderDevice::create_transient_vertex_ring() {
    transient_vb_size_ = static_cast<uint64_t>(kFrameSlotCount) * 2 * 1024 * 1024;
    transient_vb_slot_size_ = transient_vb_size_ / kFrameSlotCount;

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = transient_vb_size_;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info{};
    if (vmaCreateBuffer(allocator_, &bi, &ai, &transient_vb_buffer_,
                        &transient_vb_allocation_, &alloc_info) != VK_SUCCESS) {
        throw std::runtime_error("VulkanRenderDevice: failed to allocate transient vertex ring");
    }
    transient_vb_mapped_ = alloc_info.pMappedData;

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);
    transient_vb_coherent_ =
        (mem_props.memoryTypes[alloc_info.memoryType].propertyFlags
         & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    for (auto& head : transient_vb_heads_) {
        head.store(0, std::memory_order_relaxed);
    }
    transient_vb_slot_idx_ = 0;

    VkBufferResource res{};
    res.buffer = transient_vb_buffer_;
    res.allocation = VK_NULL_HANDLE;
    res.desc.size = transient_vb_size_;
    res.desc.usage = BufferUsage::Vertex;
    res.desc.cpu_visible = true;
    res.mapped_ptr = transient_vb_mapped_;
    transient_vb_handle_.id = buffers_.add(std::move(res));
}

uint64_t VulkanRenderDevice::transient_vertex_write(const void* data, uint32_t size) {
    if (!data || size == 0 || size > transient_vb_slot_size_ ||
        transient_vb_mapped_ == nullptr || transient_vb_handle_.id == 0) {
        return UINT64_MAX;
    }

    constexpr uint64_t align = 16;
    const uint64_t padded = (static_cast<uint64_t>(size) + align - 1) & ~(align - 1);
    const uint32_t slot = transient_vb_slot_idx_;
    const uint64_t base = static_cast<uint64_t>(slot) * transient_vb_slot_size_;
    uint64_t offset_in_slot =
        transient_vb_heads_[slot].fetch_add(padded, std::memory_order_relaxed);

    if (offset_in_slot + padded > transient_vb_slot_size_) {
        static thread_local bool s_warned = false;
        if (!s_warned) {
            tc_log(TC_LOG_ERROR,
                   "[TransientVB] slot %u overflow: head=%llu size=%u slot_cap=%llu",
                   slot, (unsigned long long)offset_in_slot, size,
                   (unsigned long long)transient_vb_slot_size_);
            s_warned = true;
        }
        return UINT64_MAX;
    }

    const uint64_t offset = base + offset_in_slot;
    std::memcpy(static_cast<uint8_t*>(transient_vb_mapped_) + offset, data, size);
    if (!transient_vb_coherent_) {
        vmaFlushAllocation(allocator_, transient_vb_allocation_, offset, size);
    }
    return offset;
}

VkSampler VulkanRenderDevice::ensure_default_sampler() {
    if (default_sampler_) return default_sampler_;
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(device_, &sci, nullptr, &default_sampler_);
    return default_sampler_;
}

TextureHandle VulkanRenderDevice::ensure_default_sampled_texture() {
    if (default_sampled_texture_) {
        return default_sampled_texture_;
    }

    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.mip_levels = 1;
    desc.sample_count = 1;
    desc.format = PixelFormat::RGBA8_UNorm;
    desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;

    default_sampled_texture_ = create_texture(desc);
    const uint8_t white[] = {255, 255, 255, 255};
    upload_texture(default_sampled_texture_, std::span<const uint8_t>(white, sizeof(white)));
    return default_sampled_texture_;
}

// --- Capabilities ---

BackendCapabilities VulkanRenderDevice::capabilities() const { return caps_; }
void VulkanRenderDevice::wait_idle() { vkDeviceWaitIdle(device_); }

void VulkanRenderDevice::invalidate_descriptor_cache() {
    for (auto& cache : descriptor_cache_) {
        for (const auto& [_, handle] : cache) {
            resource_sets_.remove(handle.id);
        }
        cache.clear();
    }
}

void VulkanRenderDevice::invalidate_render_target_cache() {
    for (auto& [_, fb] : framebuffer_cache_) {
        if (fb != VK_NULL_HANDLE) {
            pending_destroy_current_.framebuffers.push_back(fb);
        }
    }
    framebuffer_cache_.clear();
    invalidate_descriptor_cache();
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

    // Uniform buffers are effectively always per-frame data (camera
    // matrices, material params, bones, ...) — uploaded every frame
    // and read once. Placing them in HOST_VISIBLE|DEVICE_LOCAL memory
    // (CPU_TO_GPU) lets upload_buffer go through a plain map+memcpy,
    // skipping the staging buffer + execute_immediate + vkQueueWaitIdle
    // path entirely. Without this, a frame with ~30 UBO uploads would
    // do ~30 full GPU stalls — the dominant Vulkan perf regression vs
    // OpenGL on shadow/color passes.
    //
    // The caller-set `cpu_visible` still forces HOST_VISIBLE for other
    // use cases (e.g. readback staging mirrors). Vertex/index buffers
    // and large static textures stay GPU_ONLY as intended.
    const bool want_host_visible =
        desc.cpu_visible ||
        (static_cast<uint32_t>(desc.usage & BufferUsage::Uniform) != 0);
    res.desc.cpu_visible = want_host_visible;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = want_host_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU
                                       : VMA_MEMORY_USAGE_GPU_ONLY;
    // Persistently-mapped host-visible buffers: VMA keeps a pointer on
    // `VmaAllocationInfo::pMappedData`, so upload_buffer skips
    // vmaMapMemory/vmaUnmapMemory and does a plain memcpy.
    if (want_host_visible) {
        alloc_ci.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo alloc_info{};
    if (vmaCreateBuffer(allocator_, &ci, &alloc_ci,
                        &res.buffer, &res.allocation, &alloc_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }
    res.mapped_ptr = alloc_info.pMappedData;  // NULL for GPU-only buffers

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

    // If the caller flagged this as a sampled texture, pre-transition to
    // SHADER_READ_ONLY_OPTIMAL so a bind that happens before any
    // upload / blit / render-pass write doesn't see UNDEFINED at submit
    // time. Contents are undefined until something writes in — that's
    // the caller's concern — but the layout matches the descriptor the
    // pipeline expects. begin_render_pass will transition out to
    // COLOR/DEPTH attachment if the texture is used as an attachment
    // first. Cheap (one immediate barrier, no allocation).
    bool is_sampled = (static_cast<uint32_t>(desc.usage) &
                       static_cast<uint32_t>(TextureUsage::Sampled)) != 0;
    if (is_sampled) {
        VkImage image = res.image;
        VkImageAspectFlags aspect = vk::format_aspect_flags(desc.format);
        execute_immediate([&](VkCommandBuffer cb) {
            transition_image_layout(cb, image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                aspect);
        });
        res.current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    return {textures_.add(std::move(res))};
}

TextureHandle VulkanRenderDevice::register_external_texture(
    uintptr_t native_handle,
    const TextureDesc& desc
) {
    VkImage image = reinterpret_cast<VkImage>(native_handle);
    if (image == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanRenderDevice::register_external_texture: null VkImage");
    }

    VkTextureResource res;
    res.image = image;
    res.desc = desc;
    res.external = true;
    res.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = res.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk::to_vk_format(desc.format);
    view_ci.subresourceRange.aspectMask = vk::format_aspect_flags(desc.format);
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = desc.mip_levels;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_ci, nullptr, &res.view) != VK_SUCCESS) {
        throw std::runtime_error("VulkanRenderDevice::register_external_texture: vkCreateImageView failed");
    }

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

// --- Destroy ---

TextureDesc VulkanRenderDevice::texture_desc(TextureHandle handle) const {
    auto* self = const_cast<VulkanRenderDevice*>(this);
    if (auto* r = self->textures_.get(handle.id)) {
        return r->desc;
    }
    return {};
}

uintptr_t VulkanRenderDevice::pipeline_resource_layout_token(PipelineHandle pipeline) const {
    auto* self = const_cast<VulkanRenderDevice*>(this);
    if (auto* p = self->pipelines_.get(pipeline.id)) {
        return reinterpret_cast<uintptr_t>(p->descriptor_set_layout);
    }
    tc_log(TC_LOG_ERROR,
           "VulkanRenderDevice: pipeline_resource_layout_token pipeline=%u NOT FOUND",
           pipeline.id);
    return 0;
}

uintptr_t VulkanRenderDevice::pipeline_descriptor_set_layout(PipelineHandle pipeline) const {
    return pipeline_resource_layout_token(pipeline);
}

// All `destroy(*Handle)` calls queue the handle into
// `pending_destroy_current_`; actual Vk releases happen after the
// current frame's fence signals (see `submit()`). Caller's handle is
// invalid to use after this returns — the pool entry stays reserved
// until drain time.

void VulkanRenderDevice::destroy(BufferHandle h) {
    if (h.id == 0) return;
    // The ring UBO handle aliases a device-owned buffer; its real lifetime
    // is tied to the device, and destroying it mid-frame would wipe the
    // UBO store for every pending dynamic-offset binding.
    if (ring_ubo_handle_.id != 0 && h.id == ring_ubo_handle_.id) return;
    if (transient_vb_handle_.id != 0 && h.id == transient_vb_handle_.id) return;
    invalidate_descriptor_cache();
    pending_destroy_current_.buffers.push_back(h);
}

void VulkanRenderDevice::destroy(TextureHandle h) {
    if (h.id != 0) {
        invalidate_descriptor_cache();
        pending_destroy_current_.textures.push_back(h);
    }
}

void VulkanRenderDevice::destroy(SamplerHandle h) {
    if (h.id != 0) {
        invalidate_descriptor_cache();
        pending_destroy_current_.samplers.push_back(h);
    }
}

void VulkanRenderDevice::destroy(ShaderHandle h) {
    if (h.id != 0) pending_destroy_current_.shaders.push_back(h);
}

void VulkanRenderDevice::destroy(PipelineHandle h) {
    if (h.id != 0) pending_destroy_current_.pipelines.push_back(h);
}

void VulkanRenderDevice::destroy(ResourceSetHandle h) {
    if (h.id != 0) pending_destroy_current_.resource_sets.push_back(h);
}

void VulkanRenderDevice::drain_pending_destroy(PendingDestroyQueue& q) {
    for (auto h : q.buffers) {
        if (auto* r = buffers_.get(h.id)) {
            if (r->buffer) vmaDestroyBuffer(allocator_, r->buffer, r->allocation);
            buffers_.remove(h.id);
        }
    }
    for (auto h : q.textures) {
        if (auto* r = textures_.get(h.id)) {
            if (r->view) vkDestroyImageView(device_, r->view, nullptr);
            if (r->image && !r->external) vmaDestroyImage(allocator_, r->image, r->allocation);
            textures_.remove(h.id);
        }
    }
    for (auto h : q.samplers) {
        if (auto* r = samplers_.get(h.id)) {
            if (r->sampler) vkDestroySampler(device_, r->sampler, nullptr);
            samplers_.remove(h.id);
        }
    }
    for (auto h : q.shaders) {
        if (auto* r = shaders_.get(h.id)) {
            if (r->module) vkDestroyShaderModule(device_, r->module, nullptr);
            shaders_.remove(h.id);
        }
    }
    for (auto h : q.pipelines) {
        if (auto* r = pipelines_.get(h.id)) {
            if (r->pipeline) vkDestroyPipeline(device_, r->pipeline, nullptr);
            pipelines_.remove(h.id);
        }
    }
    // Resource-set handles are owned by the per-pool descriptor cache —
    // removed together with the VkDescriptorSet when the pool is reset
    // (see `descriptor_cache_[current_pool_idx_].clear()` in submit()).
    // Do NOT remove from HandlePool here: the same handle id may still
    // be live in the cache for its slot, and a subsequent cache-hit
    // would otherwise return a dangling entry.
    (void)q.resource_sets;
    for (auto fb : q.framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
    }
    for (auto cb : q.cmd_buffers) {
        vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
    }
    for (auto& [buf, alloc] : q.vma_buffers) {
        vmaDestroyBuffer(allocator_, buf, alloc);
    }
    q = {};
}


// --- Command list ---

std::unique_ptr<ICommandList> VulkanRenderDevice::create_command_list(QueueType /*queue*/) {
    return std::make_unique<VulkanCommandList>(*this);
}

void VulkanRenderDevice::prepare_frame_slot(uint32_t slot) {
    if (frame_fence_in_flight_[slot]) {
        vkWaitForFences(device_, 1, &frame_fences_[slot], VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &frame_fences_[slot]);
        frame_fence_in_flight_[slot] = false;
    }
    complete_pixel_readbacks(pixel_readbacks_slots_[slot]);
    drain_pending_destroy(pending_destroy_slots_[slot]);

    vkResetDescriptorPool(device_, descriptor_pools_[slot], 0);
    for (const auto& [_, h] : descriptor_cache_[slot]) {
        resource_sets_.remove(h.id);
    }
    descriptor_cache_[slot].clear();

    ring_ubo_heads_[slot].store(0, std::memory_order_relaxed);
    transient_vb_heads_[slot].store(0, std::memory_order_relaxed);
}

void VulkanRenderDevice::submit(ICommandList& cmd) {
    auto& vcmd = static_cast<VulkanCommandList&>(cmd);
    VkCommandBuffer main_cb = vcmd.command_buffer();

    // Per-second Vulkan hot-path summary. Counts since last submit, and
    // cumulative over the ~1s sliding window: draws, resource-set
    // allocations, and pipeline creations. Pipelines should drop to ~0
    // after startup if the PipelineCache is doing its job; resource
    // sets should scale with draws (one per pipeline-state change).
    static thread_local uint64_t s_submits = 0;
    static thread_local auto     s_window_start = std::chrono::steady_clock::now();
    static thread_local uint64_t s_last_fence_wait_us = 0;

    const uint32_t submitted_slot = current_pool_idx_;
    PendingDestroyQueue submitted_destroy = std::move(pending_destroy_current_);
    pending_destroy_current_ = {};

    // Close the immediate cb (copies / transitions / clears accumulated
    // since last submit) and submit it together with the main draw cb in
    // ONE vkQueueSubmit. Queue-submit ordering guarantees immediate_cb's
    // GPU work completes before the main cb starts — no explicit barrier
    // needed for the "staging → device UBO, then draw reads UBO" pattern.
    VkCommandBuffer cbs[2];
    uint32_t cb_count = 0;
    VkCommandBuffer submitted_immediate_cb = VK_NULL_HANDLE;
    if (immediate_cb_open_) {
        vkEndCommandBuffer(immediate_cb_);
        immediate_cb_open_ = false;
        cbs[cb_count++] = immediate_cb_;
        submitted_immediate_cb = immediate_cb_;
        immediate_cb_ = VK_NULL_HANDLE;  // next frame allocates a fresh one
    }
    cbs[cb_count++] = main_cb;

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = cb_count;
    si.pCommandBuffers = cbs;

    auto t_submit0 = std::chrono::steady_clock::now();
    vkQueueSubmit(graphics_queue_, 1, &si, frame_fences_[submitted_slot]);
    auto t_submit1 = std::chrono::steady_clock::now();
    g_submit_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_submit1 - t_submit0).count(),
        std::memory_order_relaxed);
    frame_fence_in_flight_[submitted_slot] = true;

    // Defer-free the immediate cb we just submitted — it's in-flight on
    // the graphics queue, so vkFreeCommandBuffers can only run after the
    // frame fence signals. The drain at the top of the NEXT submit()
    // picks this up.
    if (submitted_immediate_cb != VK_NULL_HANDLE) {
        submitted_destroy.cmd_buffers.push_back(submitted_immediate_cb);
        auto& slot_readbacks = pixel_readbacks_slots_[submitted_slot];
        slot_readbacks.insert(
            slot_readbacks.end(),
            std::make_move_iterator(pixel_readbacks_current_.begin()),
            std::make_move_iterator(pixel_readbacks_current_.end()));
        pixel_readbacks_current_.clear();
    } else if (!pixel_readbacks_current_.empty()) {
        tc_log(TC_LOG_ERROR,
               "[VulkanRenderDevice] async pixel readbacks queued without an immediate command buffer");
        destroy_pixel_readbacks(pixel_readbacks_current_);
    }
    pending_destroy_slots_[submitted_slot] = std::move(submitted_destroy);

    const uint32_t next_slot = (submitted_slot + 1) % kFrameSlotCount;
    auto t_wait0 = std::chrono::steady_clock::now();
    prepare_frame_slot(next_slot);
    auto t_wait1 = std::chrono::steady_clock::now();
    s_last_fence_wait_us +=
        std::chrono::duration<double, std::micro>(t_wait1 - t_wait0).count();

    current_pool_idx_ = next_slot;
    ring_ubo_slot_idx_ = next_slot;
    transient_vb_slot_idx_ = next_slot;

    ++s_submits;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - s_window_start).count() >= 1.0) {
        uint64_t draws    = g_draw_count.exchange(0, std::memory_order_relaxed);
        uint64_t rsets    = g_resource_set_count.exchange(0, std::memory_order_relaxed);
        uint64_t pipes    = g_pipeline_count.exchange(0, std::memory_order_relaxed);
        uint64_t pipe_hits = g_pipeline_cache_hit_count.exchange(0, std::memory_order_relaxed);
        uint64_t pipe_misses = g_pipeline_cache_miss_count.exchange(0, std::memory_order_relaxed);
        uint64_t pipe_layouts =
            g_pipeline_cache_unique_vertex_layout_count.exchange(0, std::memory_order_relaxed);
        uint64_t shaders  = g_shader_count.exchange(0, std::memory_order_relaxed);
        uint64_t bp       = g_bind_pipeline_count.exchange(0, std::memory_order_relaxed);
        uint64_t brs      = g_bind_rset_count.exchange(0, std::memory_order_relaxed);
        uint64_t bvb      = g_bind_vbo_count.exchange(0, std::memory_order_relaxed);
        uint64_t bib      = g_bind_ibo_count.exchange(0, std::memory_order_relaxed);
        uint64_t bpc      = g_push_constants_count.exchange(0, std::memory_order_relaxed);
        uint64_t rec_us   = g_record_us.exchange(0, std::memory_order_relaxed);
        uint64_t subm_us  = g_submit_us.exchange(0, std::memory_order_relaxed);
        uint64_t sh_pre_us = g_shader_preprocess_us.exchange(0, std::memory_order_relaxed);
        uint64_t sh_comp_us = g_shader_compile_us.exchange(0, std::memory_order_relaxed);
        uint64_t sh_reflect_us = g_shader_reflect_us.exchange(0, std::memory_order_relaxed);
        uint64_t sh_module_us = g_shader_module_us.exchange(0, std::memory_order_relaxed);
        uint64_t pipe_rp_us = g_pipeline_renderpass_us.exchange(0, std::memory_order_relaxed);
        uint64_t pipe_create_us = g_pipeline_create_us.exchange(0, std::memory_order_relaxed);
        if (vulkan_stats_enabled()) {
            tc_log(TC_LOG_INFO,
                   "[tgfx2-vulkan] submit stats: submits=%llu draws=%llu "
                   "pipelines=%llu pipeline_cache_hit=%llu pipeline_cache_miss=%llu "
                   "new_vertex_layouts=%llu resource_sets=%llu bind_pipeline=%llu "
                   "bind_rset=%llu bind_vbo=%llu bind_ibo=%llu push_constants=%llu "
                   "record_ms=%.3f submit_ms=%.3f fence_wait_ms=%.3f",
                   static_cast<unsigned long long>(s_submits),
                   static_cast<unsigned long long>(draws),
                   static_cast<unsigned long long>(pipes),
                   static_cast<unsigned long long>(pipe_hits),
                   static_cast<unsigned long long>(pipe_misses),
                   static_cast<unsigned long long>(pipe_layouts),
                   static_cast<unsigned long long>(rsets),
                   static_cast<unsigned long long>(bp),
                   static_cast<unsigned long long>(brs),
                   static_cast<unsigned long long>(bvb),
                   static_cast<unsigned long long>(bib),
                   static_cast<unsigned long long>(bpc),
                   static_cast<double>(rec_us) / 1000.0,
                   static_cast<double>(subm_us) / 1000.0,
                   s_last_fence_wait_us / 1000.0);
            if (shaders != 0 || pipes != 0) {
                tc_log(TC_LOG_INFO,
                       "[tgfx2-vulkan] cold stats: shaders=%llu shader_preprocess_ms=%.3f "
                       "shader_compile_ms=%.3f shader_reflect_ms=%.3f shader_module_ms=%.3f "
                       "pipeline_renderpass_ms=%.3f pipeline_create_ms=%.3f",
                       static_cast<unsigned long long>(shaders),
                       static_cast<double>(sh_pre_us) / 1000.0,
                       static_cast<double>(sh_comp_us) / 1000.0,
                       static_cast<double>(sh_reflect_us) / 1000.0,
                       static_cast<double>(sh_module_us) / 1000.0,
                       static_cast<double>(pipe_rp_us) / 1000.0,
                       static_cast<double>(pipe_create_us) / 1000.0);
            }
        }
        s_submits = 0;
        s_last_fence_wait_us = 0;
        s_window_start = now;
    }
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
    // Build key — formats + sample_count + load ops. LoadOp must be part
    // of the key: for LoadOp::Load the render pass is built with
    // initialLayout = COLOR/DEPTH_ATTACHMENT_OPTIMAL (the layout
    // begin_render_pass transitions the attachment into); for Clear/
    // DontCare we keep initialLayout = UNDEFINED. Collapsing Load and
    // Clear into one cache entry would either cause a "loadOp=LOAD with
    // initialLayout=UNDEFINED" validation error or silently clear
    // content the caller wanted preserved.
    std::vector<VkFormat> key;
    for (auto f : color_formats) key.push_back(vk::to_vk_format(f));
    if (has_depth) key.push_back(vk::to_vk_format(depth_format));
    key.push_back(static_cast<VkFormat>(sample_count));
    key.push_back(static_cast<VkFormat>(static_cast<int>(color_load) + 0x10000));
    key.push_back(static_cast<VkFormat>(static_cast<int>(depth_load) + 0x20000));

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
        att.initialLayout = (color_load == LoadOp::Load)
            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
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
        att.initialLayout = (depth_load == LoadOp::Load)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
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

    // Single EXTERNAL→0 dependency covering color/depth writes and
    // fragment reads. No self-dependency: inside the pass we never emit
    // vkCmdPipelineBarrier. Every caller is required to deliver sampled
    // textures already in SHADER_READ_ONLY_OPTIMAL (via upload_texture,
    // end_render_pass or copy_texture/blit_to_texture, all of which
    // leave that layout on exit).
    //
    // dstStageMask/dstAccessMask must only reference stages supported by
    // a graphics subpass (the VUID-00838 check). TRANSFER stays on the
    // EXTERNAL side so that transitions to SHADER_READ_ONLY done before
    // vkCmdBeginRenderPass synchronize into the pass's fragment shader
    // reads.
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                       VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_TRANSFER_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
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
    VkFramebufferCacheKey key;
    key.render_pass = render_pass;
    key.width = width;
    key.height = height;
    key.attachments = attachments;

    auto it = framebuffer_cache_.find(key);
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

    framebuffer_cache_[std::move(key)] = fb;
    return fb;
}

// --- Texture-to-texture blit (backend-neutral external-target replacement) ---

void VulkanRenderDevice::blit_to_texture(
    TextureHandle dst_handle,
    TextureHandle src_handle,
    int src_x, int src_y, int src_w, int src_h,
    int dst_x, int dst_y, int dst_w, int dst_h)
{
    auto* src = textures_.get(src_handle.id);
    auto* dst = textures_.get(dst_handle.id);
    if (!src || !dst) return;

    // Self-blit is meaningless and would emit contradictory
    // TRANSFER_SRC/DST barriers on the same image.
    if (src == dst) return;

    // Require the usage flags that the transitions below need. Without
    // them Vulkan rejects the layout transition outright. Callers are
    // expected to create host-owned scan-out textures with CopyDst and
    // renderer outputs with CopySrc.
    auto has = [](TextureUsage u, TextureUsage bit) {
        return (static_cast<uint32_t>(u) & static_cast<uint32_t>(bit)) != 0;
    };
    if (!has(src->desc.usage, TextureUsage::CopySrc)) {
        fprintf(stderr, "[Vulkan] blit_to_texture: src missing CopySrc usage — skipping\n");
        return;
    }
    if (!has(dst->desc.usage, TextureUsage::CopyDst)) {
        fprintf(stderr, "[Vulkan] blit_to_texture: dst missing CopyDst usage — skipping\n");
        return;
    }
    // single → MSAA has no single Vulkan command — semantically ambiguous
    // (how do you broadcast one sample across N?). Reject with a warn.
    if (dst->desc.sample_count > 1 && src->desc.sample_count == 1) {
        fprintf(stderr, "[Vulkan] blit_to_texture: single → MSAA — skipping "
                        "(src h=%u %ux%u fmt=%d samples=1 usage=0x%x, "
                        "dst h=%u %ux%u fmt=%d samples=%u usage=0x%x)\n",
                src_handle.id, src->desc.width, src->desc.height,
                (int)src->desc.format,
                (unsigned)src->desc.usage,
                dst_handle.id, dst->desc.width, dst->desc.height,
                (int)dst->desc.format, dst->desc.sample_count,
                (unsigned)dst->desc.usage);
        return;
    }

    VkImageLayout prev_src = src->current_layout;
    VkImageLayout prev_dst = dst->current_layout;

    bool msaa_resolve = src->desc.sample_count > 1 && dst->desc.sample_count == 1;
    bool msaa_copy = src->desc.sample_count > 1 && dst->desc.sample_count == src->desc.sample_count;

    execute_immediate([&](VkCommandBuffer cb) {
        transition_image_layout(cb, src->image,
            prev_src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
        transition_image_layout(cb, dst->image,
            prev_dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        if (msaa_resolve) {
            // Resolve copies exactly the same rect in src/dst — no
            // scaling. If the caller requested different dst dims, fall
            // back to a no-op (vkCmdResolveImage can't rescale).
            if (src_w != dst_w || src_h != dst_h) {
                fprintf(stderr, "[Vulkan] blit_to_texture: MSAA resolve "
                                "cannot rescale (%dx%d → %dx%d)\n",
                        src_w, src_h, dst_w, dst_h);
            } else {
                VkImageResolve resolve{};
                resolve.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                resolve.srcOffset = {src_x, src_y, 0};
                resolve.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                resolve.dstOffset = {dst_x, dst_y, 0};
                resolve.extent = {static_cast<uint32_t>(src_w),
                                  static_cast<uint32_t>(src_h), 1};
                vkCmdResolveImage(cb,
                    src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &resolve);
            }
        } else if (msaa_copy) {
            // Matching MSAA→MSAA. vkCmdCopyImage requires matching
            // formats + samples and no scaling. vkCmdBlitImage forbids
            // MSAA dst, so a scale+MSAA combined op isn't legal — warn
            // if sizes differ.
            if (src_w != dst_w || src_h != dst_h) {
                fprintf(stderr, "[Vulkan] blit_to_texture: MSAA→MSAA cannot "
                                "rescale (%dx%d → %dx%d)\n",
                        src_w, src_h, dst_w, dst_h);
            } else if (src->desc.format != dst->desc.format) {
                fprintf(stderr, "[Vulkan] blit_to_texture: MSAA→MSAA format "
                                "mismatch (src fmt=%d, dst fmt=%d)\n",
                        (int)src->desc.format, (int)dst->desc.format);
            } else {
                VkImageCopy region{};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.srcOffset = {src_x, src_y, 0};
                region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.dstOffset = {dst_x, dst_y, 0};
                region.extent = {static_cast<uint32_t>(src_w),
                                 static_cast<uint32_t>(src_h), 1};
                vkCmdCopyImage(cb,
                    src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &region);
            }
        } else {
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[0] = {src_x, src_y, 0};
            blit.srcOffsets[1] = {src_x + src_w, src_y + src_h, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[0] = {dst_x, dst_y, 0};
            blit.dstOffsets[1] = {dst_x + dst_w, dst_y + dst_h, 1};
            vkCmdBlitImage(cb,
                src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);
        }

        // Leave both images in SHADER_READ_ONLY_OPTIMAL so downstream
        // samplers (including bind_resource_set, which cannot transition
        // from inside a render pass) work without further fix-ups. If
        // prev_src was COLOR_ATTACHMENT_OPTIMAL the next render-pass
        // begin will transition it back — one cheap barrier.
        transition_image_layout(cb, src->image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
        transition_image_layout(cb, dst->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
        src->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dst->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    });
}

void VulkanRenderDevice::clear_texture(
    TextureHandle dst_handle,
    float r, float g, float b, float a,
    int viewport_x, int viewport_y,
    int viewport_w, int viewport_h)
{
    auto* dst = textures_.get(dst_handle.id);
    if (!dst) return;

    VkImageLayout prev = dst->current_layout;

    execute_immediate([&](VkCommandBuffer cb) {
        transition_image_layout(cb, dst->image,
            prev, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkClearColorValue clear{};
        clear.float32[0] = r;
        clear.float32[1] = g;
        clear.float32[2] = b;
        clear.float32[3] = a;

        // vkCmdClearColorImage requires a full-image range with an
        // offset-based mechanism — it does not natively support a
        // viewport subrect. For a subrect clear we'd need a render pass
        // with LoadOp::Clear + scissor. In practice callers pass the
        // full texture extent here (PullRenderingManager clears the
        // whole display before compositing viewports), so clearing the
        // whole image is correct; if a future caller needs a true rect
        // clear, route through begin_pass + scissor instead.
        (void)viewport_x; (void)viewport_y;
        (void)viewport_w; (void)viewport_h;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;

        vkCmdClearColorImage(cb, dst->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear, 1, &range);

        if (prev != VK_IMAGE_LAYOUT_UNDEFINED) {
            transition_image_layout(cb, dst->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, prev,
                VK_IMAGE_ASPECT_COLOR_BIT);
        } else {
            dst->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
    });
}


} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
