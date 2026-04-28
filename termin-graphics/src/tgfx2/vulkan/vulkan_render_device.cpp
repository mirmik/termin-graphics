#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_command_list.hpp"
#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"
#include "tgfx2/internal/shader_preprocess.hpp"

#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <vector>

extern "C" {
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_texture.h>
#include <tgfx/resources/tc_texture_registry.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
}

// Trampolines for destroy-hook C callbacks. Defined at file scope (not in
// an anonymous namespace) so their addresses are stable identifiers that
// `tc_*_registry_remove_destroy_hook` can match against.
static void vulkan_invalidate_tc_texture_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::VulkanRenderDevice*>(user)->invalidate_tc_texture_cache(pool_index);
}

static void vulkan_invalidate_tc_mesh_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::VulkanRenderDevice*>(user)->invalidate_tc_mesh_cache(pool_index);
}

namespace tgfx {
// Vulkan hot-path counters — swept once per second from submit().
static std::atomic<uint64_t> g_resource_set_count{0};
static std::atomic<uint64_t> g_pipeline_count{0};
// Defined here (non-static), incremented from vulkan_command_list.cpp's
// per-command methods. The extern decls in that file live inside
// `namespace tgfx`, so the fully-qualified symbols are `tgfx::g_*`.
std::atomic<uint64_t> g_draw_count{0};
std::atomic<uint64_t> g_bind_pipeline_count{0};
std::atomic<uint64_t> g_bind_rset_count{0};
std::atomic<uint64_t> g_bind_vbo_count{0};
std::atomic<uint64_t> g_bind_ibo_count{0};
std::atomic<uint64_t> g_push_constants_count{0};
std::atomic<uint64_t> g_record_us{0};
std::atomic<uint64_t> g_submit_us{0};
}

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

    pick_physical_device();
    create_logical_device();
    create_allocator();
    create_command_pool();
    create_descriptor_pool();
    create_shared_layouts();
    create_ring_ubo();

    // Frame fence: tracks in-flight submits. Created unsignaled — the
    // first `submit()` sees `frame_fence_in_flight_ == false` and skips
    // the wait; subsequent submits wait on it before reusing it.
    {
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device_, &fci, nullptr, &frame_fence_) != VK_SUCCESS) {
            throw std::runtime_error("VulkanRenderDevice: vkCreateFence failed");
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
    caps_.max_texture_dimension_2d = props.limits.maxImageDimension2D;
    caps_.max_color_attachments = props.limits.maxColorAttachments;
    caps_.max_texture_units = props.limits.maxBoundDescriptorSets;
    caps_.supports_compute = true;
    caps_.supports_geometry_shaders = features.geometryShader;
    caps_.supports_timestamp_queries = (props.limits.timestampComputeAndGraphics != 0);
    caps_.supports_multisample_resolve = true;

    // Subscribe to registry destroy-hooks so per-device tc_texture /
    // tc_mesh caches get invalidated before a slot is recycled. The
    // matching unregister calls live in the destructor.
    tc_texture_registry_add_destroy_hook(
        &vulkan_invalidate_tc_texture_trampoline, this);
    tc_mesh_registry_add_destroy_hook(
        &vulkan_invalidate_tc_mesh_trampoline, this);
}

VulkanRenderDevice::~VulkanRenderDevice() {
    // Unsubscribe from registry destroy-hooks before tearing anything down
    // so an incoming tc_texture_destroy / tc_mesh_destroy after this point
    // can't call into a half-destroyed device.
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

    // Drop per-device tc_texture / tc_mesh caches. The VkImage / VkBuffer
    // objects those caches point at are owned through the handle pools
    // (buffers_, textures_) and get released by the blanket per-pool loops
    // below, so we just need to clear the cache maps here — no explicit
    // destroy() calls required.
    tc_texture_cache_.clear();
    tc_mesh_cache_.clear();

    // Both deferred-destroy queues are now safe to release (GPU is idle).
    // The `in_flight` queue represents handles the previous frame freed;
    // `current` holds handles freed during the current frame that were
    // never submitted. Handle-pool lookups are still valid at this point,
    // so drain them through the normal per-type helper.
    drain_pending_destroy(pending_destroy_in_flight_);
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
    if (frame_fence_) vkDestroyFence(device_, frame_fence_, nullptr);
    if (default_sampler_) vkDestroySampler(device_, default_sampler_, nullptr);
    if (shared_pipeline_layout_) vkDestroyPipelineLayout(device_, shared_pipeline_layout_, nullptr);
    if (descriptor_set_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
    for (int i = 0; i < 2; ++i) {
        if (descriptor_pools_[i]) {
            vkDestroyDescriptorPool(device_, descriptor_pools_[i], nullptr);
        }
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
    // Shadow shaders index `sampler2DShadow u_shadow_map[N]` with a
    // runtime loop variable. In Vulkan that requires the
    // `shaderSampledImageArrayDynamicIndexing` feature — without it
    // access is undefined and shadow lookups silently return 1.0 (no
    // shadow) on most drivers. Matches GL's always-available dynamic
    // indexing of sampler arrays.
    features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;

    std::vector<const char*> extensions;
    if (surface_) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // NDC-Z convention: Vulkan-native Z ∈ [0, 1]. OpenGL reaches the
    // same convention via a one-time glClipControl(GL_UPPER_LEFT,
    // GL_ZERO_TO_ONE) in OpenGLRenderDevice, and scene/shadow
    // projection matrices (see termin-base/geom/mat44.hpp) build
    // matrices that target exactly that. No VK_EXT_depth_clip_control
    // needed.

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
    // Two pools, double-buffered with the frame fence. Each frame's draws
    // allocate from `descriptor_pools_[current_pool_idx_]`; at submit(),
    // after the previous submit's fence signals, we flip to the other
    // pool and `vkResetDescriptorPool` it in one call — no individual
    // `vkFreeDescriptorSets`, no FREE_DESCRIPTOR_SET_BIT flag (the driver
    // can use a faster bump allocator internally).
    //
    // Pool size covers a single frame's worst case for chronosquad-like
    // scenes: ~30 skinned meshes × 4 shadow cascades + color/depth/id/
    // normal + UI draws ≈ 500-800 sets. Headroom to 2048 accommodates
    // heavier future scenes without re-sizing. Each set may touch up to
    // ~5 UBOs and ~6 samplers, hence the pool-size multipliers below.
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          8 * 2048},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          512},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,           2 * 2048},
        {VK_DESCRIPTOR_TYPE_SAMPLER,                 2 * 2048},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20 * 2048},
    };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags = 0;  // No per-set free — pool reset frees everything at once.
    ci.maxSets = 2048;
    ci.poolSizeCount = 5;
    ci.pPoolSizes = pool_sizes;

    for (int i = 0; i < 2; ++i) {
        if (vkCreateDescriptorPool(device_, &ci, nullptr, &descriptor_pools_[i])
                != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }
}

// --- Shared layouts (MVP: universal layout) ---

void VulkanRenderDevice::create_shared_layouts() {
    // Universal descriptor set layout:
    //   binding 0..3  = UBO  (lighting=0, material=1, per-frame=2, shadow-block=3)
    //   binding 4..7  = COMBINED_IMAGE_SAMPLER, 1 each (material textures)
    //   binding 8     = COMBINED_IMAGE_SAMPLER, MAX_SHADOW_MAPS (shadow depth array;
    //                    `layout(binding = 8) sampler2DShadow u_shadow_map[N]`
    //                    compiles to a single array descriptor, so Vulkan
    //                    needs descriptorCount = N on binding 8)
    //   binding 9..15 = COMBINED_IMAGE_SAMPLER, 1 each (extra slots)
    //   binding 16    = UBO  (BoneBlock — SkinnedMeshRenderer bone matrices,
    //                          used by VS only, see shader_skinning.cpp)
    //
    // MAX_SHADOW_MAPS must match the GLSL macro in shadows.glsl (currently 16).
    constexpr uint32_t MAX_SHADOW_MAPS = 16;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    // UBO bindings 0..3 are DYNAMIC: per-draw offset supplied via the
    // last argument of vkCmdBindDescriptorSets, not baked into the
    // descriptor write. Lets a single ring buffer back many draws without
    // per-draw vkUpdateDescriptorSets churn (see create_ring_ubo).
    for (uint32_t i = 0; i < 4; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_ALL;
        bindings.push_back(b);
    }
    // Material samplers 4..7 (individual).
    for (uint32_t i = 4; i < 8; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }
    // Shadow-map array at binding 8 (MAX_SHADOW_MAPS descriptors).
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 8;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = MAX_SHADOW_MAPS;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }
    // Extras 9..15 (individual — debug overlays etc.).
    for (uint32_t i = 9; i < 16; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }
    // BoneBlock UBO at binding 16 (skinning variant VS, see
    // shader_skinning.cpp). Kept out of the 0..3 UBO block because the
    // skinned path is optional — materials without skinning never touch
    // this slot and Vulkan allows declared-but-unused descriptors.
    // DYNAMIC for the same reason as 0..3 — SkinnedMeshRenderer will
    // route bone matrices through the ring buffer.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 16;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings.push_back(b);
    }

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_ci.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layout_ci, nullptr, &descriptor_set_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    // Push-constant range: 128 bytes accessible from every graphics
    // stage. That is the minimum guaranteed by `maxPushConstantsSize`
    // in Vulkan 1.0 (lots of mobile GPUs stop at exactly 128). Fits
    // a mat4 + a vec4 + spare ints, which covers the UIRenderer /
    // Text2D / Text3D style where a single `layout(push_constant)`
    // block carries all per-draw state. Shaders that need more must
    // fall back to UBO bindings 0-3.
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
    pc_range.offset = 0;
    pc_range.size = 128;

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &descriptor_set_layout_;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &pc_range;

    if (vkCreatePipelineLayout(device_, &pl_ci, nullptr, &shared_pipeline_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
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

    // 16 MB total — 8 MB per slot. Budget ~10 KB UBO data per draw × 1600
    // worst-case draws/frame = 16 MB needed, so per-slot headroom is tight
    // on adversarial scenes; logged (rare) overflow falls back to offset 0.
    // If that ever fires in practice, grow the ring or shrink per-pass UBOs.
    ring_ubo_size_ = 16 * 1024 * 1024;
    ring_ubo_slot_size_ = ring_ubo_size_ / 2;

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
    ring_ubo_heads_[0].store(0, std::memory_order_relaxed);
    ring_ubo_heads_[1].store(0, std::memory_order_relaxed);
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

// --- Capabilities ---

BackendCapabilities VulkanRenderDevice::capabilities() const { return caps_; }
void VulkanRenderDevice::wait_idle() { vkDeviceWaitIdle(device_); }

// --- Immediate command execution ---

VkCommandBuffer VulkanRenderDevice::ensure_immediate_cb() {
    if (immediate_cb_open_) return immediate_cb_;

    // Always allocate a fresh cb. The previous frame's immediate_cb was
    // handed over to pending_destroy_current_ in submit(), so it will
    // be freed after the frame fence signals — safe. Re-recording the
    // same cb here would hit "buffer is in use" validation because
    // submit()'s fence wait hasn't necessarily run yet between calls.
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &ai, &immediate_cb_);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(immediate_cb_, &bi);

    immediate_cb_open_ = true;
    return immediate_cb_;
}

void VulkanRenderDevice::execute_immediate(std::function<void(VkCommandBuffer)> fn) {
    // Record into the shared immediate cb. No submit, no wait — the cb
    // gets submitted together with the main draw cb in `submit()`, as
    // entry 0 of a multi-cb `vkQueueSubmit` so the copies/transitions
    // complete before the draws that depend on them.
    //
    // Callers that used to do `staging; execute_immediate; destroy
    // staging;` must now push staging into `defer_vma_buffer_destroy` —
    // the GPU hasn't run the copy by the time this function returns.
    fn(ensure_immediate_cb());
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
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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

    // Get or create render pass from formats. `needs_depth` is driven
    // by the attachment presence (depth_format != Undefined), NOT by the
    // depth_test/depth_write flags — a pipeline that has depth_test off
    // still needs a render pass compatible with passes that carry a
    // depth attachment. Conversely a pass with no depth attachment must
    // produce a pipeline whose cached RP also has no depth slot, or
    // vkCmdDraw fails with "RenderPasses incompatible".
    // Pass the raw color_formats through — depth-only passes (ShadowPass,
    // DepthPass) carry an empty list, and the pipeline's cached render
    // pass must also have zero color attachments to stay compatible with
    // the framebuffer begin_render_pass binds. Previous behaviour of
    // force-pushing RGBA8_UNorm produced a 1-color RP that mismatched a
    // 0-color framebuffer → vkCreateFramebuffer attachmentCount error.
    std::vector<PixelFormat> color_fmts = desc.color_formats;
    // Drop any `Undefined` entries — caller may have zero-initialised
    // slots we should not attach.
    color_fmts.erase(
        std::remove(color_fmts.begin(), color_fmts.end(),
                    PixelFormat::Undefined),
        color_fmts.end());
    bool needs_depth = desc.depth_format != PixelFormat::Undefined;
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

    // Dynamic viewport/scissor. No VkPipelineViewportDepthClipControl —
    // we target Vulkan-native NDC Z ∈ [0,1] everywhere; OpenGL reaches
    // the same convention via glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE).
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

    // Blend — one attachment per color output. Must match subpass's
    // colorAttachmentCount, else VUID-VkGraphicsPipelineCreateInfo-
    // renderPass-06041 fires and some drivers silently reject the draw
    // (shadow/depth-only passes were hit by this — 0 color attachments
    // in the subpass but attachmentCount=1 on the pipeline).
    std::vector<VkPipelineColorBlendAttachmentState> blend_atts(color_fmts.size());
    for (auto& ba : blend_atts) {
        ba.blendEnable = desc.blend.enabled ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = vk::to_vk_blend_factor(desc.blend.src_color);
        ba.dstColorBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_color);
        ba.colorBlendOp = vk::to_vk_blend_op(desc.blend.color_op);
        ba.srcAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.src_alpha);
        ba.dstAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_alpha);
        ba.alphaBlendOp = vk::to_vk_blend_op(desc.blend.alpha_op);
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<uint32_t>(blend_atts.size());
    blend.pAttachments = blend_atts.empty() ? nullptr : blend_atts.data();

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
    g_pipeline_count.fetch_add(1, std::memory_order_relaxed);

    return {pipelines_.add(std::move(res))};
}

// --- Resource set ---

static uint64_t hash_resource_set_desc(const ResourceSetDesc& desc) {
    // FNV-1a over the stable-key fields of each binding. Handles live in
    // HandlePool slots and are stable within a frame, so we can hash the
    // raw ids without worrying about pointer churn.
    //
    // UBO offsets are INTENTIONALLY excluded from the hash: UBOs on the
    // DYNAMIC slots supply their per-draw offset through the
    // dynamic_offsets[] argument at bind time, not through the descriptor
    // write, so two draws that differ only in ring-offset must reuse the
    // same descriptor set instead of paying another vkAllocateDescriptorSets.
    // Range still participates (different range → different buffer_info).
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    for (const auto& b : desc.bindings) {
        mix(b.binding);
        mix(static_cast<uint64_t>(b.kind));
        mix(b.buffer.id);
        if (b.kind != ResourceBinding::Kind::UniformBuffer) {
            mix(b.offset);
        }
        mix(b.range);
        mix(b.texture.id);
        mix(b.sampler.id);
    }
    return h;
}

ResourceSetHandle VulkanRenderDevice::create_resource_set(const ResourceSetDesc& desc) {
    // Per-frame cache: multiple draws with the same bindings (typical —
    // many meshes share a material + PerFrame UBO + shadow array) reuse
    // one VkDescriptorSet instead of paying for another
    // vkAllocateDescriptorSets + vkUpdateDescriptorSets. Cache is
    // cleared when the pool is reset in submit(), so every entry is
    // always backed by a live set in descriptor_pools_[current_pool_idx_].
    const uint64_t key = hash_resource_set_desc(desc);
    auto& cache = descriptor_cache_[current_pool_idx_];
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    g_resource_set_count.fetch_add(1, std::memory_order_relaxed);
    VkResourceSetResource res;
    res.desc = desc;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptor_pools_[current_pool_idx_];
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

    // Shadow-map array spans slots 8..8+MAX_SHADOW_MAPS-1 (matches
    // SHADOW_SLOT_BASE in ColorPass and the `u_shadow_map[N]` array in
    // shadows.glsl). The shared descriptor set layout folds those slots
    // into a single binding=8 with descriptorCount=N, so sampler writes
    // in that range must be re-targeted to binding=8,
    // dstArrayElement=slot-8.
    //
    // IMPORTANT: the re-target only applies to SampledTexture bindings —
    // UBOs on the same numeric slot live at their own binding in the
    // layout (e.g. BoneBlock at binding 16, inside the shadow-array
    // numeric range). Without this check the UBO write gets retargeted
    // to binding=8 and Vulkan complains about type mismatch
    // (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER vs COMBINED_IMAGE_SAMPLER).
    constexpr uint32_t SHADOW_SLOT_BASE  = 8;
    constexpr uint32_t MAX_SHADOW_MAPS_W = 16;

    // Dynamic-UBO bindings declared by the shared layout, in the exact
    // order vkCmdBindDescriptorSets consumes the dynamic_offsets[] array
    // (sorted ascending by binding, per Vulkan spec).
    // Keep in sync with create_shared_layouts().
    constexpr uint32_t DYNAMIC_UBO_BINDINGS[VkResourceSetResource::DYNAMIC_UBO_COUNT] =
        {0, 1, 2, 3, 16};
    auto dynamic_idx_for_binding = [&](uint32_t binding) -> int {
        for (uint32_t i = 0; i < VkResourceSetResource::DYNAMIC_UBO_COUNT; ++i)
            if (DYNAMIC_UBO_BINDINGS[i] == binding) return static_cast<int>(i);
        return -1;
    };

    for (const auto& b : desc.bindings) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = res.descriptor_set;
        w.dstBinding = b.binding;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        const bool is_sampled = (b.kind == ResourceBinding::Kind::SampledTexture);
        if (is_sampled &&
            b.binding >= SHADOW_SLOT_BASE &&
            b.binding <  SHADOW_SLOT_BASE + MAX_SHADOW_MAPS_W) {
            w.dstBinding = SHADOW_SLOT_BASE;
            w.dstArrayElement = b.binding - SHADOW_SLOT_BASE;
        }

        switch (b.kind) {
            case ResourceBinding::Kind::UniformBuffer:
            case ResourceBinding::Kind::StorageBuffer: {
                auto* buf = get_buffer(b.buffer);
                if (!buf) continue;
                // UBOs on the five DYNAMIC slots route their offset through
                // the dynamic_offsets[] argument at bind time — the write
                // itself uses offset=0 with an explicit range. Two hard
                // Vulkan rules apply to this binding type:
                //   1. range must be <= maxUniformBufferRange (64 KB min).
                //      Setting VK_WHOLE_SIZE on a 16 MB ring buffer
                //      trips VUID-VkWriteDescriptorSet-descriptorType-00332.
                //   2. With VK_WHOLE_SIZE the dynamic offset must be 0
                //      (VUID-vkCmdBindDescriptorSets-pDescriptorSets-06715) —
                //      defeating the whole point of dynamic bindings.
                // So we *always* bake an explicit range. Callers supply
                // the block size through ResourceBinding::range; for the
                // legacy bind_uniform_buffer(handle) path where range==0
                // we fall back to min(buffer.size, 64 KB) — the per-UBO
                // buffers are small (a few KB), so this lands at the real
                // size and not a truncated view.
                const int dyn_idx = (b.kind == ResourceBinding::Kind::UniformBuffer)
                    ? dynamic_idx_for_binding(b.binding) : -1;
                const bool is_dynamic_ubo = (dyn_idx >= 0);
                if (is_dynamic_ubo) {
                    constexpr uint64_t VK_MIN_MAX_UBO_RANGE = 65536;
                    uint64_t range = b.range;
                    if (range == 0) {
                        range = std::min<uint64_t>(buf->desc.size, VK_MIN_MAX_UBO_RANGE);
                    }
                    if (range > VK_MIN_MAX_UBO_RANGE) range = VK_MIN_MAX_UBO_RANGE;
                    buf_infos.push_back({buf->buffer, 0, range});
                    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    res.dynamic_offsets[dyn_idx] = static_cast<uint32_t>(b.offset);
                } else {
                    buf_infos.push_back({buf->buffer, b.offset, b.range ? b.range : VK_WHOLE_SIZE});
                    w.descriptorType = (b.kind == ResourceBinding::Kind::UniformBuffer)
                        ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
                w.pBufferInfo = &buf_infos.back();
                break;
            }
            case ResourceBinding::Kind::SampledTexture: {
                // GLSL `sampler2D` compiles to a COMBINED_IMAGE_SAMPLER
                // descriptor in SPIR-V; layout bindings 4-7 are of that
                // type. Pack the view + sampler into a single image info.
                // If caller didn't supply a sampler, fall back to a
                // cached default linear-clamp one so the slot is never
                // descriptor-wise empty.
                auto* tex = get_texture(b.texture);
                if (!tex) continue;
                VkSampler samp_vk = VK_NULL_HANDLE;
                if (b.sampler) {
                    auto* samp = get_sampler(b.sampler);
                    if (samp) samp_vk = samp->sampler;
                }
                if (samp_vk == VK_NULL_HANDLE) {
                    samp_vk = ensure_default_sampler();
                }
                img_infos.push_back({samp_vk, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &img_infos.back();
                break;
            }
            case ResourceBinding::Kind::Sampler: {
                // Kept for forward compatibility with shaders using the
                // separate texture2D + sampler split. No layout slots
                // for it in the current descriptor set layout; the
                // write is effectively a no-op on the shared layout
                // but other layouts may use it.
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

    ResourceSetHandle handle{resource_sets_.add(std::move(res))};
    cache[key] = handle;
    return handle;
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
    pending_destroy_current_.buffers.push_back(h);
}

void VulkanRenderDevice::destroy(TextureHandle h) {
    if (h.id != 0) pending_destroy_current_.textures.push_back(h);
}

void VulkanRenderDevice::destroy(SamplerHandle h) {
    if (h.id != 0) pending_destroy_current_.samplers.push_back(h);
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
            if (r->image) vmaDestroyImage(allocator_, r->image, r->allocation);
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
    for (auto cb : q.cmd_buffers) {
        vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
    }
    for (auto& [buf, alloc] : q.vma_buffers) {
        vmaDestroyBuffer(allocator_, buf, alloc);
    }
    q = {};
}

// --- Upload ---

void VulkanRenderDevice::upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset) {
    auto* res = buffers_.get(dst.id);
    if (!res) return;

    if (res->mapped_ptr) {
        // Persistently-mapped host-visible buffer. The common path for
        // every per-frame UBO (PerFrame / ShadowBlock / BoneBlock /
        // material params). One memcpy — no map/unmap, no submit, no
        // stall. vmaFlushAllocation covers non-coherent memory types
        // (NVIDIA/AMD desktop Linux is coherent in practice, but flush
        // is a no-op when the allocation is coherent, so always call).
        std::memcpy(static_cast<uint8_t*>(res->mapped_ptr) + offset,
                    data.data(), data.size());
        vmaFlushAllocation(allocator_, res->allocation, offset, data.size());
    } else if (res->desc.cpu_visible) {
        // Fallback: host-visible but not persistently mapped (e.g. buffer
        // registered externally). Keep the old explicit map path.
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

        // Defer staging destroy — GPU runs the copy after the frame submit.
        defer_vma_buffer_destroy(staging, staging_alloc);
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
    defer_vma_buffer_destroy(staging, staging_alloc);
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
        // Blocking readback — caller needs CPU-side bytes right after
        // this returns. Deliberately NOT routed through the shared
        // immediate_cb_ path, because that one is batched and drained
        // asynchronously by submit(). A readback call is rare (screen-
        // shot / GPU debug) so the extra submit+wait here is fine.
        VkBufferCreateInfo stage_ci{};
        stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stage_ci.size = data.size();
        stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo stage_alloc{};
        stage_alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

        VkBuffer staging;
        VmaAllocation staging_alloc_h;
        vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc, &staging, &staging_alloc_h, nullptr);

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = command_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(device_, &ai, &cb);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy region{};
        region.srcOffset = offset;
        region.size = data.size();
        vkCmdCopyBuffer(cb, res->buffer, staging, 1, &region);
        vkEndCommandBuffer(cb);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue_);

        void* mapped;
        vmaMapMemory(allocator_, staging_alloc_h, &mapped);
        std::memcpy(data.data(), mapped, data.size());
        vmaUnmapMemory(allocator_, staging_alloc_h);
        vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
        vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
    }
}

bool VulkanRenderDevice::read_pixel_rgba8(
    TextureHandle tex, int x, int y, float out_rgba[4]
) {
    auto* res = textures_.get(tex.id);
    if (!res || !out_rgba) return false;

    // Clamp to texture bounds — caller's (x, y) in pixel coords from the
    // editor picking path is not guaranteed to land inside the attachment.
    const int w = static_cast<int>(res->desc.width);
    const int h = static_cast<int>(res->desc.height);
    if (x < 0 || y < 0 || x >= w || y >= h) return false;

    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = 4;
    stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo stage_alloc{};
    stage_alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc_h = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc,
                        &staging, &staging_alloc_h, nullptr) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &ai, &cb);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    // Move the image from whatever layout it is in now into TRANSFER_SRC
    // for the copy; the end_render_pass hook already transitions color
    // attachments to SHADER_READ_ONLY_OPTIMAL, so that is the common
    // source layout, but we don't assume — transition_image_layout
    // handles the generic case.
    VkImageLayout prev_layout = res->current_layout;
    transition_image_layout(cb, res->image, prev_layout,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(cb, res->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    // Put the image back where we found it so the next frame's
    // sampling / render-pass-load doesn't start from an unexpected layout.
    transition_image_layout(cb, res->image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            prev_layout, VK_IMAGE_ASPECT_COLOR_BIT);
    res->current_layout = prev_layout;

    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    void* mapped = nullptr;
    uint8_t pixel[4] = {0, 0, 0, 0};
    if (vmaMapMemory(allocator_, staging_alloc_h, &mapped) == VK_SUCCESS && mapped) {
        std::memcpy(pixel, mapped, 4);
        vmaUnmapMemory(allocator_, staging_alloc_h);
    }

    vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cb);

    out_rgba[0] = pixel[0] / 255.0f;
    out_rgba[1] = pixel[1] / 255.0f;
    out_rgba[2] = pixel[2] / 255.0f;
    out_rgba[3] = pixel[3] / 255.0f;
    return true;
}

// --- Command list ---

std::unique_ptr<ICommandList> VulkanRenderDevice::create_command_list(QueueType /*queue*/) {
    return std::make_unique<VulkanCommandList>(*this);
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

    auto t_wait0 = std::chrono::steady_clock::now();

    // Before kicking off a new submit, retire the previous one. If the
    // fence is in-flight we wait (usually signaled already by the time we
    // get here — GPU rendering a shadow cascade takes a few ms at most),
    // then drain the destroy queue that was waiting on it. These
    // resources are now GPU-safe to free.
    if (frame_fence_in_flight_) {
        vkWaitForFences(device_, 1, &frame_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &frame_fence_);
        drain_pending_destroy(pending_destroy_in_flight_);
        frame_fence_in_flight_ = false;
    }
    auto t_wait1 = std::chrono::steady_clock::now();
    s_last_fence_wait_us +=
        std::chrono::duration<double, std::micro>(t_wait1 - t_wait0).count();

    // Destroys queued during *this* submit's recording graduate to
    // in-flight status and will be freed after the fence signals.
    pending_destroy_in_flight_ = std::move(pending_destroy_current_);
    pending_destroy_current_ = {};

    // Flip descriptor pools. The pool we're about to switch *into* has
    // just been retired (its fence — the one we waited on above — was
    // from the frame that used it), so it is GPU-safe to reset. Reset
    // as a single call returns every descriptor set in that pool to the
    // free list; much cheaper than per-set vkFreeDescriptorSets and
    // completely removes the "pool fills during a long frame" failure.
    current_pool_idx_ = 1 - current_pool_idx_;
    vkResetDescriptorPool(device_, descriptor_pools_[current_pool_idx_], 0);
    // Cache entries point into the pool we just reset — their VkDescriptorSets
    // are gone. Also retire the HandlePool entries they aliased so freed
    // ids can be reused in the new frame.
    for (const auto& [_, h] : descriptor_cache_[current_pool_idx_]) {
        resource_sets_.remove(h.id);
    }
    descriptor_cache_[current_pool_idx_].clear();

    // Flip the ring-UBO slot. Same double-buffered logic as the descriptor
    // pool above: the slot we switch INTO was last written two frames ago
    // (its fence long since signaled). Resetting its head lets the upcoming
    // frame start writing at offset 0 of that slot while the just-submitted
    // frame's GPU work keeps reading from the other slot.
    ring_ubo_slot_idx_ = 1 - ring_ubo_slot_idx_;
    ring_ubo_heads_[ring_ubo_slot_idx_].store(0, std::memory_order_relaxed);

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
    vkQueueSubmit(graphics_queue_, 1, &si, frame_fence_);
    auto t_submit1 = std::chrono::steady_clock::now();
    g_submit_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_submit1 - t_submit0).count(),
        std::memory_order_relaxed);
    frame_fence_in_flight_ = true;

    // Defer-free the immediate cb we just submitted — it's in-flight on
    // the graphics queue, so vkFreeCommandBuffers can only run after the
    // frame fence signals. The drain at the top of the NEXT submit()
    // picks this up.
    if (submitted_immediate_cb != VK_NULL_HANDLE) {
        pending_destroy_current_.cmd_buffers.push_back(submitted_immediate_cb);
    }

    ++s_submits;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - s_window_start).count() >= 1.0) {
        uint64_t draws    = g_draw_count.exchange(0, std::memory_order_relaxed);
        uint64_t rsets    = g_resource_set_count.exchange(0, std::memory_order_relaxed);
        uint64_t pipes    = g_pipeline_count.exchange(0, std::memory_order_relaxed);
        uint64_t bp       = g_bind_pipeline_count.exchange(0, std::memory_order_relaxed);
        uint64_t brs      = g_bind_rset_count.exchange(0, std::memory_order_relaxed);
        uint64_t bvb      = g_bind_vbo_count.exchange(0, std::memory_order_relaxed);
        uint64_t bib      = g_bind_ibo_count.exchange(0, std::memory_order_relaxed);
        uint64_t bpc      = g_push_constants_count.exchange(0, std::memory_order_relaxed);
        uint64_t rec_us   = g_record_us.exchange(0, std::memory_order_relaxed);
        uint64_t subm_us  = g_submit_us.exchange(0, std::memory_order_relaxed);
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

// --- tc_texture / tc_mesh per-device caches -----------------------------
//
// These replace the former file-scope g_tex_cache / g_mesh_cache singletons
// in tgfx2_bridge.cpp. Bridge functions on the Vulkan path now delegate
// directly to these methods; GL continues to go through tc_gpu_slot.

namespace {

PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return PixelFormat::RGBA16F;
        case TC_TEXTURE_DEPTH24: return PixelFormat::D24_UNorm_S8_UInt;
        case TC_TEXTURE_DEPTH32F: return PixelFormat::D32F;
    }
    return PixelFormat::RGBA8_UNorm;
}

// Normalise tc_texture pixel data so Vulkan can upload it. RGB8 isn't a
// universal VkFormat, so expand it to RGBA8 (alpha = 255) here. Everything
// else is passed through.
std::vector<uint8_t> normalize_pixels(const tc_texture* tex, PixelFormat& out_fmt) {
    const auto fmt = static_cast<tc_texture_format>(tex->format);
    const size_t src_bytes = tc_texture_data_size(tex);
    const auto* src = static_cast<const uint8_t*>(tex->data);

    std::vector<uint8_t> pixels;
    if (fmt == TC_TEXTURE_RGB8) {
        out_fmt = PixelFormat::RGBA8_UNorm;
        pixels.resize(size_t(tex->width) * tex->height * 4);
        for (size_t i = 0, j = 0; i < src_bytes; i += 3, j += 4) {
            pixels[j + 0] = src[i + 0];
            pixels[j + 1] = src[i + 1];
            pixels[j + 2] = src[i + 2];
            pixels[j + 3] = 0xFF;
        }
        return pixels;
    }

    out_fmt = tc_format_to_tgfx2(fmt);
    pixels.assign(src, src + src_bytes);
    return pixels;
}

// Translate tc_texture_usage_flags bitset → tgfx::TextureUsage. Mirrors
// the helper in tgfx2_gpu_ops.cpp; we duplicate it here because the two
// translation units don't share a common implementation file.
TextureUsage tc_usage_to_tgfx(uint32_t usage) {
    uint32_t out = 0;
    if (usage & TC_TEXTURE_USAGE_SAMPLED)
        out |= static_cast<uint32_t>(TextureUsage::Sampled);
    if (usage & TC_TEXTURE_USAGE_COLOR_ATTACHMENT)
        out |= static_cast<uint32_t>(TextureUsage::ColorAttachment);
    if (usage & TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)
        out |= static_cast<uint32_t>(TextureUsage::DepthStencilAttachment);
    if (usage & TC_TEXTURE_USAGE_COPY_SRC)
        out |= static_cast<uint32_t>(TextureUsage::CopySrc);
    if (usage & TC_TEXTURE_USAGE_COPY_DST)
        out |= static_cast<uint32_t>(TextureUsage::CopyDst);
    return static_cast<TextureUsage>(out);
}

} // anonymous namespace

TextureHandle VulkanRenderDevice::ensure_tc_texture(tc_texture* tex) {
    if (!tex) return {};

    const bool gpu_only = (tex->storage_kind == TC_TEXTURE_STORAGE_GPU_ONLY);

    if (tex->width == 0 || tex->height == 0) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    if (!gpu_only && !tex->data) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: tc_texture '%s' has no CPU pixels",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    const uint32_t pool_index = tex->header.pool_index;
    const uint32_t version = tex->header.version;

    std::lock_guard<std::mutex> lock(tc_texture_cache_mtx_);
    auto it = tc_texture_cache_.find(pool_index);
    if (it != tc_texture_cache_.end() && it->second.version == version) {
        return it->second.handle;
    }

    // Stale entry — destroy old handle before replacing it.
    if (it != tc_texture_cache_.end()) {
        if (it->second.handle) destroy(it->second.handle);
        tc_texture_cache_.erase(it);
    }

    TextureDesc desc;
    desc.width = tex->width;
    desc.height = tex->height;
    desc.mip_levels = 1;
    desc.sample_count = 1;

    if (gpu_only) {
        // Render-target-style: no CPU upload, just allocate a blank
        // VkImage with whatever attachment bits the caller declared.
        // CopyDst is always added because the staging upload path uses
        // it and so do `blit_to_texture` / `clear_texture`.
        desc.format = tc_format_to_tgfx2(static_cast<tc_texture_format>(tex->format));
        desc.usage = tc_usage_to_tgfx(tex->usage) | TextureUsage::CopyDst;

        TextureHandle handle = create_texture(desc);
        if (!handle) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice::ensure_tc_texture: GPU-only create_texture failed for '%s'",
                   tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }

        CachedTcTextureEntry entry;
        entry.handle = handle;
        entry.version = version;
        tc_texture_cache_.emplace(pool_index, entry);
        return handle;
    }

    PixelFormat fmt = PixelFormat::RGBA8_UNorm;
    std::vector<uint8_t> pixels = normalize_pixels(tex, fmt);
    if (pixels.empty()) return {};

    desc.format = fmt;
    desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;

    TextureHandle handle = create_texture(desc);
    if (!handle) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: create_texture failed for '%s'",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()));

    CachedTcTextureEntry entry;
    entry.handle = handle;
    entry.version = version;
    tc_texture_cache_.emplace(pool_index, entry);
    return handle;
}

void VulkanRenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_texture_cache_mtx_);
    auto it = tc_texture_cache_.find(pool_index);
    if (it == tc_texture_cache_.end()) return;
    if (it->second.handle) destroy(it->second.handle);
    tc_texture_cache_.erase(it);
}

std::pair<BufferHandle, BufferHandle> VulkanRenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
    if (!mesh) return {};

    if (!mesh->vertices || mesh->vertex_count == 0 ||
        !mesh->indices  || mesh->index_count == 0)
    {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_mesh: tc_mesh '%s' has no CPU data",
               mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return {};
    }

    const uint32_t pool_index = mesh->header.pool_index;
    const uint32_t version = mesh->header.version;

    std::lock_guard<std::mutex> lock(tc_mesh_cache_mtx_);
    auto it = tc_mesh_cache_.find(pool_index);
    if (it != tc_mesh_cache_.end() && it->second.version == version) {
        return {it->second.vbo, it->second.ebo};
    }

    if (it != tc_mesh_cache_.end()) {
        if (it->second.vbo) destroy(it->second.vbo);
        if (it->second.ebo) destroy(it->second.ebo);
        tc_mesh_cache_.erase(it);
    }

    const size_t vb_size = mesh->vertex_count *
                           static_cast<size_t>(mesh->layout.stride);
    const size_t ib_size = mesh->index_count * sizeof(uint32_t);

    BufferDesc vb_desc;
    vb_desc.size  = vb_size;
    vb_desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    BufferHandle vbo = create_buffer(vb_desc);
    upload_buffer(
        vbo,
        std::span<const uint8_t>(
            static_cast<const uint8_t*>(mesh->vertices), vb_size));

    BufferDesc ib_desc;
    ib_desc.size  = ib_size;
    ib_desc.usage = BufferUsage::Index | BufferUsage::CopyDst;
    BufferHandle ebo = create_buffer(ib_desc);
    upload_buffer(
        ebo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(mesh->indices), ib_size));

    CachedTcMeshEntry entry;
    entry.vbo = vbo;
    entry.ebo = ebo;
    entry.version = version;
    tc_mesh_cache_.emplace(pool_index, entry);
    return {vbo, ebo};
}

void VulkanRenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_mesh_cache_mtx_);
    auto it = tc_mesh_cache_.find(pool_index);
    if (it == tc_mesh_cache_.end()) return;
    if (it->second.vbo) destroy(it->second.vbo);
    if (it->second.ebo) destroy(it->second.ebo);
    tc_mesh_cache_.erase(it);
}

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
