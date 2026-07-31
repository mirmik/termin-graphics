#pragma once

#include <algorithm>
#include <cstdint>

namespace tgfx::vulkan_detail {

// Half-open byte range written through a persistently mapped allocation.
// Alignment is deferred until submission so coherent-memory writes can bypass
// dirty tracking entirely.
struct NonCoherentDirtyRange {
    uint64_t begin = 0;
    uint64_t end = 0;

    [[nodiscard]] bool empty() const noexcept { return begin == end; }

    void clear() noexcept {
        begin = 0;
        end = 0;
    }

    [[nodiscard]] bool include(
        uint64_t offset,
        uint64_t size,
        uint64_t allocation_size) noexcept
    {
        if (size == 0) {
            return offset <= allocation_size;
        }
        if (offset > allocation_size || size > allocation_size - offset) {
            return false;
        }

        const uint64_t write_end = offset + size;
        if (empty()) {
            begin = offset;
            end = write_end;
        } else {
            begin = std::min(begin, offset);
            end = std::max(end, write_end);
        }
        return true;
    }

    [[nodiscard]] NonCoherentDirtyRange aligned(
        uint64_t non_coherent_atom_size,
        uint64_t allocation_size) const noexcept
    {
        if (empty()) {
            return {};
        }

        const uint64_t atom = std::max<uint64_t>(non_coherent_atom_size, 1);
        NonCoherentDirtyRange result;
        result.begin = (begin / atom) * atom;
        const uint64_t rounded_end = (end / atom) * atom;
        result.end = end == rounded_end
            ? end
            : std::min(allocation_size, rounded_end + atom);
        return result;
    }
};

} // namespace tgfx::vulkan_detail
