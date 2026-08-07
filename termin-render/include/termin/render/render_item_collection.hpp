#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <termin/render/render_export.hpp>

extern "C" {
#include <core/tc_render_item.h>
}

namespace termin {

// Owns every borrowed payload referenced by its active RenderItems. The
// storage retains capacity between frame snapshots but publishes no source-
// domain semantics.
struct RENDER_CORE_API RenderItemCollection {
    std::vector<tc_render_item> items;
    // Source-adapter payloads whose concrete types remain outside render core.
    // RenderItems may reference these values through source.adapter_data. The
    // collection owns them for the complete immutable snapshot lifetime.
    std::vector<std::shared_ptr<const void>> adapter_payloads;
    std::vector<std::vector<tc_render_item_vec3>> line_batch_points;
    std::vector<std::unique_ptr<std::string>> text_batch_strings;
    std::vector<std::unique_ptr<std::string>> foliage_batch_strings;
    size_t active_line_batch_points = 0;
    size_t active_text_batch_strings = 0;
    size_t active_foliage_batch_strings = 0;

    RenderItemCollection() = default;
    ~RenderItemCollection() = default;
    RenderItemCollection(RenderItemCollection&&) noexcept = default;
    RenderItemCollection& operator=(RenderItemCollection&&) noexcept = default;
    RenderItemCollection(const RenderItemCollection&) = delete;
    RenderItemCollection& operator=(const RenderItemCollection&) = delete;

    template <typename Payload>
    const Payload* retain_adapter_payload(
        std::shared_ptr<const Payload> payload)
    {
        if (!payload) {
            return nullptr;
        }
        const Payload* value = payload.get();
        adapter_payloads.emplace_back(std::move(payload));
        return value;
    }

    void clear() {
        items.clear();
        adapter_payloads.clear();
        active_line_batch_points = 0;
        active_text_batch_strings = 0;
        active_foliage_batch_strings = 0;
    }
};

} // namespace termin
