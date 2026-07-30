#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <string>

#include <tgfx2/font_atlas.hpp>

#include "tcplot/plot_layout2d.hpp"
#include "tcplot/tc_plot_layout2d.h"

namespace {

bool near(double left, double right, double epsilon = 1e-5) {
  return std::abs(left - right) <= epsilon;
}

tcplot::PlotFrame2D frame(float pixel_scale) {
  return {
      {0.0f, 0.0f, 900.0f, 500.0f},
      {70.0f, 40.0f, 800.0f, 400.0f},
      {-4.0, 8.0, -2.0, 6.0},
      {70.0f, 40.0f, 800.0f, 400.0f},
      pixel_scale,
  };
}

} // namespace

int main() {
  using namespace tcplot;

  const auto fitted = fit_plot_range2d({0.0, 10.0, -5.0, 5.0});
  assert(fitted);
  assert(near(fitted->x_min(), -0.5));
  assert(near(fitted->x_max(), 10.5));
  assert(near(fitted->y_min(), -5.5));
  assert(near(fitted->y_max(), 5.5));

  const auto degenerate = fit_plot_range2d({3.0, 3.0, -2.0, -2.0});
  assert(degenerate);
  assert(near(degenerate->x_min(), 2.95));
  assert(near(degenerate->x_max(), 3.05));
  assert(near(degenerate->y_min(), -2.05));
  assert(near(degenerate->y_max(), -1.95));
  assert(!fit_plot_range2d(
      {0.0, std::numeric_limits<double>::infinity(), 0.0, 1.0}));
  assert(!fit_plot_range2d({2.0, 1.0, 0.0, 1.0}));
  const auto empty = fit_optional_plot_range2d(std::nullopt);
  assert(empty);
  assert(near(empty->x_min(), -0.05));
  assert(near(empty->x_max(), 1.05));

  const auto scale_one = make_plot_ticks2d(frame(1.0f));
  const auto scale_two = make_plot_ticks2d(frame(2.0f));
  assert(scale_one && scale_two);
  assert(!scale_one->x.values.empty() && !scale_one->y.values.empty());
  assert(scale_one->x.values.size() == scale_one->x.labels.size());
  assert(scale_one->y.values.size() == scale_one->y.labels.size());
  assert(scale_two->x.values.size() <= scale_one->x.values.size());
  assert(scale_two->y.values.size() <= scale_one->y.values.size());
  assert(!make_plot_ticks2d(frame(1.0f), 0.0f, 50.0f));

  tc_plot_range2d c_fitted{};
  assert(tc_plot_fit_range2d({0.0, 10.0, -5.0, 5.0}, 0.05, &c_fitted));
  assert(near(c_fitted.x_min, -0.5));
  const tc_plot_axis_ticks_desc2d tick_desc{
      -4.0, 8.0, 800.0f, 80.0f, 1.0f, 3};
  const size_t c_tick_count =
      tc_plot_axis_ticks2d_copy(&tick_desc, nullptr, 0);
  assert(c_tick_count > 0);
  std::vector<double> c_ticks(c_tick_count);
  assert(tc_plot_axis_ticks2d_copy(&tick_desc, c_ticks.data(),
                                   c_ticks.size()) == c_tick_count);
  const size_t label_size = tc_plot_format_tick2d(c_ticks.front(), nullptr, 0);
  assert(label_size > 1);
  std::string label(label_size, '\0');
  assert(tc_plot_format_tick2d(c_ticks.front(), label.data(), label.size()) ==
         label_size);
  assert(!label.empty() && label.back() == '\0');

  tgfx::FontAtlas font(TCPLOT_TEST_FONT, 14);
  const auto narrow = measure_plot_text2d(font, "iii", 14.0f);
  const auto wide = measure_plot_text2d(font, "WWW", 14.0f);
  const auto hidpi = measure_plot_text2d(font, "WWW", 14.0f, 2.0f);
  assert(narrow && wide && hidpi);
  assert(narrow->width > 0.0f);
  assert(wide->width > narrow->width);
  assert(hidpi->width > wide->width * 1.8f);
  assert(hidpi->line_height > wide->line_height);
  assert(!measure_plot_text2d(font, "bad", -1.0f));

  tgfx::FontAtlas alternate_font(TCPLOT_TEST_FONT_ALT, 14);
  const auto alternate =
      measure_plot_text2d(alternate_font, "Axis label", 14.0f);
  assert(alternate);
  assert(alternate->width > 0.0f && alternate->line_height > 0.0f);

  return 0;
}
