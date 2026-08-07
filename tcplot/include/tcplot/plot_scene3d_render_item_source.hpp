#pragma once

#include <cstdint>

#include <termin/render/render_item_source.hpp>

#include "tcplot/retained_chart3d.h"
#include "tcplot/tcplot_api.h"

namespace tcplot {

// Adapter-owned values outside the built-in render-item kind range. Concrete
// draw encoders are intentionally a separate migration step; these values
// already make retained chart snapshots unambiguous to generic passes.
inline constexpr uint32_t PLOT_RENDER_ITEM_KIND_SURFACE = 0x54500001u;
inline constexpr uint32_t PLOT_RENDER_ITEM_KIND_SCATTER = 0x54500002u;
inline constexpr uint32_t PLOT_RENDER_ITEM_KIND_GRID = 0x54500003u;

// ASCII "tcplot3d" encoded as a stable adapter-owned 64-bit domain id.
inline constexpr uint64_t PLOT_RENDER_ITEM_SOURCE_DOMAIN =
    UINT64_C(0x7463706c6f743364);

// The returned source is owned by chart and remains valid only while chart is
// alive. It publishes value-only identity today, so independently published
// snapshots do not borrow mutable retained slots from the chart.
TCPLOT_API termin::RenderItemSource& plot_scene3d_render_item_source(
    tc_retained_chart3d& chart);

} // namespace tcplot
