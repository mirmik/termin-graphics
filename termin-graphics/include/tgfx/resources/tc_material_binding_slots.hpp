#pragma once

#include <cstdint>

namespace termin {

// Legacy material sampler slots used by generated GLSL and by fallback
// resource binding for shaders that do not carry reflected resource layout.
constexpr uint32_t MATERIAL_TEXTURE_BINDING_BASE = 4;
constexpr uint32_t MATERIAL_TEXTURE_BINDING_SHADOW_SLOT = 8;

inline uint32_t material_texture_binding_for_index(uint32_t index) {
    uint32_t binding = MATERIAL_TEXTURE_BINDING_BASE + index;
    if (binding >= MATERIAL_TEXTURE_BINDING_SHADOW_SLOT) {
        binding += 1;
    }
    return binding;
}

} // namespace termin
