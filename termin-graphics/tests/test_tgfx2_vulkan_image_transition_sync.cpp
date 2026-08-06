#include <tgfx2/vulkan/internal/image_transition_sync.hpp>

#include <iostream>

int main() {
    const VkPipelineStageFlags stages =
        tgfx::vulkan_detail::depth_stencil_attachment_stages();
    const bool has_early =
        (stages & VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT) != 0;
    const bool has_late =
        (stages & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) != 0;
    const VkPipelineStageFlags expected =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    const VkAccessFlags color_accesses =
        tgfx::vulkan_detail::color_attachment_accesses();
    const VkAccessFlags depth_accesses =
        tgfx::vulkan_detail::depth_stencil_attachment_accesses();

    if (!has_early || !has_late || stages != expected) {
        std::cerr
            << "depth/stencil attachment transitions must synchronize both "
               "early and late fragment tests\n";
        return 1;
    }
    if ((color_accesses & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) == 0 ||
        (color_accesses & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) == 0) {
        std::cerr << "color attachment synchronization must cover reads and writes\n";
        return 1;
    }
    if ((depth_accesses & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT) == 0 ||
        (depth_accesses & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) == 0) {
        std::cerr << "depth attachment synchronization must cover reads and writes\n";
        return 1;
    }
    return 0;
}
