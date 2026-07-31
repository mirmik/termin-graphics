#pragma once

#include <vulkan/vulkan.h>

namespace tgfx::vulkan_detail {

// A depth/stencil attachment may perform reads and writes in either fragment
// test stage. Restricting a layout transition to only one of them leaves the
// other stage outside the dependency scope and can expose stale depth data to
// a following shader read on stricter drivers.
inline constexpr VkPipelineStageFlags depth_stencil_attachment_stages() {
    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
}

} // namespace tgfx::vulkan_detail
