#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <termin/render/drawable.hpp>
#include <termin/render/render_export.hpp>
#include <termin/render/render_item_source.hpp>

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
        int scene_filter_flags = TC_SCENE_FILTER_ENABLED | TC_SCENE_FILTER_VISIBLE | TC_SCENE_FILTER_ENTITY_ENABLED;
    };

    class RENDER_API RenderSceneItemCollector {
    private:
        RenderItemCollection storage_;
        uint64_t last_scene_traversals_ = 0;
        uint64_t last_drawable_producers_ = 0;

    public:
        void clear_keep_capacity();

        bool collect(const RenderSceneItemCollectRequest& request);
        bool collect_into(const RenderSceneItemCollectRequest& request, RenderItemCollection& output);

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

        uint64_t last_scene_traversals() const {
            return last_scene_traversals_;
        }
        uint64_t last_drawable_producers() const {
            return last_drawable_producers_;
        }
    };

    // tc_scene implementation of the scene-neutral RenderItemSource contract.
    // Scene traversal and component metadata remain confined to this adapter.
    class RENDER_API TcSceneRenderItemSource final : public RenderItemSource {
    private:
        tc_scene_handle scene_{};
        const void* scene_context_ = nullptr;
        int scene_filter_flags_ = TC_SCENE_FILTER_ENABLED | TC_SCENE_FILTER_VISIBLE | TC_SCENE_FILTER_ENTITY_ENABLED;

    protected:
        const char* source_name() const noexcept override;
        bool collect_items(const RenderItemSourceRequest& request,
                           RenderItemCollection& output,
                           RenderItemSnapshotCounters& counters) override;

    public:
        explicit TcSceneRenderItemSource(tc_scene_handle scene,
                                         const void* scene_context = nullptr,
                                         int scene_filter_flags = TC_SCENE_FILTER_ENABLED | TC_SCENE_FILTER_VISIBLE |
                                                                  TC_SCENE_FILTER_ENTITY_ENABLED);
    };

    RENDER_API tc_component* render_scene_item_component(const tc_render_item& item);

} // namespace termin
