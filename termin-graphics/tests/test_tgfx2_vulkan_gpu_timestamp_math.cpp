#include <tgfx2/vulkan/internal/gpu_timestamp_math.hpp>

#include <cmath>
#include <iostream>

namespace {

    bool expect(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            return false;
        }
        return true;
    }

} // namespace

int main() {
    bool ok = true;
    double duration_ms = -1.0;
    ok &= expect(tgfx::vulkan_detail::gpu_timestamp_duration_ms(100, 2100, 64, 5.0, duration_ms),
                 "ordinary timestamp interval is valid");
    ok &= expect(std::abs(duration_ms - 0.01) < 1e-12, "ticks are converted with timestampPeriod");

    duration_ms = -1.0;
    ok &= expect(tgfx::vulkan_detail::gpu_timestamp_duration_ms(250, 5, 8, 1000.0, duration_ms),
                 "masked timestamp wrap is valid");
    ok &= expect(std::abs(duration_ms - 0.011) < 1e-12, "timestamp wrap uses valid-bit mask");

    ok &= expect(!tgfx::vulkan_detail::gpu_timestamp_duration_ms(0, 1, 0, 1.0, duration_ms),
                 "zero valid bits are rejected");
    ok &= expect(!tgfx::vulkan_detail::gpu_timestamp_duration_ms(0, 1, 65, 1.0, duration_ms),
                 "too many valid bits are rejected");
    ok &= expect(!tgfx::vulkan_detail::gpu_timestamp_duration_ms(0, 1, 64, 0.0, duration_ms),
                 "zero timestamp period is rejected");
    return ok ? 0 : 1;
}
