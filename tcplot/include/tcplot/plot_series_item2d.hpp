#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <termin_visual_scene/native_graphic_item2d.hpp>

#include "tcplot/plot_data.hpp"
#include "tcplot/plot_projection2d.hpp"
#include "tcplot/tc_plot_series_item2d.h"

namespace tgfx {
class RenderContext2;
}

namespace tcplot {

struct PlotLineSeriesStyle2D {
  Color4 color{};
  float thickness_px = 1.5f;
  LineStyle line_style = LineStyle::Solid;
  float dash_px = 8.0f;
  float gap_px = 5.0f;
  SurfaceColorMap colormap = SurfaceColorMap::Solid;
  bool colormap_reversed = false;
  double scalar_min = 0.0;
  double scalar_max = 1.0;
};

struct PlotScatterSeriesStyle2D {
  Color4 color{};
  float diameter_px = 4.0f;
};

struct PlotNearestPoint2D {
  std::size_t index = 0;
  double data_x = 0.0;
  double data_y = 0.0;
  termin::Vec2f pixel{};
  float distance_px = 0.0f;
};

class TCPLOT_API PlotLineSeriesGpu2D {
public:
  PlotLineSeriesGpu2D();
  ~PlotLineSeriesGpu2D();
  PlotLineSeriesGpu2D(const PlotLineSeriesGpu2D &) = delete;
  PlotLineSeriesGpu2D &operator=(const PlotLineSeriesGpu2D &) = delete;

  void invalidate_data(bool append_only = false);
  void release_gpu_resources();
  bool render(tgfx::RenderContext2 &context, const PlotFrame2D &frame,
              const termin::Affine2f &transform, float opacity,
              std::optional<termin::Rect2f> clip_rect,
              std::span<const double> x, std::span<const double> y,
              std::span<const double> scalar, PlotLineSeriesStyle2D style);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TCPLOT_API PlotScatterSeriesGpu2D {
public:
  PlotScatterSeriesGpu2D();
  ~PlotScatterSeriesGpu2D();
  PlotScatterSeriesGpu2D(const PlotScatterSeriesGpu2D &) = delete;
  PlotScatterSeriesGpu2D &operator=(const PlotScatterSeriesGpu2D &) = delete;

  void invalidate_data(bool append_only = false);
  void release_gpu_resources();
  bool render(tgfx::RenderContext2 &context, const PlotFrame2D &frame,
              const termin::Affine2f &transform, float opacity,
              std::optional<termin::Rect2f> clip_rect,
              std::span<const double> x, std::span<const double> y,
              PlotScatterSeriesStyle2D style);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TCPLOT_API PlotLineSeriesItem2D final
    : public termin::visual::NativeGraphicItem2D {
public:
  struct State;

  PlotLineSeriesItem2D(PlotProjection2D projection, std::vector<double> x,
                       std::vector<double> y, std::vector<double> scalar,
                       PlotLineSeriesStyle2D style);
  ~PlotLineSeriesItem2D() override;

  bool set_projection(PlotProjection2D projection);
  bool set_data(std::vector<double> x, std::vector<double> y,
                std::vector<double> scalar = {});
  bool append(std::span<const double> x, std::span<const double> y,
              std::span<const double> scalar = {});
  bool set_style(PlotLineSeriesStyle2D style);
  PlotProjection2D projection() const;
  PlotLineSeriesStyle2D style() const;
  std::span<const double> x() const;
  std::span<const double> y() const;
  std::span<const double> scalar() const;
  std::uint64_t revision() const;
  std::optional<PlotNearestPoint2D> nearest(termin::Vec2f pixel,
                                            float max_distance_px) const;

  std::optional<termin::Bounds2f> local_bounds() const override;
  bool paint(termin::visual::GraphicItemPaintContext2D &context) const override;
  bool hit_test(termin::Vec2f point, float tolerance) const override;

private:
  std::shared_ptr<State> state_;
};

class TCPLOT_API PlotScatterSeriesItem2D final
    : public termin::visual::NativeGraphicItem2D {
public:
  struct State;

  PlotScatterSeriesItem2D(PlotProjection2D projection, std::vector<double> x,
                          std::vector<double> y,
                          PlotScatterSeriesStyle2D style);
  ~PlotScatterSeriesItem2D() override;

  bool set_projection(PlotProjection2D projection);
  bool set_data(std::vector<double> x, std::vector<double> y);
  bool set_style(PlotScatterSeriesStyle2D style);
  PlotProjection2D projection() const;
  PlotScatterSeriesStyle2D style() const;
  std::span<const double> x() const;
  std::span<const double> y() const;
  std::uint64_t revision() const;
  std::optional<PlotNearestPoint2D> nearest(termin::Vec2f pixel,
                                            float max_distance_px) const;

  std::optional<termin::Bounds2f> local_bounds() const override;
  bool paint(termin::visual::GraphicItemPaintContext2D &context) const override;
  bool hit_test(termin::Vec2f point, float tolerance) const override;

private:
  std::shared_ptr<State> state_;
};

TCPLOT_API std::optional<termin::visual::GraphicItemHandle>
adopt_plot_line_series_item2d(termin::visual::TcVisualScene scene,
                              PlotProjection2D projection,
                              std::vector<double> x, std::vector<double> y,
                              std::vector<double> scalar,
                              PlotLineSeriesStyle2D style);
TCPLOT_API std::optional<termin::visual::GraphicItemHandle>
adopt_plot_scatter_series_item2d(termin::visual::TcVisualScene scene,
                                 PlotProjection2D projection,
                                 std::vector<double> x, std::vector<double> y,
                                 PlotScatterSeriesStyle2D style);

TCPLOT_API PlotLineSeriesItem2D *
resolve_plot_line_series_item2d(termin::visual::TcVisualScene &scene,
                                termin::visual::GraphicItemHandle handle);
TCPLOT_API const PlotLineSeriesItem2D *
resolve_plot_line_series_item2d(const termin::visual::TcVisualScene &scene,
                                termin::visual::GraphicItemHandle handle);
TCPLOT_API PlotScatterSeriesItem2D *
resolve_plot_scatter_series_item2d(termin::visual::TcVisualScene &scene,
                                   termin::visual::GraphicItemHandle handle);
TCPLOT_API const PlotScatterSeriesItem2D *
resolve_plot_scatter_series_item2d(const termin::visual::TcVisualScene &scene,
                                   termin::visual::GraphicItemHandle handle);

} // namespace tcplot
