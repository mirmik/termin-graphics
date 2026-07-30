#pragma once

#include <optional>

#include <termin_visual_scene/scene2d.hpp>

#include "tcplot/plot_frame2d.hpp"
#include "tcplot/tc_plot_projection2d.h"

namespace tcplot {

using PlotProjectionHandle2D = tc_plot_projection_handle2d;

struct PlotProjectionSnapshot2D {
  PlotFrame2D frame;
  std::uint64_t revision = 0;
};

// Copyable non-owning facade. Projection creation and destruction are explicit
// so no language wrapper silently extends either projection or scene lifetime.
class TCPLOT_API PlotProjection2D final {
public:
  PlotProjection2D() = default;
  explicit PlotProjection2D(PlotProjectionHandle2D handle) : handle_(handle) {}

  bool valid() const;
  bool matches_scene(const termin::visual::TcVisualScene &scene) const;
  termin::visual::TcVisualScene owner_scene() const;

  bool update(const PlotFrame2D &frame);
  std::optional<PlotProjectionSnapshot2D> snapshot() const;

  PlotProjectionHandle2D handle() const { return handle_; }

private:
  PlotProjectionHandle2D handle_ = tc_plot_projection_handle2d_invalid();
};

TCPLOT_API std::optional<PlotProjection2D>
create_plot_projection2d(const termin::visual::TcVisualScene &owner_scene,
                         const PlotFrame2D &frame);
TCPLOT_API bool destroy_plot_projection2d(PlotProjection2D projection);

} // namespace tcplot
