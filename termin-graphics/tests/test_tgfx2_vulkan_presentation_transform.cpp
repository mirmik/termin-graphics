#include "tgfx2/vulkan/vulkan_swapchain.hpp"

#include <cstdio>

int main() {
    using tgfx::select_swapchain_pre_transform;
    using tgfx::swapchain_result_requires_recreate;

    const auto identity_only = select_swapchain_pre_transform(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
    if (identity_only != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        std::fprintf(stderr, "identity-only surface did not select identity\n");
        return 1;
    }

    const auto rotated_with_identity =
        select_swapchain_pre_transform(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
    if (rotated_with_identity != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        std::fprintf(stderr, "rotated surface did not prefer identity\n");
        return 1;
    }

    const auto rotated_without_identity = select_swapchain_pre_transform(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
    if (rotated_without_identity != 0) {
        std::fprintf(stderr, "surface without identity did not report unsupported policy\n");
        return 1;
    }

    if (swapchain_result_requires_recreate(VK_SUCCESS) || swapchain_result_requires_recreate(VK_SUBOPTIMAL_KHR) ||
        !swapchain_result_requires_recreate(VK_ERROR_OUT_OF_DATE_KHR)) {
        std::fprintf(stderr, "swapchain recreate policy is inconsistent\n");
        return 1;
    }

    return 0;
}
