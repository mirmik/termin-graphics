#include "tgfx2/vulkan/vulkan_swapchain.hpp"

#include <array>
#include <cstdio>

int main() {
    using tgfx::select_swapchain_pre_transform;
    using tgfx::select_swapchain_surface_format;
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

    const std::array formats = {
        VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    if (select_swapchain_surface_format(formats).format != VK_FORMAT_R8G8B8A8_SRGB) {
        std::fprintf(stderr, "swapchain did not prefer an sRGB attachment format\n");
        return 1;
    }

    const std::array linear_only = {
        VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    if (select_swapchain_surface_format(linear_only).format != VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "swapchain accepted a surface that cannot satisfy its sRGB contract\n");
        return 1;
    }

    const std::array arbitrary_format = {
        VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    if (select_swapchain_surface_format(arbitrary_format).format != VK_FORMAT_B8G8R8A8_SRGB) {
        std::fprintf(stderr, "swapchain did not choose sRGB when the surface permits any format\n");
        return 1;
    }

    return 0;
}
