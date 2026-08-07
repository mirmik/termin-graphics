#include <termin/render/render_item_snapshot.hpp>

#include <tcbase/tc_log.hpp>
#include <termin/render/execute_context.hpp>

extern "C" {
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
}

namespace termin {

    namespace {

        tc_material_phase* resolve_phase(const tc_render_item& item) {
            tc_material_phase* phase = nullptr;
            if (!tc_material_handle_is_invalid(item.material) && item.material_phase_index != SIZE_MAX) {
                tc_material* material = tc_material_get(item.material);
                if (material && item.material_phase_index < material->phase_count) {
                    phase = &material->phases[item.material_phase_index];
                }
            }
            return phase ? phase : item.material_phase;
        }

    } // namespace

    RenderItemCollection& RenderItemSnapshot::begin_collection() {
        invalidate_keep_capacity();
        return storage_;
    }

    void RenderItemSnapshot::finish_collection(const RenderItemSnapshotCounters& counters) {
        counters_ = counters;
        counters_.emitted_items = storage_.items.size();
        for (RenderItemPhaseBucket& bucket : phase_buckets_) {
            bucket.item_indices.clear();
        }

        for (size_t item_index = 0; item_index < storage_.items.size(); ++item_index) {
            tc_material_phase* phase = resolve_phase(storage_.items[item_index]);
            if (!phase || !tc_phase_is_single(phase->phase)) {
                continue;
            }

            RenderItemPhaseBucket* selected = nullptr;
            for (RenderItemPhaseBucket& bucket : phase_buckets_) {
                if (bucket.phase == phase->phase) {
                    selected = &bucket;
                    break;
                }
            }
            if (!selected) {
                phase_buckets_.emplace_back();
                selected = &phase_buckets_.back();
                selected->phase = phase->phase;
            }
            selected->item_indices.push_back(item_index);
        }
        valid_ = true;
    }

    void RenderItemSnapshot::invalidate_keep_capacity() {
        storage_.clear();
        counters_ = {};
        valid_ = false;
        for (RenderItemPhaseBucket& bucket : phase_buckets_) {
            bucket.item_indices.clear();
        }
    }

    const tc_render_item* RenderItemSnapshot::item(size_t index) const {
        return index < storage_.items.size() ? &storage_.items[index] : nullptr;
    }

    std::span<const size_t> RenderItemSnapshot::phase_item_indices(tc_phase_mask phase) const {
        if (!tc_phase_is_single(phase)) {
            return {};
        }
        for (const RenderItemPhaseBucket& bucket : phase_buckets_) {
            if (bucket.phase == phase) {
                return bucket.item_indices;
            }
        }
        return {};
    }

    bool render_item_matches_phase(const tc_render_item& item, tc_phase_mask requested_phase) {
        if (requested_phase == TC_PHASE_NONE) {
            return true;
        }
        tc_material_phase* phase = resolve_phase(item);
        return phase && phase->phase == requested_phase;
    }

    const RenderItemSnapshot* require_render_item_snapshot(const ExecuteContext& context, const char* consumer) {
        const char* name = consumer ? consumer : "RenderItemSnapshot";
        if (!context.render_item_snapshot) {
            tc::Log::error("[%s] render execution has no RenderItemSnapshot", name);
            return nullptr;
        }
        if (!context.render_item_snapshot->valid()) {
            tc::Log::error("[%s] RenderItemSnapshot was not published before execution", name);
            return nullptr;
        }
        return context.render_item_snapshot;
    }

} // namespace termin
