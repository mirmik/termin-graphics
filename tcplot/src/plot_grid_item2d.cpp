#include "tcplot/plot_grid_item2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.hpp>
#include <tgfx2/path2d.hpp>

namespace tcplot {
    namespace {

        constexpr const char* kPlotGridItemType = "tcplot.PlotGridItem2D";

        bool valid_ticks(std::span<const double> values) {
            return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
        }

        bool valid_style(PlotGridStyle2D style) {
            return style.color.is_finite() && std::isfinite(style.width_px) && style.width_px > 0.0f;
        }

        PlotGridStyle2D from_c(tc_plot_grid_style2d style) {
            return {
                {style.r, style.g, style.b, style.a},
                style.width_px,
            };
        }

        tc_plot_grid_style2d to_c(PlotGridStyle2D style) {
            return {
                style.color.r,
                style.color.g,
                style.color.b,
                style.color.a,
                style.width_px,
            };
        }

        std::vector<double> copy_ticks(const double* values, std::size_t count) {
            if (count == 0)
                return {};
            return {values, values + count};
        }

        void bump(std::uint64_t& revision) {
            ++revision;
            if (revision == 0)
                revision = 1;
        }

        std::optional<PlotRect2D> clipped_plot_area(const PlotFrame2D& frame) {
            const PlotRect2D& plot = frame.plot_area();
            const PlotRect2D& clip = frame.clip_rect();
            const float left = std::max(plot.x(), clip.x());
            const float top = std::max(plot.y(), clip.y());
            const float right = std::min(plot.right(), clip.right());
            const float bottom = std::min(plot.bottom(), clip.bottom());
            if (right <= left || bottom <= top)
                return std::nullopt;
            return PlotRect2D{left, top, right - left, bottom - top};
        }

    } // namespace

    PlotGridItem2D::PlotGridItem2D(PlotProjection2D projection,
                                   std::vector<double> x_ticks,
                                   std::vector<double> y_ticks,
                                   PlotGridStyle2D style)
        : NativeGraphicItem2D(kPlotGridItemType),
          projection_(projection),
          x_ticks_(std::move(x_ticks)),
          y_ticks_(std::move(y_ticks)),
          style_(style) {
        if (!projection_.valid()) {
            throw std::invalid_argument("PlotGridItem2D projection is invalid");
        }
        if (!valid_ticks(x_ticks_) || !valid_ticks(y_ticks_)) {
            throw std::invalid_argument("PlotGridItem2D ticks must be finite");
        }
        if (!valid_style(style_)) {
            throw std::invalid_argument("PlotGridItem2D style is invalid");
        }
    }

    bool PlotGridItem2D::projection_matches_item_scene() const {
        const auto item_handle = handle();
        return tc_graphic_item_handle_is_invalid(item_handle) ||
               (projection_.valid() && item_handle.scene_id == projection_.handle().scene_id);
    }

    bool PlotGridItem2D::set_projection(PlotProjection2D projection) {
        if (!projection.valid()) {
            tc::Log::error("PlotGridItem2D::set_projection rejected stale projection");
            return false;
        }
        const auto item_handle = handle();
        if (!tc_graphic_item_handle_is_invalid(item_handle) && item_handle.scene_id != projection.handle().scene_id) {
            tc::Log::error("PlotGridItem2D::set_projection rejected cross-scene projection");
            return false;
        }
        projection_ = projection;
        bump(revision_);
        return true;
    }

    bool PlotGridItem2D::set_ticks(std::vector<double> x_ticks, std::vector<double> y_ticks) {
        if (!valid_ticks(x_ticks) || !valid_ticks(y_ticks)) {
            tc::Log::error("PlotGridItem2D::set_ticks requires finite values");
            return false;
        }
        x_ticks_ = std::move(x_ticks);
        y_ticks_ = std::move(y_ticks);
        bump(revision_);
        return true;
    }

    bool PlotGridItem2D::set_style(PlotGridStyle2D style) {
        if (!valid_style(style)) {
            tc::Log::error("PlotGridItem2D::set_style rejected invalid style");
            return false;
        }
        style_ = style;
        bump(revision_);
        return true;
    }

    PlotGridSnapshot2D PlotGridItem2D::snapshot() const {
        return {
            projection_,
            style_,
            x_ticks_,
            y_ticks_,
            revision_,
        };
    }

    std::optional<termin::Bounds2f> PlotGridItem2D::local_bounds() const {
        if (!projection_matches_item_scene()) {
            tc::Log::error("PlotGridItem2D::local_bounds detected cross-scene state");
            return std::nullopt;
        }
        const auto projection = projection_.snapshot();
        if (!projection) {
            tc::Log::error("PlotGridItem2D::local_bounds projection is stale");
            return std::nullopt;
        }
        const auto area = clipped_plot_area(projection->frame);
        if (!area)
            return std::nullopt;
        return termin::Bounds2f{
            area->x(),
            area->y(),
            area->right(),
            area->bottom(),
        };
    }

    bool PlotGridItem2D::paint(termin::visual::GraphicItemPaintContext2D& context) const {
        if (!projection_matches_item_scene()) {
            tc::Log::error("PlotGridItem2D::paint rejected cross-scene projection");
            return false;
        }
        const auto projection = projection_.snapshot();
        if (!projection) {
            tc::Log::error("PlotGridItem2D::paint projection is stale");
            return false;
        }

        const PlotFrame2D& frame = projection->frame;
        const auto clip_area = clipped_plot_area(frame);
        if (!clip_area)
            return true;
        const PlotRect2D& area = frame.plot_area();
        const PlotRange2D& range = frame.range();
        tgfx::Path2f path;
        bool has_lines = false;

        for (double tick : x_ticks_) {
            if (tick < range.x_min() || tick > range.x_max())
                continue;
            const float x = frame.data_to_pixel(tick, range.y_min()).x;
            if (!path.move_to({x, area.y()}) || !path.line_to({x, area.bottom()})) {
                tc::Log::error("PlotGridItem2D::paint failed to build X grid path");
                return false;
            }
            has_lines = true;
        }
        for (double tick : y_ticks_) {
            if (tick < range.y_min() || tick > range.y_max())
                continue;
            const float y = frame.data_to_pixel(range.x_min(), tick).y;
            if (!path.move_to({area.x(), y}) || !path.line_to({area.right(), y})) {
                tc::Log::error("PlotGridItem2D::paint failed to build Y grid path");
                return false;
            }
            has_lines = true;
        }
        if (!has_lines)
            return true;
        if (!context.push_clip_rect({clip_area->x(), clip_area->y(), clip_area->width(), clip_area->height()})) {
            tc::Log::error("PlotGridItem2D::paint failed to push plot clip");
            return false;
        }
        const bool painted =
            context.path(std::move(path), std::nullopt, tgfx::StrokePaint{style_.color, style_.width_px});
        const bool popped = context.pop_clip();
        if (!popped) {
            tc::Log::error("PlotGridItem2D::paint failed to pop plot clip");
        }
        return painted && popped;
    }

    bool PlotGridItem2D::hit_test(termin::Vec2f, float) const {
        return false;
    }

    std::optional<termin::visual::GraphicItemHandle> adopt_plot_grid_item2d(termin::visual::TcVisualScene scene,
                                                                            PlotProjection2D projection,
                                                                            std::vector<double> x_ticks,
                                                                            std::vector<double> y_ticks,
                                                                            PlotGridStyle2D style) {
        if (!scene.valid() || !projection.matches_scene(scene)) {
            tc::Log::error("adopt_plot_grid_item2d requires a live matching scene and projection");
            return std::nullopt;
        }
        try {
            return scene.adopt(
                std::make_unique<PlotGridItem2D>(projection, std::move(x_ticks), std::move(y_ticks), style));
        } catch (const std::exception& error) {
            tc::Log::error("adopt_plot_grid_item2d failed: %s", error.what());
            return std::nullopt;
        }
    }

    PlotGridItem2D* resolve_plot_grid_item2d(termin::visual::TcVisualScene& scene,
                                             termin::visual::GraphicItemHandle handle) {
        tc_graphic_item* item = scene.resolve(handle);
        if (item == nullptr || item->body == nullptr ||
            std::strcmp(tc_graphic_item_type_name(item), kPlotGridItemType) != 0) {
            return nullptr;
        }
        return static_cast<PlotGridItem2D*>(item->body);
    }

    const PlotGridItem2D* resolve_plot_grid_item2d(const termin::visual::TcVisualScene& scene,
                                                   termin::visual::GraphicItemHandle handle) {
        const tc_graphic_item* item = scene.resolve(handle);
        if (item == nullptr || item->body == nullptr ||
            std::strcmp(tc_graphic_item_type_name(item), kPlotGridItemType) != 0) {
            return nullptr;
        }
        return static_cast<const PlotGridItem2D*>(item->body);
    }

} // namespace tcplot

extern "C" {

tc_graphic_item_handle tc_plot_grid_item2d_create(tc_visual_scene_handle owner_scene,
                                                  tc_plot_projection_handle2d projection,
                                                  const double* x_ticks,
                                                  size_t x_tick_count,
                                                  const double* y_ticks,
                                                  size_t y_tick_count,
                                                  tc_plot_grid_style2d style) {
    if ((x_tick_count > 0 && x_ticks == nullptr) || (y_tick_count > 0 && y_ticks == nullptr)) {
        tc::Log::error("tc_plot_grid_item2d_create received null tick data");
        return tc_graphic_item_handle_invalid();
    }
    auto handle = tcplot::adopt_plot_grid_item2d(termin::visual::TcVisualScene{owner_scene},
                                                 tcplot::PlotProjection2D{projection},
                                                 tcplot::copy_ticks(x_ticks, x_tick_count),
                                                 tcplot::copy_ticks(y_ticks, y_tick_count),
                                                 tcplot::from_c(style));
    return handle.value_or(tc_graphic_item_handle_invalid());
}

bool tc_plot_grid_item2d_set_projection(tc_visual_scene_handle owner_scene,
                                        tc_graphic_item_handle item,
                                        tc_plot_projection_handle2d projection) {
    termin::visual::TcVisualScene scene{owner_scene};
    auto* grid = tcplot::resolve_plot_grid_item2d(scene, item);
    if (grid == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_set_projection received stale or wrong item");
        return false;
    }
    return grid->set_projection(tcplot::PlotProjection2D{projection});
}

bool tc_plot_grid_item2d_set_ticks(tc_visual_scene_handle owner_scene,
                                   tc_graphic_item_handle item,
                                   const double* x_ticks,
                                   size_t x_tick_count,
                                   const double* y_ticks,
                                   size_t y_tick_count) {
    if ((x_tick_count > 0 && x_ticks == nullptr) || (y_tick_count > 0 && y_ticks == nullptr)) {
        tc::Log::error("tc_plot_grid_item2d_set_ticks received null tick data");
        return false;
    }
    termin::visual::TcVisualScene scene{owner_scene};
    auto* grid = tcplot::resolve_plot_grid_item2d(scene, item);
    if (grid == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_set_ticks received stale or wrong item");
        return false;
    }
    return grid->set_ticks(tcplot::copy_ticks(x_ticks, x_tick_count), tcplot::copy_ticks(y_ticks, y_tick_count));
}

bool tc_plot_grid_item2d_set_style(tc_visual_scene_handle owner_scene,
                                   tc_graphic_item_handle item,
                                   tc_plot_grid_style2d style) {
    termin::visual::TcVisualScene scene{owner_scene};
    auto* grid = tcplot::resolve_plot_grid_item2d(scene, item);
    if (grid == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_set_style received stale or wrong item");
        return false;
    }
    return grid->set_style(tcplot::from_c(style));
}

bool tc_plot_grid_item2d_snapshot(tc_visual_scene_handle owner_scene,
                                  tc_graphic_item_handle item,
                                  tc_plot_grid_item_snapshot2d* out_snapshot) {
    if (out_snapshot == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_snapshot requires output");
        return false;
    }
    const termin::visual::TcVisualScene scene{owner_scene};
    const auto* grid = tcplot::resolve_plot_grid_item2d(scene, item);
    if (grid == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_snapshot received stale or wrong item");
        return false;
    }
    const tcplot::PlotGridSnapshot2D snapshot = grid->snapshot();
    *out_snapshot = {
        snapshot.projection.handle(),
        tcplot::to_c(snapshot.style),
        snapshot.x_ticks.size(),
        snapshot.y_ticks.size(),
        snapshot.revision,
    };
    return true;
}

size_t tc_plot_grid_item2d_copy_ticks(tc_visual_scene_handle owner_scene,
                                      tc_graphic_item_handle item,
                                      double* out_x_ticks,
                                      size_t x_capacity,
                                      double* out_y_ticks,
                                      size_t y_capacity) {
    const termin::visual::TcVisualScene scene{owner_scene};
    const auto* grid = tcplot::resolve_plot_grid_item2d(scene, item);
    if (grid == nullptr) {
        tc::Log::error("tc_plot_grid_item2d_copy_ticks received stale or wrong item");
        return 0;
    }
    const tcplot::PlotGridSnapshot2D snapshot = grid->snapshot();
    const std::size_t total = snapshot.x_ticks.size() + snapshot.y_ticks.size();
    if (out_x_ticks == nullptr && out_y_ticks == nullptr)
        return total;
    if ((snapshot.x_ticks.size() > 0 && (out_x_ticks == nullptr || x_capacity < snapshot.x_ticks.size())) ||
        (snapshot.y_ticks.size() > 0 && (out_y_ticks == nullptr || y_capacity < snapshot.y_ticks.size()))) {
        tc::Log::error("tc_plot_grid_item2d_copy_ticks output is too small");
        return 0;
    }
    if (!snapshot.x_ticks.empty()) {
        std::copy(snapshot.x_ticks.begin(), snapshot.x_ticks.end(), out_x_ticks);
    }
    if (!snapshot.y_ticks.empty()) {
        std::copy(snapshot.y_ticks.begin(), snapshot.y_ticks.end(), out_y_ticks);
    }
    return total;
}

} // extern "C"
