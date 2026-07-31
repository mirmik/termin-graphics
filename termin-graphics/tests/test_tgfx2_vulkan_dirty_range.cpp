#include <tgfx2/vulkan/internal/non_coherent_dirty_range.hpp>

#include <iostream>

using tgfx::vulkan_detail::NonCoherentDirtyRange;

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
    NonCoherentDirtyRange range;
    ok &= expect(range.empty(), "new range is empty");

    ok &= expect(range.include(513, 7, 4096), "include first write");
    ok &= expect(range.include(900, 30, 4096), "include disjoint write");
    ok &= expect(range.include(500, 4, 4096), "include earlier write");
    ok &= expect(range.begin == 500, "merged begin");
    ok &= expect(range.end == 930, "merged end");

    const NonCoherentDirtyRange aligned = range.aligned(256, 4096);
    ok &= expect(aligned.begin == 256, "atom-aligned begin");
    ok &= expect(aligned.end == 1024, "atom-aligned end");

    range.clear();
    ok &= expect(range.empty(), "clear resets range");
    ok &= expect(range.include(4080, 16, 4096), "include allocation tail");
    const NonCoherentDirtyRange allocation_tail = range.aligned(256, 4096);
    ok &= expect(allocation_tail.begin == 3840, "tail begin aligned down");
    ok &= expect(allocation_tail.end == 4096, "tail end clamped");

    range.clear();
    ok &= expect(range.include(64, 0, 4096), "zero-size write accepted");
    ok &= expect(range.empty(), "zero-size write stays empty");
    ok &= expect(!range.include(4090, 7, 4096), "out-of-bounds rejected");
    ok &= expect(range.empty(), "rejected write doesn't mutate range");

    ok &= expect(range.include(9, 1, 64), "include single byte");
    const NonCoherentDirtyRange atom_fallback = range.aligned(0, 64);
    ok &= expect(atom_fallback.begin == 9, "zero atom falls back to one");
    ok &= expect(atom_fallback.end == 10, "one-byte fallback end");
    return ok ? 0 : 1;
}
