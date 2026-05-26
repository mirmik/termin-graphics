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
#include "tgfx2/pixel_format_utils.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <atomic>
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
// Vulkan hot-path counters — swept once per second from submit().
static std::atomic<uint64_t> g_resource_set_count{0};
static std::atomic<uint64_t> g_pipeline_count{0};
static std::atomic<uint64_t> g_shader_count{0};
static std::atomic<uint64_t> g_shader_preprocess_us{0};
static std::atomic<uint64_t> g_shader_compile_us{0};
static std::atomic<uint64_t> g_shader_reflect_us{0};
static std::atomic<uint64_t> g_shader_module_us{0};
static std::atomic<uint64_t> g_pipeline_renderpass_us{0};
static std::atomic<uint64_t> g_pipeline_create_us{0};
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

struct SpirvVertexInputs {
    bool known = false;
    std::vector<uint32_t> locations;
};

static std::string spirv_read_string(const uint32_t* words, uint32_t word_count, uint32_t start_word) {
    std::string out;
    for (uint32_t i = start_word; i < word_count; ++i) {
        uint32_t word = words[i];
        for (uint32_t b = 0; b < 4; ++b) {
            char ch = static_cast<char>((word >> (8u * b)) & 0xffu);
            if (ch == '\0') return out;
            out.push_back(ch);
        }
    }
    return out;
}

static uint32_t spirv_string_word_count(const std::string& text) {
    return static_cast<uint32_t>((text.size() + 4u) / 4u);
}

static SpirvVertexInputs reflect_spirv_vertex_inputs(
    const std::vector<uint32_t>& spirv,
    const std::string& entry_point
) {
    SpirvVertexInputs result;
    if (spirv.size() < 5) return result;

    static constexpr uint32_t OP_ENTRY_POINT = 15;
    static constexpr uint32_t OP_DECORATE = 71;
    static constexpr uint32_t OP_VARIABLE = 59;
    static constexpr uint32_t EXECUTION_MODEL_VERTEX = 0;
    static constexpr uint32_t STORAGE_CLASS_INPUT = 1;
    static constexpr uint32_t DECORATION_LOCATION = 30;

    std::unordered_set<uint32_t> vertex_entry_interfaces;
    std::unordered_map<uint32_t, uint32_t> storage_class_by_id;
    std::unordered_map<uint32_t, uint32_t> location_by_id;

    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) return result;

        const uint32_t* words = spirv.data() + offset;

        if (opcode == OP_ENTRY_POINT && word_count >= 3 && words[1] == EXECUTION_MODEL_VERTEX) {
            std::string name = spirv_read_string(words, word_count, 3);
            if (name == entry_point) {
                uint32_t first_interface = 3u + spirv_string_word_count(name);
                for (uint32_t i = first_interface; i < word_count; ++i) {
                    vertex_entry_interfaces.insert(words[i]);
                }
                result.known = true;
            }
        } else if (opcode == OP_DECORATE && word_count >= 4 && words[2] == DECORATION_LOCATION) {
            location_by_id[words[1]] = words[3];
        } else if (opcode == OP_VARIABLE && word_count >= 4) {
            storage_class_by_id[words[2]] = words[3];
        }

        offset += word_count;
    }

    if (!result.known) return result;

    std::set<uint32_t> sorted_locations;
    for (uint32_t id : vertex_entry_interfaces) {
        auto storage_it = storage_class_by_id.find(id);
        if (storage_it == storage_class_by_id.end() || storage_it->second != STORAGE_CLASS_INPUT) {
            continue;
        }
        auto location_it = location_by_id.find(id);
        if (location_it != location_by_id.end()) {
            sorted_locations.insert(location_it->second);
        }
    }

    result.locations.assign(sorted_locations.begin(), sorted_locations.end());
    return result;
}

static bool vertex_shader_uses_location(const VkShaderResource* shader, uint32_t location) {
    if (!shader || !shader->vertex_input_locations_known) return true;
    return std::find(shader->vertex_input_locations.begin(),
                     shader->vertex_input_locations.end(),
                     location) != shader->vertex_input_locations.end();
}

static bool vertex_attributes_have_location(
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    uint32_t location
) {
    return std::any_of(attributes.begin(), attributes.end(),
        [location](const VkVertexInputAttributeDescription& attr) {
            return attr.location == location;
        });
}

static std::string join_u32s(const std::vector<uint32_t>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i];
    }
    return out.str();
}

static std::string describe_vk_vertex_attributes(
    const std::vector<VkVertexInputAttributeDescription>& attributes
) {
    std::ostringstream out;
    for (size_t i = 0; i < attributes.size(); ++i) {
        const auto& attr = attributes[i];
        if (i) out << "; ";
        out << "loc=" << attr.location
            << " binding=" << attr.binding
            << " offset=" << attr.offset
            << " vkfmt=" << attr.format;
    }
    return out.str();
}

static std::string describe_vertex_layouts(
    const std::vector<VertexBufferLayout>& layouts
) {
    std::ostringstream out;
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (i) out << " | ";
        out << "binding " << i
            << " stride=" << layout.stride
            << " rate=" << (layout.per_instance ? "instance" : "vertex")
            << " attrs=[";
        for (size_t j = 0; j < layout.attributes.size(); ++j) {
            const auto& attr = layout.attributes[j];
            if (j) out << "; ";
            out << "loc=" << attr.location
                << " offset=" << attr.offset
                << " fmt=" << static_cast<int>(attr.format);
        }
        out << "]";
    }
    return out.str();
}

static uint32_t pixel_format_byte_size(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::R8_UNorm:          return 1;
        case PixelFormat::RG8_UNorm:         return 2;
        case PixelFormat::RGB8_UNorm:        return 3;
        case PixelFormat::RGBA8_UNorm:       return 4;
        case PixelFormat::BGRA8_UNorm:       return 4;
        case PixelFormat::R16F:              return 2;
        case PixelFormat::RG16F:             return 4;
        case PixelFormat::RGBA16F:           return 8;
        case PixelFormat::R32F:              return 4;
        case PixelFormat::RG32F:             return 8;
        case PixelFormat::RGBA32F:           return 16;
        case PixelFormat::D24_UNorm:         return 4;
        case PixelFormat::D24_UNorm_S8_UInt: return 4;
        case PixelFormat::D32F:              return 4;
        case PixelFormat::Undefined:         return 0;
    }
    return 0;
}

static VkImageLayout texture_post_upload_layout(const TextureDesc& desc) {
    return has_flag(desc.usage, TextureUsage::Sampled)
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

static float half_to_float(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16u;
    uint32_t exp = (h >> 10u) & 0x1fu;
    uint32_t mant = h & 0x03ffu;

    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1u;
                --exp;
            }
            mant &= 0x03ffu;
            bits = sign | ((exp + 112u) << 23u) | (mant << 13u);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | (mant << 13u);
    } else {
        bits = sign | ((exp + 112u) << 23u) | (mant << 13u);
    }

    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
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
    create_shared_layouts();
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
    if (shared_pipeline_layout_) vkDestroyPipelineLayout(device_, shared_pipeline_layout_, nullptr);
    if (descriptor_set_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
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

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
    ci.pQueueCreateInfos = queue_cis.data();
    ci.pEnabledFeatures = &features;
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

// --- Shared layouts (MVP: universal layout) ---

void VulkanRenderDevice::create_shared_layouts() {
    // Universal descriptor set layout:
    //   binding 0..3  = UBO  (lighting=0, material=1, per-frame=2, shadow-block=3)
    //   binding 4..7  = COMBINED_IMAGE_SAMPLER, 1 each (material textures)
    //   binding 8     = COMBINED_IMAGE_SAMPLER, MAX_SHADOW_MAPS (shadow depth array;
    //                    `layout(binding = 8) sampler2DShadow u_shadow_map[N]`
    //                    compiles to a single array descriptor, so Vulkan
    //                    needs descriptorCount = N on binding 8)
    //   binding 9..15 = COMBINED_IMAGE_SAMPLER, 1 each (more material textures)
    //   binding 16    = UBO  (BoneBlock — SkinnedMeshRenderer bone matrices,
    //                          used by VS only, see shader_skinning.cpp)
    //   binding 17..23 = COMBINED_IMAGE_SAMPLER, 1 each (extra slots)
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
    // More material samplers 9..15 (individual).
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
    // Extras 17..23 (individual — debug overlays etc.).
    for (uint32_t i = 17; i < 24; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
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

// --- Shader ---

ShaderHandle VulkanRenderDevice::create_shader(const ShaderDesc& desc) {
    std::vector<uint32_t> spirv;

    try {
        if (!desc.bytecode.empty()) {
            // Use provided SPIR-V
            spirv.resize(desc.bytecode.size() / 4);
            std::memcpy(spirv.data(), desc.bytecode.data(), desc.bytecode.size());
        } else if (!desc.source.empty()) {
            // Resolve #include / @features etc. through the shared
            // preprocessor hook, then compile the GLSL to SPIR-V via
            // shaderc. OpenGL runs the same preprocess step — shaders
            // stay identical across backends.
            auto t_pre0 = std::chrono::steady_clock::now();
            std::string resolved = internal::preprocess_shader_source(desc.source);
            auto t_pre1 = std::chrono::steady_clock::now();
            g_shader_preprocess_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_pre1 - t_pre0).count(),
                std::memory_order_relaxed);

            auto t_compile0 = std::chrono::steady_clock::now();
            auto result = vk::compile_glsl_to_spirv(resolved, desc.stage, desc.entry_point);
            auto t_compile1 = std::chrono::steady_clock::now();
            g_shader_compile_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_compile1 - t_compile0).count(),
                std::memory_order_relaxed);
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
        res.debug_name = desc.debug_name;
        if (desc.stage == ShaderStage::Vertex) {
            auto t_reflect0 = std::chrono::steady_clock::now();
            SpirvVertexInputs inputs = reflect_spirv_vertex_inputs(spirv, desc.entry_point);
            auto t_reflect1 = std::chrono::steady_clock::now();
            g_shader_reflect_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_reflect1 - t_reflect0).count(),
                std::memory_order_relaxed);
            res.vertex_input_locations_known = inputs.known;
            res.vertex_input_locations = std::move(inputs.locations);
        }

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spirv.size() * sizeof(uint32_t);
        ci.pCode = spirv.data();

        auto t_module0 = std::chrono::steady_clock::now();
        if (vkCreateShaderModule(device_, &ci, nullptr, &res.module) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }
        auto t_module1 = std::chrono::steady_clock::now();
        g_shader_module_us.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(t_module1 - t_module0).count(),
            std::memory_order_relaxed);
        g_shader_count.fetch_add(1, std::memory_order_relaxed);

        return {shaders_.add(std::move(res))};
    } catch (const std::bad_alloc& e) {
        tc_log(TC_LOG_ERROR,
            "[VulkanRenderDevice] create_shader bad_alloc: debug='%s' stage=%d entry='%s' "
            "source_bytes=%zu bytecode_bytes=%zu spirv_words=%zu: %s",
            desc.debug_name.c_str(),
            static_cast<int>(desc.stage),
            desc.entry_point.c_str(),
            desc.source.size(),
            desc.bytecode.size(),
            spirv.size(),
            e.what());
        throw;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
            "[VulkanRenderDevice] create_shader failed: debug='%s' stage=%d entry='%s' "
            "source_bytes=%zu bytecode_bytes=%zu spirv_words=%zu: %s",
            desc.debug_name.c_str(),
            static_cast<int>(desc.stage),
            desc.entry_point.c_str(),
            desc.source.size(),
            desc.bytecode.size(),
            spirv.size(),
            e.what());
        throw;
    }
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
    auto t_renderpass0 = std::chrono::steady_clock::now();
    res.render_pass = get_or_create_render_pass(
        color_fmts, desc.depth_format, needs_depth, desc.sample_count,
        LoadOp::Clear, LoadOp::Clear);
    auto t_renderpass1 = std::chrono::steady_clock::now();
    g_pipeline_renderpass_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_renderpass1 - t_renderpass0).count(),
        std::memory_order_relaxed);

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
    const VkShaderResource* vertex_shader = get_shader(desc.vertex_shader);

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
            if (!vertex_shader_uses_location(vertex_shader, attr.location)) {
                continue;
            }
            VkVertexInputAttributeDescription ad{};
            ad.location = attr.location;
            ad.binding = i;
            ad.format = vk::to_vk_vertex_format(attr.format);
            ad.offset = attr.offset;
            attributes.push_back(ad);
        }
    }

    if (vertex_shader && vertex_shader->vertex_input_locations_known) {
        std::vector<uint32_t> provided_locations;
        provided_locations.reserve(attributes.size());
        for (const auto& attr : attributes) {
            provided_locations.push_back(attr.location);
        }
        std::sort(provided_locations.begin(), provided_locations.end());
        provided_locations.erase(
            std::unique(provided_locations.begin(), provided_locations.end()),
            provided_locations.end());

        std::vector<uint32_t> missing_locations;
        for (uint32_t location : vertex_shader->vertex_input_locations) {
            if (!vertex_attributes_have_location(attributes, location)) {
                missing_locations.push_back(location);
            }
        }

        if (!missing_locations.empty()) {
            const std::string shader_name = vertex_shader->debug_name.empty()
                ? std::string("<unnamed vertex shader>")
                : vertex_shader->debug_name;
            tc_log(TC_LOG_ERROR,
                "[VulkanRenderDevice] vertex input mismatch before pipeline creation: "
                "shader='%s' shader_handle=%u entry='%s' requires_locations=[%s] "
                "provided_locations=[%s] missing_locations=[%s] raw_vertex_layouts={%s} "
                "effective_vk_attributes={%s}. This usually means the shader declares "
                "an input the mesh/drawable layout does not provide, or the pass filtered "
                "the layout more aggressively than the shader interface allows.",
                shader_name.c_str(),
                desc.vertex_shader.id,
                vertex_shader->entry_point.c_str(),
                join_u32s(vertex_shader->vertex_input_locations).c_str(),
                join_u32s(provided_locations).c_str(),
                join_u32s(missing_locations).c_str(),
                describe_vertex_layouts(desc.vertex_layouts).c_str(),
                describe_vk_vertex_attributes(attributes).c_str());
        }
    } else if (vertex_shader) {
        const std::string shader_name = vertex_shader->debug_name.empty()
            ? std::string("<unnamed vertex shader>")
            : vertex_shader->debug_name;
        tc_log(TC_LOG_WARN,
            "[VulkanRenderDevice] vertex shader input reflection unavailable: "
            "shader='%s' shader_handle=%u entry='%s'; Vulkan validation may be "
            "the first place to report vertex input mismatches.",
            shader_name.c_str(),
            desc.vertex_shader.id,
            vertex_shader->entry_point.c_str());
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
    raster.depthBiasEnable = desc.raster.depth_bias_enabled ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depth_bias_constant;
    raster.depthBiasSlopeFactor = desc.raster.depth_bias_slope;
    raster.depthBiasClamp = desc.raster.depth_bias_clamp;

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

    auto t_pipeline0 = std::chrono::steady_clock::now();
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &res.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
    auto t_pipeline1 = std::chrono::steady_clock::now();
    g_pipeline_create_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_pipeline1 - t_pipeline0).count(),
        std::memory_order_relaxed);
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
        mix(b.array_element);
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
    ResourceSetDesc normalized_desc = desc;

    // The Vulkan backend currently uses one shared descriptor set layout for
    // all graphics pipelines. If a shader statically uses a binding from that
    // layout, Vulkan requires the descriptor to have been updated even when a
    // particular pass did not bind it explicitly. Fill missing dynamic UBO
    // slots with the ring buffer at offset 0; real pass bindings still win
    // because they are already present in normalized_desc and provide the
    // dynamic offset used by bind_resource_set().
    constexpr uint32_t DYNAMIC_UBO_BINDINGS[VkResourceSetResource::DYNAMIC_UBO_COUNT] =
        {0, 1, 2, 3, 16};
    auto has_uniform_binding = [&](uint32_t binding) -> bool {
        for (const auto& b : normalized_desc.bindings) {
            if (b.binding == binding &&
                b.kind == ResourceBinding::Kind::UniformBuffer &&
                b.array_element == 0) {
                return true;
            }
        }
        return false;
    };
    if (ring_ubo_handle_) {
        for (uint32_t binding : DYNAMIC_UBO_BINDINGS) {
            if (has_uniform_binding(binding)) continue;
            ResourceBinding b;
            b.kind = ResourceBinding::Kind::UniformBuffer;
            b.binding = binding;
            b.buffer = ring_ubo_handle_;
            b.offset = 0;
            b.range = 65536;
            normalized_desc.bindings.push_back(b);
        }
    }

    auto has_sampled_binding = [&](uint32_t binding, uint32_t array_element = 0) -> bool {
        for (const auto& b : normalized_desc.bindings) {
            if (b.binding == binding &&
                b.kind == ResourceBinding::Kind::SampledTexture &&
                b.array_element == array_element) {
                return true;
            }
        }
        return false;
    };
    TextureHandle default_tex = ensure_default_sampled_texture();
    if (default_tex) {
        // Keep every sampled slot declared by the shared descriptor layout valid.
        // Shaders may statically use material slots 4..7 and 9..15,
        // shadow-map array 8, or extra sampled slots 17..23.
        for (uint32_t binding = 4; binding < 24; ++binding) {
            if (binding == 8 || binding == 16) continue;
            if (has_sampled_binding(binding)) continue;
            ResourceBinding b;
            b.kind = ResourceBinding::Kind::SampledTexture;
            b.binding = binding;
            b.texture = default_tex;
            normalized_desc.bindings.push_back(b);
        }
        constexpr uint32_t SHADOW_MAP_DESCRIPTOR_COUNT = 16;
        for (uint32_t array_element = 0; array_element < SHADOW_MAP_DESCRIPTOR_COUNT; ++array_element) {
            if (has_sampled_binding(8, array_element)) continue;
            ResourceBinding b;
            b.kind = ResourceBinding::Kind::SampledTexture;
            b.binding = 8;
            b.array_element = array_element;
            b.texture = default_tex;
            normalized_desc.bindings.push_back(b);
        }
    }

    std::sort(normalized_desc.bindings.begin(), normalized_desc.bindings.end(),
              [](const ResourceBinding& a, const ResourceBinding& b) {
                  if (a.binding != b.binding) return a.binding < b.binding;
                  if (a.array_element != b.array_element) return a.array_element < b.array_element;
                  return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              });

    // Per-frame cache: multiple draws with the same bindings (typical —
    // many meshes share a material + PerFrame UBO + shadow array) reuse
    // one VkDescriptorSet instead of paying for another
    // vkAllocateDescriptorSets + vkUpdateDescriptorSets. Cache is
    // cleared when the pool is reset in submit(), so every entry is
    // always backed by a live set in descriptor_pools_[current_pool_idx_].
    const uint64_t key = hash_resource_set_desc(normalized_desc);
    auto& cache = descriptor_cache_[current_pool_idx_];
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    g_resource_set_count.fetch_add(1, std::memory_order_relaxed);
    VkResourceSetResource res;
    res.desc = normalized_desc;

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptor_pools_[current_pool_idx_];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptor_set_layout_;

    VkResult alloc_result = vkAllocateDescriptorSets(device_, &ai, &res.descriptor_set);
    if (alloc_result != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: vkAllocateDescriptorSets failed result=%d pool=%u cache_size=%zu bindings=%zu",
               static_cast<int>(alloc_result),
               current_pool_idx_,
               cache.size(),
               normalized_desc.bindings.size());
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Write descriptors
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> buf_infos;
    std::vector<VkDescriptorImageInfo> img_infos;
    buf_infos.reserve(normalized_desc.bindings.size());
    img_infos.reserve(normalized_desc.bindings.size());

    // Dynamic-UBO bindings declared by the shared layout, in the exact
    // order vkCmdBindDescriptorSets consumes the dynamic_offsets[] array
    // (sorted ascending by binding, per Vulkan spec).
    // Keep in sync with create_shared_layouts().
    auto dynamic_idx_for_binding = [&](uint32_t binding) -> int {
        for (uint32_t i = 0; i < VkResourceSetResource::DYNAMIC_UBO_COUNT; ++i)
            if (DYNAMIC_UBO_BINDINGS[i] == binding) return static_cast<int>(i);
        return -1;
    };

    for (const auto& b : normalized_desc.bindings) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = res.descriptor_set;
        w.dstBinding = b.binding;
        w.dstArrayElement = b.array_element;
        w.descriptorCount = 1;

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

        VkImageLayout final_layout = texture_post_upload_layout(res->desc);
        if (final_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            transition_image_layout(cmd, res->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout,
                vk::format_aspect_flags(res->desc.format));
        }
    });

    res->current_layout = texture_post_upload_layout(res->desc);
    defer_vma_buffer_destroy(staging, staging_alloc);
}

void VulkanRenderDevice::upload_texture_region(TextureHandle dst,
                                               uint32_t x, uint32_t y,
                                               uint32_t w, uint32_t h,
                                               std::span<const uint8_t> data,
                                               uint32_t mip) {
    auto* res = textures_.get(dst.id);
    if (!res) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: invalid texture handle %u",
               dst.id);
        return;
    }
    if (w == 0 || h == 0) {
        return;
    }
    if (mip >= res->desc.mip_levels) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: mip %u out of range for texture %u (mips=%u)",
               mip, dst.id, res->desc.mip_levels);
        return;
    }

    const uint32_t mip_w = std::max(1u, res->desc.width >> mip);
    const uint32_t mip_h = std::max(1u, res->desc.height >> mip);
    if (x >= mip_w || y >= mip_h || w > mip_w - x || h > mip_h - y) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: region (%u,%u %ux%u) outside texture %u mip %u (%ux%u)",
               x, y, w, h, dst.id, mip, mip_w, mip_h);
        return;
    }

    const uint32_t bytes_per_pixel = pixel_format_byte_size(res->desc.format);
    if (bytes_per_pixel == 0) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: unsupported texture format for texture %u",
               dst.id);
        return;
    }

    const size_t expected_size = static_cast<size_t>(w) * h * bytes_per_pixel;
    if (data.size() < expected_size) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: data too small for texture %u region (%u bytes, expected %zu)",
               dst.id, static_cast<unsigned>(data.size()), expected_size);
        return;
    }

    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = expected_size;
    stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stage_alloc_ci{};
    stage_alloc_ci.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc_ci,
                        &staging, &staging_alloc, nullptr) != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: failed to allocate staging buffer for texture %u",
               dst.id);
        return;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, staging_alloc, &mapped) != VK_SUCCESS || !mapped) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::upload_texture_region: failed to map staging buffer for texture %u",
               dst.id);
        vmaDestroyBuffer(allocator_, staging, staging_alloc);
        return;
    }
    std::memcpy(mapped, data.data(), expected_size);
    vmaUnmapMemory(allocator_, staging_alloc);

    const VkImageAspectFlags aspect = vk::format_aspect_flags(res->desc.format);
    execute_immediate([&](VkCommandBuffer cmd) {
        transition_image_layout(cmd, res->image,
            res->current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, aspect);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = aspect;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {
            static_cast<int32_t>(x),
            static_cast<int32_t>(y),
            0,
        };
        region.imageExtent = {w, h, 1};

        vkCmdCopyBufferToImage(cmd, staging, res->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        VkImageLayout final_layout = texture_post_upload_layout(res->desc);
        if (final_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            transition_image_layout(cmd, res->image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final_layout, aspect);
        }
    });

    res->current_layout = texture_post_upload_layout(res->desc);
    defer_vma_buffer_destroy(staging, staging_alloc);
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

bool VulkanRenderDevice::read_pixel_depth_float(
    TextureHandle tex, int x, int y, float* out_depth
) {
    auto* res = textures_.get(tex.id);
    if (!res || !out_depth) return false;
    if (res->desc.format != PixelFormat::D32F) return false;

    const int w = static_cast<int>(res->desc.width);
    const int h = static_cast<int>(res->desc.height);
    if (x < 0 || y < 0 || x >= w || y >= h) return false;

    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = sizeof(float);
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

    VkImageLayout prev_layout = res->current_layout;
    transition_image_layout(cb, res->image, prev_layout,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(cb, res->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    transition_image_layout(cb, res->image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            prev_layout, VK_IMAGE_ASPECT_DEPTH_BIT);
    res->current_layout = prev_layout;

    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    float depth = 1.0f;
    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, staging_alloc_h, &mapped) == VK_SUCCESS && mapped) {
        std::memcpy(&depth, mapped, sizeof(float));
        vmaUnmapMemory(allocator_, staging_alloc_h);
    }

    vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cb);

    *out_depth = depth;
    return true;
}

bool VulkanRenderDevice::read_texture_rgba_float(TextureHandle tex, float* out) {
    auto* res = textures_.get(tex.id);
    if (!res || !out) return false;
    if (is_depth_format(res->desc.format)) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_rgba_float: texture %u is a depth format",
               tex.id);
        return false;
    }
    if (!has_flag(res->desc.usage, TextureUsage::CopySrc)) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_rgba_float: texture %u missing CopySrc usage",
               tex.id);
        return false;
    }

    const uint32_t bytes_per_pixel = pixel_format_byte_size(res->desc.format);
    if (bytes_per_pixel == 0) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_rgba_float: unsupported texture format for texture %u",
               tex.id);
        return false;
    }

    const uint32_t width = res->desc.width;
    const uint32_t height = res->desc.height;
    const size_t byte_size = static_cast<size_t>(width) * height * bytes_per_pixel;

    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = byte_size;
    stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo stage_alloc{};
    stage_alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc_h = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc,
                        &staging, &staging_alloc_h, nullptr) != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_rgba_float: failed to allocate staging buffer for texture %u",
               tex.id);
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
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cb, res->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

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

    std::vector<uint8_t> bytes(byte_size);
    void* mapped = nullptr;
    bool ok = false;
    if (vmaMapMemory(allocator_, staging_alloc_h, &mapped) == VK_SUCCESS && mapped) {
        std::memcpy(bytes.data(), mapped, byte_size);
        vmaUnmapMemory(allocator_, staging_alloc_h);
        ok = true;
    }

    vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
    if (!ok) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_rgba_float: failed to map staging buffer for texture %u",
               tex.id);
        return false;
    }

    const uint32_t pixel_count = width * height;
    for (uint32_t i = 0; i < pixel_count; ++i) {
        const uint8_t* src = bytes.data() + static_cast<size_t>(i) * bytes_per_pixel;
        float* dst = out + static_cast<size_t>(i) * 4;
        switch (res->desc.format) {
            case PixelFormat::R8_UNorm:
                dst[0] = src[0] / 255.0f; dst[1] = 0.0f; dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            case PixelFormat::RG8_UNorm:
                dst[0] = src[0] / 255.0f; dst[1] = src[1] / 255.0f; dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            case PixelFormat::RGB8_UNorm:
                dst[0] = src[0] / 255.0f; dst[1] = src[1] / 255.0f; dst[2] = src[2] / 255.0f; dst[3] = 1.0f;
                break;
            case PixelFormat::RGBA8_UNorm:
                dst[0] = src[0] / 255.0f; dst[1] = src[1] / 255.0f; dst[2] = src[2] / 255.0f; dst[3] = src[3] / 255.0f;
                break;
            case PixelFormat::BGRA8_UNorm:
                dst[0] = src[2] / 255.0f; dst[1] = src[1] / 255.0f; dst[2] = src[0] / 255.0f; dst[3] = src[3] / 255.0f;
                break;
            case PixelFormat::R16F: {
                uint16_t r = 0;
                std::memcpy(&r, src, sizeof(r));
                dst[0] = half_to_float(r); dst[1] = 0.0f; dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            }
            case PixelFormat::RG16F: {
                uint16_t rg[2] = {};
                std::memcpy(rg, src, sizeof(rg));
                dst[0] = half_to_float(rg[0]); dst[1] = half_to_float(rg[1]); dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            }
            case PixelFormat::RGBA16F: {
                uint16_t rgba[4] = {};
                std::memcpy(rgba, src, sizeof(rgba));
                dst[0] = half_to_float(rgba[0]); dst[1] = half_to_float(rgba[1]);
                dst[2] = half_to_float(rgba[2]); dst[3] = half_to_float(rgba[3]);
                break;
            }
            case PixelFormat::R32F:
                std::memcpy(&dst[0], src, sizeof(float));
                dst[1] = 0.0f; dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            case PixelFormat::RG32F:
                std::memcpy(&dst[0], src, sizeof(float) * 2);
                dst[2] = 0.0f; dst[3] = 1.0f;
                break;
            case PixelFormat::RGBA32F:
                std::memcpy(dst, src, sizeof(float) * 4);
                break;
            default:
                tc_log(TC_LOG_ERROR,
                       "VulkanRenderDevice::read_texture_rgba_float: unsupported texture format for texture %u",
                       tex.id);
                return false;
        }
    }
    return true;
}

bool VulkanRenderDevice::read_texture_depth_float(TextureHandle tex, float* out) {
    auto* res = textures_.get(tex.id);
    if (!res || !out) return false;
    if (res->desc.format != PixelFormat::D32F) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_depth_float: texture %u format is not D32F",
               tex.id);
        return false;
    }
    if (!has_flag(res->desc.usage, TextureUsage::CopySrc)) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_depth_float: texture %u missing CopySrc usage",
               tex.id);
        return false;
    }

    const uint32_t width = res->desc.width;
    const uint32_t height = res->desc.height;
    const size_t byte_size = static_cast<size_t>(width) * height * sizeof(float);

    VkBufferCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stage_ci.size = byte_size;
    stage_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo stage_alloc{};
    stage_alloc.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc_h = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator_, &stage_ci, &stage_alloc,
                        &staging, &staging_alloc_h, nullptr) != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_depth_float: failed to allocate staging buffer for texture %u",
               tex.id);
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

    VkImageLayout prev_layout = res->current_layout;
    transition_image_layout(cb, res->image, prev_layout,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cb, res->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    transition_image_layout(cb, res->image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            prev_layout, VK_IMAGE_ASPECT_DEPTH_BIT);
    res->current_layout = prev_layout;

    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);

    void* mapped = nullptr;
    bool ok = false;
    if (vmaMapMemory(allocator_, staging_alloc_h, &mapped) == VK_SUCCESS && mapped) {
        std::memcpy(out, mapped, byte_size);
        vmaUnmapMemory(allocator_, staging_alloc_h);
        ok = true;
    }

    vmaDestroyBuffer(allocator_, staging, staging_alloc_h);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cb);
    if (!ok) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::read_texture_depth_float: failed to map staging buffer for texture %u",
               tex.id);
    }
    return ok;
}

uint64_t VulkanRenderDevice::request_pixel_rgba8(TextureHandle tex, int x, int y) {
    return request_pixel_readback(tex, x, y, PixelReadbackKind::Rgba8);
}

bool VulkanRenderDevice::poll_pixel_rgba8(uint64_t request_id, float out_rgba[4]) {
    if (request_id == 0 || !out_rgba) return false;
    auto it = completed_pixel_readbacks_.find(request_id);
    if (it == completed_pixel_readbacks_.end()) return false;
    if (it->second.kind != PixelReadbackKind::Rgba8) {
        completed_pixel_readbacks_.erase(it);
        return false;
    }
    const auto bytes = it->second.bytes;
    completed_pixel_readbacks_.erase(it);
    out_rgba[0] = bytes[0] / 255.0f;
    out_rgba[1] = bytes[1] / 255.0f;
    out_rgba[2] = bytes[2] / 255.0f;
    out_rgba[3] = bytes[3] / 255.0f;
    return true;
}

uint64_t VulkanRenderDevice::request_pixel_depth_float(TextureHandle tex, int x, int y) {
    return request_pixel_readback(tex, x, y, PixelReadbackKind::DepthF32);
}

bool VulkanRenderDevice::poll_pixel_depth_float(uint64_t request_id, float* out_depth) {
    if (request_id == 0 || !out_depth) return false;
    auto it = completed_pixel_readbacks_.find(request_id);
    if (it == completed_pixel_readbacks_.end()) return false;
    if (it->second.kind != PixelReadbackKind::DepthF32) {
        completed_pixel_readbacks_.erase(it);
        return false;
    }
    std::memcpy(out_depth, it->second.bytes.data(), sizeof(float));
    completed_pixel_readbacks_.erase(it);
    return true;
}

uint64_t VulkanRenderDevice::request_pixel_readback(
    TextureHandle tex, int x, int y, PixelReadbackKind kind
) {
    auto* res = textures_.get(tex.id);
    if (!res) return 0;

    const int w = static_cast<int>(res->desc.width);
    const int h = static_cast<int>(res->desc.height);
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    if (kind == PixelReadbackKind::DepthF32 && res->desc.format != PixelFormat::D32F) {
        return 0;
    }

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
        tc_log(TC_LOG_ERROR, "[VulkanRenderDevice] failed to allocate async pixel readback staging buffer");
        return 0;
    }

    const VkImageAspectFlags aspect =
        kind == PixelReadbackKind::DepthF32 ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkCommandBuffer cb = ensure_immediate_cb();
    VkImageLayout prev_layout = res->current_layout;
    transition_image_layout(cb, res->image, prev_layout,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspect);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspect;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(cb, res->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    transition_image_layout(cb, res->image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prev_layout, aspect);
    res->current_layout = prev_layout;

    const uint64_t request_id = next_pixel_readback_id_++;
    pixel_readbacks_current_.push_back(
        PendingPixelReadback{request_id, kind, staging, staging_alloc_h});
    return request_id;
}

void VulkanRenderDevice::complete_pixel_readbacks(std::vector<PendingPixelReadback>& pending) {
    for (const PendingPixelReadback& rb : pending) {
        CompletedPixelReadback completed{};
        completed.kind = rb.kind;
        void* mapped = nullptr;
        if (vmaMapMemory(allocator_, rb.allocation, &mapped) == VK_SUCCESS && mapped) {
            std::memcpy(completed.bytes.data(), mapped, completed.bytes.size());
            vmaUnmapMemory(allocator_, rb.allocation);
            completed_pixel_readbacks_[rb.request_id] = completed;
        } else {
            tc_log(TC_LOG_ERROR,
                   "[VulkanRenderDevice] failed to map async pixel readback request=%llu",
                   static_cast<unsigned long long>(rb.request_id));
        }
        vmaDestroyBuffer(allocator_, rb.staging, rb.allocation);
    }
    pending.clear();
}

void VulkanRenderDevice::destroy_pixel_readbacks(std::vector<PendingPixelReadback>& pending) {
    for (const PendingPixelReadback& rb : pending) {
        vmaDestroyBuffer(allocator_, rb.staging, rb.allocation);
    }
    pending.clear();
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
                   "pipelines=%llu resource_sets=%llu bind_pipeline=%llu "
                   "bind_rset=%llu bind_vbo=%llu bind_ibo=%llu push_constants=%llu "
                   "record_ms=%.3f submit_ms=%.3f fence_wait_ms=%.3f",
                   static_cast<unsigned long long>(s_submits),
                   static_cast<unsigned long long>(draws),
                   static_cast<unsigned long long>(pipes),
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

// --- tc_texture / tc_mesh per-device caches -----------------------------
//
// These replace the former file-scope g_tex_cache / g_mesh_cache singletons
// in tgfx2_bridge.cpp. Bridge functions now delegate directly to
// IRenderDevice on every backend.

namespace {

PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8:   return PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8:    return PixelFormat::RGB8_UNorm;
        case TC_TEXTURE_RG8:     return PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8:      return PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F:  return PixelFormat::RGBA16F;
        case TC_TEXTURE_R16F:    return PixelFormat::R16F;
        case TC_TEXTURE_R32F:    return PixelFormat::R32F;
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

// Translate tc_texture_usage_flags bitset → tgfx::TextureUsage.
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

    if (!tex->header.is_loaded && tex->header.load_callback) {
        tc_texture_ensure_loaded_ptr(tex);
    }

    const bool gpu_first = (tex->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST);

    if (tex->width == 0 || tex->height == 0) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
               tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    if (!gpu_first && !tex->data) {
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

    if (gpu_first) {
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
    // CPU-first file textures are normally sampled by materials, but graph
    // pipelines may also route them through PresentToScreenPass/copy_texture
    // as a blit source. CopyDst is needed for staging upload; CopySrc is needed
    // for those texture-to-texture copies.
    desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;

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

bool VulkanRenderDevice::ensure_tc_shader(
    tc_shader* shader,
    ShaderHandle* out_vs,
    ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "VulkanRenderDevice::ensure_tc_shader: shader is NULL");
        return false;
    }
    if (!out_fs) {
        tc_log(TC_LOG_ERROR, "VulkanRenderDevice::ensure_tc_shader: out_fs is NULL");
        return false;
    }
    if (!shader->fragment_source) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_shader: missing fragment_source for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
    const uint32_t pool_index = shader->pool_index;
    const uint32_t version = shader->version;
    {
        std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
        auto it = tc_shader_cache_.find(pool_index);
        if (it != tc_shader_cache_.end() &&
            it->second.version == version &&
            it->second.has_vs == has_vs &&
            it->second.fs &&
            (!has_vs || it->second.vs))
        {
            if (out_vs) *out_vs = it->second.vs;
            *out_fs = it->second.fs;
            return true;
        }
        if (it != tc_shader_cache_.end()) {
            if (it->second.vs) destroy(it->second.vs);
            if (it->second.fs) destroy(it->second.fs);
            tc_shader_cache_.erase(it);
        }
    }

    ShaderHandle vs;
    if (has_vs) {
        ShaderDesc vs_desc;
        vs_desc.stage = ShaderStage::Vertex;
        vs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
        if (!termin::tgfx2_load_shader_artifact(shader->uuid, vs_desc.stage, vs_desc.bytecode)) {
            vs_desc.source = shader->vertex_source;
        }
        vs = create_shader(vs_desc);
        if (!vs) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice::ensure_tc_shader: VS compile failed for '%s'",
                   shader->name ? shader->name : shader->uuid);
            return false;
        }
    }

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStage::Fragment;
    fs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
    if (!termin::tgfx2_load_shader_artifact(shader->uuid, fs_desc.stage, fs_desc.bytecode)) {
        fs_desc.source = shader->fragment_source;
    }
    ShaderHandle fs = create_shader(fs_desc);
    if (!fs) {
        if (vs) destroy(vs);
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice::ensure_tc_shader: FS compile failed for '%s'",
               shader->name ? shader->name : shader->uuid);
        return false;
    }

    std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
    auto it = tc_shader_cache_.find(pool_index);
    if (it != tc_shader_cache_.end()) {
        if (it->second.vs) destroy(it->second.vs);
        if (it->second.fs) destroy(it->second.fs);
        tc_shader_cache_.erase(it);
    }
    CachedTcShaderEntry entry;
    entry.vs = vs;
    entry.fs = fs;
    entry.version = version;
    entry.has_vs = has_vs;
    tc_shader_cache_.emplace(pool_index, entry);

    if (out_vs) *out_vs = vs;
    *out_fs = fs;
    return true;
}

void VulkanRenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
    std::lock_guard<std::mutex> lock(tc_shader_cache_mtx_);
    auto it = tc_shader_cache_.find(pool_index);
    if (it == tc_shader_cache_.end()) return;
    if (it->second.vs) destroy(it->second.vs);
    if (it->second.fs) destroy(it->second.fs);
    tc_shader_cache_.erase(it);
}

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
