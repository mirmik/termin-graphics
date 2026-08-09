#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <termin/geom/color.hpp>
#include <termin_visual_scene/native_graphic_item2d.hpp>

#include "tcplot/plot_projection2d.hpp"
#include "tcplot/tc_plot_grid_item2d.h"

namespace tcplot {

    struct PlotGridStyle2D {
        termin::SrgbColor color{0.3f, 0.3f, 0.3f, 0.5f};
        float width_px = 1.0f;
    };

    struct PlotGridSnapshot2D {
        PlotProjection2D projection;
        PlotGridStyle2D style;
        std::vector<double> x_ticks;
        std::vector<double> y_ticks;
        std::uint64_t revision = 0;
    };

    class TCPLOT_API PlotGridItem2D final : public termin::visual::NativeGraphicItem2D {
    public:
        PlotGridItem2D(PlotProjection2D projection,
                       std::vector<double> x_ticks = {},
                       std::vector<double> y_ticks = {},
                       PlotGridStyle2D style = {});

        bool set_projection(PlotProjection2D projection);
        bool set_ticks(std::vector<double> x_ticks, std::vector<double> y_ticks);
        bool set_style(PlotGridStyle2D style);
        PlotGridSnapshot2D snapshot() const;

        std::optional<termin::Bounds2f> local_bounds() const override;
        bool paint(termin::visual::GraphicItemPaintContext2D& context) const override;
        bool hit_test(termin::Vec2f point, float tolerance) const override;

    private:
        bool projection_matches_item_scene() const;

        PlotProjection2D projection_;
        std::vector<double> x_ticks_;
        std::vector<double> y_ticks_;
        PlotGridStyle2D style_;
        std::uint64_t revision_ = 1;
    };

    TCPLOT_API std::optional<termin::visual::GraphicItemHandle>
    adopt_plot_grid_item2d(termin::visual::TcVisualScene scene,
                           PlotProjection2D projection,
                           std::vector<double> x_ticks = {},
                           std::vector<double> y_ticks = {},
                           PlotGridStyle2D style = {});

    TCPLOT_API PlotGridItem2D* resolve_plot_grid_item2d(termin::visual::TcVisualScene& scene,
                                                        termin::visual::GraphicItemHandle handle);
    TCPLOT_API const PlotGridItem2D* resolve_plot_grid_item2d(const termin::visual::TcVisualScene& scene,
                                                              termin::visual::GraphicItemHandle handle);

} // namespace tcplot
