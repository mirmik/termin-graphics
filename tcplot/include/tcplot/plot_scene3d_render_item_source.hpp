#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

// Immutable item-local CPU data shared by snapshots until the retained item
// changes. A mutation installs a new value instead of modifying data already
// visible to a published snapshot.
struct PlotScene3DItemRenderData {
    tc_plot_item3d_kind kind = TC_PLOT_ITEM3D_INVALID;
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> z;
    uint32_t rows = 0;
    uint32_t columns = 0;
    tc_surface_item3d_style surface_style{};
    tc_scatter_item3d_style scatter_style{};
    tc_grid_item3d_style grid_style{};
};

// Chart-wide values captured independently for every publication. They are
// intentionally values rather than a retained-chart pointer so an encoder can
// consume a snapshot after later chart mutations or destruction.
struct PlotScene3DFrameRenderState {
    tc_orbit_camera3d_state camera{};
    std::array<float, 3> axis_scale{1.0f, 1.0f, 1.0f};
    bool surface_shading = true;
    float surface_shading_strength = 0.38f;
    std::array<float, 3> surface_light_direction{-0.4f, -0.6f, 0.7f};
    std::array<double, 3> bounds_min{0.0, 0.0, 0.0};
    std::array<double, 3> bounds_max{1.0, 1.0, 1.0};
    std::string x_label = "x";
    std::string y_label = "y";
    std::string z_label = "z";
};

struct PlotScene3DRenderItemPayload {
    std::shared_ptr<const PlotScene3DItemRenderData> item;
    PlotScene3DFrameRenderState frame;
    uint64_t geometry_revision = 0;
    uint64_t style_revision = 0;
    uint64_t render_revision = 0;
};

// The returned source is owned by chart and remains valid only while chart is
// alive. Published snapshots own their adapter payloads and do not borrow
// mutable retained slots from the chart.
TCPLOT_API termin::RenderItemSource& plot_scene3d_render_item_source(
    tc_retained_chart3d& chart);

// Returns the immutable tcplot payload retained by the item's owning snapshot,
// or null when the item belongs to another adapter/kind or is malformed.
TCPLOT_API const PlotScene3DRenderItemPayload*
plot_scene3d_render_item_payload(const tc_render_item& item) noexcept;

} // namespace tcplot
