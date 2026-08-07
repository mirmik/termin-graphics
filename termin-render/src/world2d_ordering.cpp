#include <termin/render/world2d_ordering.hpp>

#include <algorithm>
#include <cmath>

namespace termin {
    namespace {

        bool order_less(const World2DOrderEntry& lhs, const World2DOrderEntry& rhs, World2DDepthSortMode depth_mode) {
            if (lhs.key.sorting_layer != rhs.key.sorting_layer) {
                return lhs.key.sorting_layer < rhs.key.sorting_layer;
            }
            if (lhs.key.order_in_layer != rhs.key.order_in_layer) {
                return lhs.key.order_in_layer < rhs.key.order_in_layer;
            }
            if (depth_mode != World2DDepthSortMode::Disabled && lhs.key.spatial_depth != rhs.key.spatial_depth) {
                if (depth_mode == World2DDepthSortMode::BackToFront) {
                    return lhs.key.spatial_depth > rhs.key.spatial_depth;
                }
                return lhs.key.spatial_depth < rhs.key.spatial_depth;
            }
            if (lhs.key.stable_tie_breaker != rhs.key.stable_tie_breaker) {
                return lhs.key.stable_tie_breaker < rhs.key.stable_tie_breaker;
            }
            return lhs.submission_index < rhs.submission_index;
        }

    } // namespace

    bool sort_world2d_order_entries(std::span<World2DOrderEntry> entries, World2DOrderPolicy policy) {
        if (policy.depth_mode != World2DDepthSortMode::Disabled) {
            for (const World2DOrderEntry& entry : entries) {
                if (!std::isfinite(entry.key.spatial_depth)) {
                    return false;
                }
            }
        }

        std::sort(entries.begin(),
                  entries.end(),
                  [depth_mode = policy.depth_mode](const World2DOrderEntry& lhs, const World2DOrderEntry& rhs) {
                      return order_less(lhs, rhs, depth_mode);
                  });
        return true;
    }

} // namespace termin
