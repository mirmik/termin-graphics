#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <termin/render/render_export.hpp>
#include <termin/render/render_item_collection.hpp>

namespace termin {

struct ExecuteContext;

struct RenderItemSnapshotCounters {
    uint64_t source_traversals = 0;
    uint64_t producers = 0;
    uint64_t emitted_items = 0;
};

struct RenderItemPhaseBucket {
    tc_phase_mask phase = TC_PHASE_NONE;
    std::vector<size_t> item_indices;
};

// Scene-neutral immutable frame/view snapshot. A source adapter writes into
// begin_collection(), then publishes the complete value with finish_collection().
class RENDER_API RenderItemSnapshot {
private:
    RenderItemCollection storage_;
    RenderItemSnapshotCounters counters_{};
    std::vector<RenderItemPhaseBucket> phase_buckets_;
    bool valid_ = false;

public:
    RenderItemCollection& begin_collection();
    void finish_collection(const RenderItemSnapshotCounters& counters);
    void invalidate_keep_capacity();

    const std::vector<tc_render_item>& items() const { return storage_.items; }
    const RenderItemCollection& storage() const { return storage_; }
    const RenderItemSnapshotCounters& counters() const { return counters_; }
    bool valid() const { return valid_; }
    size_t item_count() const { return storage_.items.size(); }
    const tc_render_item* item(size_t index) const;
    std::span<const size_t> phase_item_indices(tc_phase_mask phase) const;
};

RENDER_API bool render_item_matches_phase(
    const tc_render_item& item,
    tc_phase_mask phase);

RENDER_API const RenderItemSnapshot* require_render_item_snapshot(
    const ExecuteContext& context,
    const char* consumer);

} // namespace termin
