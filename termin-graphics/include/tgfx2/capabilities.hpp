#pragma once

#include <cstdint>
#include <string>
#include "tgfx2/enums.hpp"

namespace tgfx {

enum class AdapterClass : uint8_t {
    Unknown,
    DiscreteGpu,
    IntegratedGpu,
    VirtualGpu,
    Cpu,
};

inline const char* adapter_class_name(AdapterClass adapter_class) {
    switch (adapter_class) {
        case AdapterClass::DiscreteGpu: return "discrete-gpu";
        case AdapterClass::IntegratedGpu: return "integrated-gpu";
        case AdapterClass::VirtualGpu: return "virtual-gpu";
        case AdapterClass::Cpu: return "cpu";
        case AdapterClass::Unknown: return "unknown";
    }
    return "unknown";
}

struct AdapterInfo {
    BackendType backend = BackendType::Null;
    AdapterClass hardware_class = AdapterClass::Unknown;
    std::string adapter_name;
    std::string driver_name;

    bool is_software() const { return hardware_class == AdapterClass::Cpu; }
};

struct BackendCapabilities {
    BackendType backend = BackendType::Null;
    // Public texture coordinates use a top-left image origin:
    // row 0 in CPU uploads and v=0 in shader sampling both refer to
    // the visual top of the image. Backends with a different native
    // convention must hide it internally.
    bool texture_origin_top_left = true;
    bool supports_compute = false;
    bool supports_geometry_shaders = false;
    bool supports_timestamp_queries = false;
    bool supports_multisample_resolve = true;
    bool supports_dynamic_uniform_offsets = false;
    bool supports_storage_textures = false;
    bool supports_texture_arrays = false;
    bool supports_multiview = false;
    uint32_t max_multiview_views = 0;
    uint32_t max_color_attachments = 4;
    uint32_t max_texture_dimension_2d = 8192;
    uint32_t max_texture_units = 16;
};

} // namespace tgfx
