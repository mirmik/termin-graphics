#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <termin/render/drawable.hpp>
#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_scene.h>
#include <core/tc_scene_drawable.h>
}

namespace termin {

struct RenderSceneItemCollectRequest {
    tc_scene_handle scene{};
    tc_phase_mask phase = TC_PHASE_NONE;
    uint32_t flags = TC_RENDER_ITEM_COLLECT_FLAG_NONE;
    uint64_t layer_mask = UINT64_MAX;
    uint64_t render_category_mask = UINT64_MAX;
    const char* debug_pass_name = nullptr;
    const void* pass_contract = nullptr;
    const void* camera = nullptr;
    const void* scene_context = nullptr;
    void* user_context = nullptr;
    int scene_filter_flags = TC_SCENE_FILTER_ENABLED
                           | TC_SCENE_FILTER_VISIBLE
                           | TC_SCENE_FILTER_ENTITY_ENABLED;
};

struct RenderSceneItemSnapshotCounters {
    uint64_t scene_traversals = 0;
    uint64_t drawable_producers = 0;
    uint64_t emitted_items = 0;
};

struct RenderSceneItemPhaseBucket {
    tc_phase_mask phase = TC_PHASE_NONE;
    std::vector<size_t> item_indices;
};

class RENDER_API RenderSceneItemCollector {
private:
    RenderItemCollection storage_;
    uint64_t last_scene_traversals_ = 0;
    uint64_t last_drawable_producers_ = 0;

public:
    void clear_keep_capacity();

    bool collect(const RenderSceneItemCollectRequest& request);

    const std::vector<tc_render_item>& items() const {
        return storage_.items;
    }

    const RenderItemCollection& storage() const {
        return storage_;
    }

    RenderItemCollection& storage() {
        return storage_;
    }

    size_t item_count() const {
        return storage_.items.size();
    }

    const tc_render_item* item(size_t index) const {
        if (index >= storage_.items.size()) {
            return nullptr;
        }
        return &storage_.items[index];
    }

    uint64_t last_scene_traversals() const { return last_scene_traversals_; }
    uint64_t last_drawable_producers() const { return last_drawable_producers_; }

};

// Immutable after collect(). One instance belongs to one scene/view execution.
// Producers are called with TC_PHASE_NONE and publish all of their phase
// variants in one invocation; passes only route the resulting stable items.
class RENDER_API RenderSceneItemSnapshot {
private:
    RenderSceneItemCollector collector_;
    RenderSceneItemSnapshotCounters counters_{};
    std::vector<RenderSceneItemPhaseBucket> phase_buckets_;
    bool valid_ = false;

public:
    bool collect(const RenderSceneItemCollectRequest& request);
    void invalidate_keep_capacity();

    const std::vector<tc_render_item>& items() const { return collector_.items(); }
    const RenderItemCollection& storage() const { return collector_.storage(); }
    const RenderSceneItemSnapshotCounters& counters() const { return counters_; }
    bool valid() const { return valid_; }
    size_t item_count() const { return collector_.item_count(); }
    const tc_render_item* item(size_t index) const { return collector_.item(index); }
    std::span<const size_t> phase_item_indices(tc_phase_mask phase) const;
};

struct ExecuteContext;
RENDER_API RenderSceneItemSnapshot* ensure_render_item_snapshot(
    ExecuteContext& context,
    const char* debug_pass_name);

RENDER_API bool render_item_matches_phase(
    const tc_render_item& item,
    tc_phase_mask phase);

} // namespace termin
