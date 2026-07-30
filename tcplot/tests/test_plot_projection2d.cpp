#include <cassert>
#include <cmath>
#include <limits>

#include <termin_visual_scene/tc_visual_scene.h>

#include "tcplot/plot_projection2d.hpp"

namespace {

bool near(double left, double right) { return std::abs(left - right) <= 1e-6; }

tcplot::PlotFrame2D frame(float x, float y, float width, float height,
                          double x_min, double x_max, double y_min,
                          double y_max, float pixel_scale = 1.0f) {
  return {
      tcplot::PlotRect2D{x, y, width, height},
      tcplot::PlotRect2D{x + 20.0f, y + 10.0f, width - 30.0f, height - 30.0f},
      tcplot::PlotRange2D{x_min, x_max, y_min, y_max},
      tcplot::PlotRect2D{x + 20.0f, y + 10.0f, width - 30.0f, height - 30.0f},
      pixel_scale,
  };
}

} // namespace

int main() {
  using namespace tcplot;

  const tc_visual_scene_handle scene_handle = tc_visual_scene_create();
  const tc_visual_scene_handle other_scene_handle = tc_visual_scene_create();
  assert(tc_visual_scene_is_valid(scene_handle));
  assert(tc_visual_scene_is_valid(other_scene_handle));

  termin::visual::TcVisualScene scene{scene_handle};
  termin::visual::TcVisualScene other_scene{other_scene_handle};

  auto created = create_plot_projection2d(
      scene, frame(5.0f, 7.0f, 640.0f, 480.0f, -4.0, 8.0, -2.0, 6.0, 1.5f));
  assert(created.has_value());
  PlotProjection2D projection = *created;
  const PlotProjectionHandle2D original_handle = projection.handle();
  assert(projection.valid());
  assert(projection.matches_scene(scene));
  assert(!projection.matches_scene(other_scene));
  assert(projection.owner_scene().handle().index == scene_handle.index);
  assert(projection.owner_scene().handle().generation ==
         scene_handle.generation);

  auto initial = projection.snapshot();
  assert(initial.has_value());
  assert(initial->revision == 1);
  assert(near(initial->frame.range().x_min(), -4.0));
  assert(near(initial->frame.viewport().x(), 5.0));

  tc_plot_projection_state2d c_state{};
  assert(tc_plot_projection2d_snapshot(projection.handle(), &c_state));
  tc_plot_visual_point2d visual{};
  assert(
      tc_plot_projection_state2d_data_to_visual(&c_state, {2.0, 2.0}, &visual));
  tc_plot_point2d round_trip{};
  assert(
      tc_plot_projection_state2d_visual_to_data(&c_state, visual, &round_trip));
  assert(near(round_trip.x, 2.0));
  assert(near(round_trip.y, 2.0));

  const PlotFrame2D resized =
      frame(100.0f, 200.0f, 320.0f, 240.0f, 20.0, 40.0, 30.0, 50.0, 2.0f);
  assert(projection.update(resized));
  auto updated = projection.snapshot();
  assert(updated.has_value());
  assert(updated->revision == 2);
  assert(near(updated->frame.viewport().x(), 100.0));
  assert(near(updated->frame.range().x_min(), 20.0));
  assert(near(updated->frame.pixel_scale(), 2.0));

  tc_plot_projection_desc2d invalid = c_state.projection;
  invalid.range.x_max = invalid.range.x_min;
  assert(!tc_plot_projection2d_update(projection.handle(), &invalid));
  auto after_rejected = projection.snapshot();
  assert(after_rejected.has_value());
  assert(after_rejected->revision == 2);
  assert(near(after_rejected->frame.range().x_min(), 20.0));

  invalid = c_state.projection;
  invalid.pixel_scale = std::numeric_limits<float>::infinity();
  assert(!tc_plot_projection_desc2d_is_valid(&invalid));

  assert(destroy_plot_projection2d(projection));
  assert(!projection.valid());
  assert(!projection.snapshot().has_value());
  assert(!tc_plot_projection2d_destroy(original_handle));

  auto reused = create_plot_projection2d(
      scene, frame(0.0f, 0.0f, 100.0f, 100.0f, 0.0, 1.0, 0.0, 1.0));
  assert(reused.has_value());
  assert(reused->handle().index == original_handle.index);
  assert(reused->handle().generation != original_handle.generation);

  const PlotProjectionHandle2D owner_stale = reused->handle();
  tc_visual_scene_destroy(scene_handle);
  assert(!reused->valid());
  assert(!reused->snapshot().has_value());
  assert(tc_plot_projection2d_destroy(owner_stale));

  const tc_plot_projection_desc2d valid_desc = {
      {0.0f, 0.0f, 100.0f, 100.0f},
      {10.0f, 10.0f, 80.0f, 80.0f},
      {0.0, 1.0, 0.0, 1.0},
      {10.0f, 10.0f, 80.0f, 80.0f},
      1.0f,
  };
  const PlotProjectionHandle2D stale_scene_create =
      tc_plot_projection2d_create(scene_handle, &valid_desc);
  assert(tc_plot_projection_handle2d_is_invalid(stale_scene_create));

  tc_visual_scene_destroy(other_scene_handle);
  return 0;
}
