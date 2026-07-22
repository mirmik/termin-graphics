#pragma once

#include <cstdint>

#include "tgfx2/enums.hpp"

namespace tgfx {

// Backend-independent description of the native DXGI presentation choices.
// Keeping the policy free of Windows headers makes unsupported-mode behavior
// testable on every platform while D3D11Swapchain owns the native flag mapping.
struct D3D11PresentationPlan {
    PresentationMode requested_mode = PresentationMode::VSync;
    PresentationMode effective_mode = PresentationMode::VSync;
    uint32_t sync_interval = 1;
    bool supported = true;
    bool tearing_supported = false;
    bool allow_tearing = false;
};

constexpr D3D11PresentationPlan resolve_d3d11_presentation(
    PresentationMode requested_mode,
    bool tearing_supported) noexcept
{
    if (requested_mode == PresentationMode::VSync) {
        return {
            PresentationMode::VSync,
            PresentationMode::VSync,
            1,
            true,
            tearing_supported,
            false,
        };
    }
    return {
        PresentationMode::Immediate,
        PresentationMode::Immediate,
        0,
        tearing_supported,
        tearing_supported,
        tearing_supported,
    };
}

} // namespace tgfx
