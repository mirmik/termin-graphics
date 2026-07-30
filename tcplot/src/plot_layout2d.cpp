#include "tcplot/plot_layout2d.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include <tcbase/tc_log.hpp>
#include <tgfx2/font_atlas.hpp>

#include "tcplot/axes.hpp"

namespace tcplot {
namespace {

bool finite_range(PlotRange2D range) {
  return std::isfinite(range.x_min()) && std::isfinite(range.x_max()) &&
         std::isfinite(range.y_min()) && std::isfinite(range.y_max());
}

PlotAxisTicks2D axis_ticks(double lo, double hi, int max_ticks) {
  PlotAxisTicks2D result;
  result.values = axes::nice_ticks(lo, hi, max_ticks);
  result.labels.reserve(result.values.size());
  for (double value : result.values)
    result.labels.push_back(axes::format_tick(value));
  return result;
}

} // namespace

std::optional<PlotRange2D> fit_plot_range2d(PlotRange2D data_bounds,
                                            double padding_fraction) {
  if (!finite_range(data_bounds) || data_bounds.x_max() < data_bounds.x_min() ||
      data_bounds.y_max() < data_bounds.y_min() ||
      !std::isfinite(padding_fraction) || padding_fraction < 0.0) {
    tc::Log::error("fit_plot_range2d rejected invalid bounds or padding");
    return std::nullopt;
  }
  const double dx = data_bounds.x_span() > 0.0 ? data_bounds.x_span() : 1.0;
  const double dy = data_bounds.y_span() > 0.0 ? data_bounds.y_span() : 1.0;
  return PlotRange2D{
      data_bounds.x_min() - dx * padding_fraction,
      data_bounds.x_max() + dx * padding_fraction,
      data_bounds.y_min() - dy * padding_fraction,
      data_bounds.y_max() + dy * padding_fraction,
  };
}

std::optional<PlotRange2D>
fit_optional_plot_range2d(std::optional<PlotRange2D> data_bounds,
                          double padding_fraction) {
  return fit_plot_range2d(data_bounds.value_or(PlotRange2D{0.0, 1.0, 0.0, 1.0}),
                          padding_fraction);
}

std::optional<PlotTicks2D> make_plot_ticks2d(const PlotFrame2D &frame,
                                             float x_spacing_logical_px,
                                             float y_spacing_logical_px,
                                             int minimum_ticks) {
  const PlotRect2D &area = frame.plot_area();
  const PlotRange2D &range = frame.range();
  if (!finite_range(range) || range.x_span() <= 0.0 || range.y_span() <= 0.0 ||
      !std::isfinite(area.width()) || !std::isfinite(area.height()) ||
      area.width() <= 0.0f || area.height() <= 0.0f ||
      !std::isfinite(frame.pixel_scale()) || frame.pixel_scale() <= 0.0f ||
      !std::isfinite(x_spacing_logical_px) ||
      !std::isfinite(y_spacing_logical_px) || x_spacing_logical_px <= 0.0f ||
      y_spacing_logical_px <= 0.0f || minimum_ticks < 1) {
    tc::Log::error("make_plot_ticks2d rejected invalid layout input");
    return std::nullopt;
  }
  const float x_spacing = x_spacing_logical_px * frame.pixel_scale();
  const float y_spacing = y_spacing_logical_px * frame.pixel_scale();
  const int max_x_ticks =
      std::max(static_cast<int>(area.width() / x_spacing), minimum_ticks);
  const int max_y_ticks =
      std::max(static_cast<int>(area.height() / y_spacing), minimum_ticks);
  return PlotTicks2D{
      axis_ticks(range.x_min(), range.x_max(), max_x_ticks),
      axis_ticks(range.y_min(), range.y_max(), max_y_ticks),
  };
}

std::optional<PlotTextMetrics2D> measure_plot_text2d(tgfx::FontAtlas &font,
                                                     std::string_view text,
                                                     float font_size_logical_px,
                                                     float pixel_scale) {
  if (!std::isfinite(font_size_logical_px) || font_size_logical_px <= 0.0f ||
      !std::isfinite(pixel_scale) || pixel_scale <= 0.0f) {
    tc::Log::error("measure_plot_text2d rejected invalid size or pixel scale");
    return std::nullopt;
  }
  const float physical_size = font_size_logical_px * pixel_scale;
  if (!std::isfinite(physical_size) || physical_size <= 0.0f) {
    tc::Log::error("measure_plot_text2d physical size overflowed");
    return std::nullopt;
  }
  try {
    font.ensure_glyphs(text, physical_size);
    const auto measured = font.measure_text(text, physical_size);
    return PlotTextMetrics2D{
        measured.width,
        measured.height,
        static_cast<float>(font.ascent_px(physical_size)),
        static_cast<float>(font.descent_px(physical_size)),
        static_cast<float>(font.line_height_px(physical_size)),
    };
  } catch (const std::exception &error) {
    tc::Log::error("measure_plot_text2d failed: %s", error.what());
    return std::nullopt;
  }
}

} // namespace tcplot
