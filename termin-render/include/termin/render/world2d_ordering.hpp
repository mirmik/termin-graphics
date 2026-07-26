#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <termin/render/render_export.hpp>

namespace termin {

// Phase routing is intentionally outside this key. A render phase selects an
// ordering domain first; entries from different phases are never interleaved
// by this comparator.
struct World2DOrderKey {
    int32_t sorting_layer = 0;
    int32_t order_in_layer = 0;
    double spatial_depth = 0.0;
    uint64_t stable_tie_breaker = 0;
};

// The submission index is both the payload lookup and the final deterministic
// fallback when two producers emit identical authored keys.
struct World2DOrderEntry {
    World2DOrderKey key{};
    size_t submission_index = 0;
};

enum class World2DDepthSortMode : uint8_t {
    Disabled = 0,
    BackToFront = 1,
    FrontToBack = 2,
};

struct World2DOrderPolicy {
    World2DDepthSortMode depth_mode = World2DDepthSortMode::Disabled;
};

// Returns false when an enabled depth policy encounters a non-finite depth.
// On failure the input is left untouched, so the owning collector can reject
// the malformed frame and log it with producer context.
RENDER_API bool sort_world2d_order_entries(
    std::span<World2DOrderEntry> entries,
    World2DOrderPolicy policy = {});

} // namespace termin
