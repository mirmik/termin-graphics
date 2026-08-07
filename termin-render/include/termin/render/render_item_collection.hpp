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

    void clear() {
        items.clear();
        active_line_batch_points = 0;
        active_text_batch_strings = 0;
        active_foliage_batch_strings = 0;
    }
};

} // namespace termin
