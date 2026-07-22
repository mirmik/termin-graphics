#include <type_traits>

#include "tgfx2/d3d11/d3d11_presentation.hpp"

int main() {
    static_assert(std::is_standard_layout_v<tgfx::D3D11PresentationPlan>);

    const auto vsync_without_tearing = tgfx::resolve_d3d11_presentation(
        tgfx::PresentationMode::VSync,
        false);
    if (!vsync_without_tearing.supported ||
        vsync_without_tearing.effective_mode != tgfx::PresentationMode::VSync ||
        vsync_without_tearing.sync_interval != 1 ||
        vsync_without_tearing.allow_tearing) {
        return 1;
    }

    const auto vsync_with_tearing = tgfx::resolve_d3d11_presentation(
        tgfx::PresentationMode::VSync,
        true);
    if (!vsync_with_tearing.supported ||
        !vsync_with_tearing.tearing_supported ||
        vsync_with_tearing.allow_tearing) {
        return 2;
    }

    const auto immediate_supported = tgfx::resolve_d3d11_presentation(
        tgfx::PresentationMode::Immediate,
        true);
    if (!immediate_supported.supported ||
        immediate_supported.effective_mode != tgfx::PresentationMode::Immediate ||
        immediate_supported.sync_interval != 0 ||
        !immediate_supported.tearing_supported ||
        !immediate_supported.allow_tearing) {
        return 3;
    }

    const auto immediate_unsupported = tgfx::resolve_d3d11_presentation(
        tgfx::PresentationMode::Immediate,
        false);
    if (immediate_unsupported.supported ||
        immediate_unsupported.sync_interval != 0 ||
        immediate_unsupported.tearing_supported ||
        immediate_unsupported.allow_tearing) {
        return 4;
    }

    return 0;
}
