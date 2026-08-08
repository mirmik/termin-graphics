#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace tgfx::vulkan_detail {

    inline bool gpu_timestamp_duration_ms(std::uint64_t begin,
                                          std::uint64_t end,
                                          std::uint32_t valid_bits,
                                          double timestamp_period_ns,
                                          double& out_duration_ms) noexcept {
        if (valid_bits == 0 || valid_bits > 64 || !std::isfinite(timestamp_period_ns) || timestamp_period_ns <= 0.0) {
            return false;
        }
        const std::uint64_t mask =
            valid_bits == 64 ? std::numeric_limits<std::uint64_t>::max() : ((std::uint64_t{1} << valid_bits) - 1);
        const std::uint64_t ticks = ((end & mask) - (begin & mask)) & mask;
        const double duration_ms = static_cast<double>(ticks) * timestamp_period_ns / 1'000'000.0;
        if (!std::isfinite(duration_ms) || duration_ms < 0.0) {
            return false;
        }
        out_duration_ms = duration_ms;
        return true;
    }

} // namespace tgfx::vulkan_detail
