#include "tcplot/plot_series_item2d.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.hpp>
#include <tgfx2/builtin_shader_sources.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

namespace tcplot {
namespace {

constexpr const char *kLineItemType = "tcplot.PlotLineSeriesItem2D";
constexpr const char *kScatterItemType = "tcplot.PlotScatterSeriesItem2D";
constexpr const char *kLineShader = "termin-engine-tcplot-2d-line";
constexpr const char *kStyledLineShader = "termin-engine-tcplot-2d-styled-line";
constexpr const char *kScatterShader = "termin-engine-tcplot-2d-scatter";

struct LinePush {
  float matrix[16];
  float color[4];
};
static_assert(sizeof(LinePush) == 80);

struct StyledLinePush {
  float matrix[16];
  float color[4];
  float params[4];
  float range[4];
  float viewport[4];
};
static_assert(sizeof(StyledLinePush) == 128);

struct ScatterPush {
  float matrix[16];
  float color[4];
  float params[4];
};
static_assert(sizeof(ScatterPush) == 96);

tc_shader_handle shader_handle(const char *uuid) {
  if (std::strcmp(uuid, kLineShader) == 0) {
    static tc_shader_handle value = tc_shader_handle_invalid();
    if (tc_shader_handle_is_invalid(value)) {
      value = tgfx::register_builtin_shader_from_catalog(uuid);
    }
    return value;
  }
  if (std::strcmp(uuid, kStyledLineShader) == 0) {
    static tc_shader_handle value = tc_shader_handle_invalid();
    if (tc_shader_handle_is_invalid(value)) {
      value = tgfx::register_builtin_shader_from_catalog(uuid);
    }
    return value;
  }
  static tc_shader_handle value = tc_shader_handle_invalid();
  if (tc_shader_handle_is_invalid(value)) {
    value = tgfx::register_builtin_shader_from_catalog(kScatterShader);
  }
  return value;
}

bool finite_color(Color4 color) {
  return std::isfinite(color.r) && std::isfinite(color.g) &&
         std::isfinite(color.b) && std::isfinite(color.a);
}

bool finite_values(std::span<const double> values) {
  return std::all_of(values.begin(), values.end(),
                     [](double value) { return std::isfinite(value); });
}

bool valid_line_style(PlotLineSeriesStyle2D style) {
  const auto line_style = static_cast<int>(style.line_style);
  const auto colormap = static_cast<int>(style.colormap);
  return line_style >= static_cast<int>(LineStyle::Solid) &&
         line_style <= static_cast<int>(LineStyle::Dot) &&
         colormap >= static_cast<int>(SurfaceColorMap::Jet) &&
         colormap <= static_cast<int>(SurfaceColorMap::Solid) &&
         finite_color(style.color) && std::isfinite(style.thickness_px) &&
         style.thickness_px > 0.0f && std::isfinite(style.dash_px) &&
         style.dash_px > 0.0f && std::isfinite(style.gap_px) &&
         style.gap_px > 0.0f && std::isfinite(style.scalar_min) &&
         std::isfinite(style.scalar_max) && style.scalar_max > style.scalar_min;
}

bool valid_scatter_style(PlotScatterSeriesStyle2D style) {
  return finite_color(style.color) && std::isfinite(style.diameter_px) &&
         style.diameter_px > 0.0f;
}

bool valid_line_data(std::span<const double> x, std::span<const double> y,
                     std::span<const double> scalar) {
  return x.size() == y.size() &&
         (scalar.empty() || scalar.size() == x.size()) && finite_values(x) &&
         finite_values(y) && finite_values(scalar);
}

bool valid_scatter_data(std::span<const double> x, std::span<const double> y) {
  return x.size() == y.size() && finite_values(x) && finite_values(y);
}

void bump(std::uint64_t &revision) {
  ++revision;
  if (revision == 0)
    revision = 1;
}

std::optional<PlotRect2D> clipped_plot_area(const PlotFrame2D &frame) {
  const auto &plot = frame.plot_area();
  const auto &clip = frame.clip_rect();
  const float x0 = std::max(plot.x(), clip.x());
  const float y0 = std::max(plot.y(), clip.y());
  const float x1 = std::min(plot.right(), clip.right());
  const float y1 = std::min(plot.bottom(), clip.bottom());
  if (x1 <= x0 || y1 <= y0)
    return std::nullopt;
  return PlotRect2D{x0, y0, x1 - x0, y1 - y0};
}

std::optional<termin::Rect2f>
intersect_clip(std::optional<termin::Rect2f> requested,
               const tgfx::RetainedDrawState2D &state) {
  if (state.unsupported_clip)
    return std::nullopt;
  if (!state.has_clip_rect)
    return requested;
  if (!requested)
    return state.clip_rect;
  const float x0 = std::max(requested->x, state.clip_rect.x);
  const float y0 = std::max(requested->y, state.clip_rect.y);
  const float x1 = std::min(requested->x + requested->width,
                            state.clip_rect.x + state.clip_rect.width);
  const float y1 = std::min(requested->y + requested->height,
                            state.clip_rect.y + state.clip_rect.height);
  return termin::Rect2f{x0, y0, std::max(0.0f, x1 - x0),
                        std::max(0.0f, y1 - y0)};
}

void set_draw_area(tgfx::RenderContext2 &context,
                   const termin::Rect2f &viewport,
                   std::optional<termin::Rect2f> clip) {
  context.set_viewport(static_cast<int>(std::floor(viewport.x)),
                       static_cast<int>(std::floor(viewport.y)),
                       static_cast<int>(std::ceil(viewport.width)),
                       static_cast<int>(std::ceil(viewport.height)));
  const auto area = clip.value_or(viewport);
  context.set_scissor(static_cast<int>(std::floor(area.x)),
                      static_cast<int>(std::floor(area.y)),
                      static_cast<int>(std::ceil(area.width)),
                      static_cast<int>(std::ceil(area.height)));
  context.set_depth_test(false);
  context.set_blend(true);
  context.set_blend_func(tgfx::BlendFactor::SrcAlpha,
                         tgfx::BlendFactor::OneMinusSrcAlpha);
  context.set_cull(tgfx::CullMode::None);
}

void data_to_clip(const PlotFrame2D &frame, const termin::Affine2f &transform,
                  termin::Rect2f viewport, float out[16]) {
  std::memset(out, 0, sizeof(float) * 16);
  out[10] = 1.0f;
  out[15] = 1.0f;
  const auto &area = frame.plot_area();
  const auto &range = frame.range();
  const double sx = area.width() / range.x_span();
  const double sy = -area.height() / range.y_span();
  const double cx = area.x() - sx * range.x_min();
  const double cy = area.bottom() - sy * range.y_min();
  const double px_x = transform.m00 * sx;
  const double px_y = transform.m01 * sy;
  const double px_c = transform.m00 * cx + transform.m01 * cy + transform.tx;
  const double py_x = transform.m10 * sx;
  const double py_y = transform.m11 * sy;
  const double py_c = transform.m10 * cx + transform.m11 * cy + transform.ty;
  const double vw = std::max(viewport.width, 1.0f);
  const double vh = std::max(viewport.height, 1.0f);
  out[0] = static_cast<float>(2.0 * px_x / vw);
  out[4] = static_cast<float>(2.0 * px_y / vw);
  out[12] = static_cast<float>(2.0 * (px_c - viewport.x) / vw - 1.0);
  out[1] = static_cast<float>(2.0 * py_x / vh);
  out[5] = static_cast<float>(2.0 * py_y / vh);
  out[13] = static_cast<float>(2.0 * (py_c - viewport.y) / vh - 1.0);
}

float projected_length_scale(const PlotFrame2D &frame,
                             const termin::Affine2f &transform) {
  const auto &area = frame.plot_area();
  const auto &range = frame.range();
  const auto x = transform.transform_vector(
      {static_cast<float>(area.width() / range.x_span()), 0.0f});
  const auto y = transform.transform_vector(
      {0.0f, static_cast<float>(area.height() / range.y_span())});
  return std::sqrt((x.x * x.x + x.y * x.y + y.x * y.x + y.y * y.y) * 0.5f);
}

termin::Rect2f transformed_viewport(const PlotFrame2D &frame,
                                    const termin::Affine2f &transform) {
  const auto &viewport = frame.viewport();
  const auto bounds = transform.transform_bounds(
      {viewport.x(), viewport.y(), viewport.right(), viewport.bottom()});
  return {bounds.x0, bounds.y0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0};
}

std::optional<PlotNearestPoint2D>
nearest_point(const PlotProjection2D &projection, std::span<const double> x,
              std::span<const double> y, termin::Vec2f pixel,
              float max_distance_px) {
  if (!std::isfinite(max_distance_px) || max_distance_px < 0.0f) {
    tc::Log::error("series nearest query requires non-negative distance");
    return std::nullopt;
  }
  const auto snapshot = projection.snapshot();
  if (!snapshot || x.empty())
    return std::nullopt;
  float best_squared = max_distance_px * max_distance_px;
  std::optional<PlotNearestPoint2D> result;
  for (std::size_t index = 0; index < x.size(); ++index) {
    const auto projected = snapshot->frame.data_to_pixel(x[index], y[index]);
    const float dx = projected.x - pixel.x;
    const float dy = projected.y - pixel.y;
    const float squared = dx * dx + dy * dy;
    if (squared > best_squared)
      continue;
    best_squared = squared;
    result = PlotNearestPoint2D{index,
                                x[index],
                                y[index],
                                {projected.x, projected.y},
                                std::sqrt(squared)};
  }
  return result;
}

Color4 from_c(tc_plot_color2d value) {
  return {value.r, value.g, value.b, value.a};
}

tc_plot_color2d to_c(Color4 value) {
  return {value.r, value.g, value.b, value.a};
}

PlotLineSeriesStyle2D from_c(tc_plot_line_style_state2d value) {
  return {
      from_c(value.color),
      value.thickness_px,
      static_cast<LineStyle>(value.line_style),
      value.dash_px,
      value.gap_px,
      static_cast<SurfaceColorMap>(value.colormap),
      value.colormap_reversed,
      value.scalar_min,
      value.scalar_max,
  };
}

tc_plot_line_style_state2d to_c(PlotLineSeriesStyle2D value) {
  return {
      to_c(value.color),
      value.thickness_px,
      static_cast<tc_plot_line_style2d>(value.line_style),
      value.dash_px,
      value.gap_px,
      static_cast<tc_plot_colormap2d>(value.colormap),
      value.colormap_reversed,
      value.scalar_min,
      value.scalar_max,
  };
}

PlotScatterSeriesStyle2D from_c(tc_plot_scatter_style_state2d value) {
  return {from_c(value.color), value.diameter_px};
}

tc_plot_scatter_style_state2d to_c(PlotScatterSeriesStyle2D value) {
  return {to_c(value.color), value.diameter_px};
}

std::vector<double> copy_array(const double *values, std::size_t count) {
  return count == 0 ? std::vector<double>{}
                    : std::vector<double>{values, values + count};
}

} // namespace

struct PlotLineSeriesGpu2D::Impl {
  tgfx::IRenderDevice *device = nullptr;
  tgfx::ShaderHandle line_vs{};
  tgfx::ShaderHandle line_fs{};
  tgfx::ShaderHandle styled_vs{};
  tgfx::ShaderHandle styled_fs{};
  tgfx::BufferHandle vbo{};
  std::uint32_t capacity = 0;
  std::uint32_t gpu_count = 0;
  std::uint32_t floats_per_vertex = 0;
  bool full_dirty = true;
  bool append_only = false;
  bool styled_uploaded = false;

  void release() {
    if (device && vbo)
      device->destroy(vbo);
    device = nullptr;
    vbo = {};
    capacity = 0;
    gpu_count = 0;
    floats_per_vertex = 0;
    line_vs = {};
    line_fs = {};
    styled_vs = {};
    styled_fs = {};
    full_dirty = true;
    append_only = false;
    styled_uploaded = false;
  }

  bool ensure_shader(tgfx::IRenderDevice &requested, bool styled) {
    if (device != &requested)
      release();
    device = &requested;
    auto &vs = styled ? styled_vs : line_vs;
    auto &fs = styled ? styled_fs : line_fs;
    if (vs && fs)
      return true;
    tc_shader *raw =
        tc_shader_get(shader_handle(styled ? kStyledLineShader : kLineShader));
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, &requested, &vs, &fs)) {
      tc::Log::error("PlotLineSeriesGpu2D failed to prepare %s",
                     styled ? kStyledLineShader : kLineShader);
      return false;
    }
    return true;
  }

  bool ensure_capacity(tgfx::IRenderDevice &target, std::uint32_t wanted,
                       std::uint32_t stride_floats) {
    if (floats_per_vertex != 0 && floats_per_vertex != stride_floats) {
      if (vbo)
        target.destroy(vbo);
      vbo = {};
      capacity = 0;
      gpu_count = 0;
      full_dirty = true;
    }
    if (wanted <= capacity && vbo)
      return true;
    std::uint32_t next = capacity ? capacity * 2 : 256;
    while (next < wanted)
      next *= 2;
    if (vbo)
      target.destroy(vbo);
    tgfx::BufferDesc desc;
    desc.size =
        static_cast<std::uint64_t>(next) * stride_floats * sizeof(float);
    desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    vbo = target.create_buffer(desc);
    capacity = vbo ? next : 0;
    floats_per_vertex = vbo ? stride_floats : 0;
    gpu_count = 0;
    full_dirty = true;
    return static_cast<bool>(vbo);
  }
};

PlotLineSeriesGpu2D::PlotLineSeriesGpu2D() : impl_(std::make_unique<Impl>()) {}
PlotLineSeriesGpu2D::~PlotLineSeriesGpu2D() { release_gpu_resources(); }

void PlotLineSeriesGpu2D::invalidate_data(bool append_only) {
  impl_->append_only = append_only && !impl_->full_dirty;
  impl_->full_dirty = !append_only || impl_->full_dirty;
  impl_->styled_uploaded = false;
}

void PlotLineSeriesGpu2D::release_gpu_resources() {
  if (impl_)
    impl_->release();
}

bool PlotLineSeriesGpu2D::render(
    tgfx::RenderContext2 &context, const PlotFrame2D &frame,
    const termin::Affine2f &transform, float opacity,
    std::optional<termin::Rect2f> clip_rect, std::span<const double> x,
    std::span<const double> y, std::span<const double> scalar,
    PlotLineSeriesStyle2D style) {
  if (x.size() < 2)
    return true;
  if (!valid_line_data(x, y, scalar) || !valid_line_style(style) ||
      x.size() > std::numeric_limits<std::uint32_t>::max() ||
      !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
    tc::Log::error("PlotLineSeriesGpu2D rejected invalid render state");
    return false;
  }
  const bool styled = !scalar.empty() || style.line_style != LineStyle::Solid;
  if (!impl_->ensure_shader(context.device(), styled))
    return false;
  const auto viewport = transformed_viewport(frame, transform);
  set_draw_area(context, viewport, clip_rect);

  const std::uint32_t point_count = static_cast<std::uint32_t>(x.size());
  if (!styled) {
    if (!impl_->ensure_capacity(context.device(), point_count, 2)) {
      tc::Log::error("PlotLineSeriesGpu2D failed to allocate VBO");
      return false;
    }
    const std::uint32_t first =
        impl_->full_dirty ? 0 : std::min(impl_->gpu_count, point_count);
    if (first < point_count) {
      std::vector<float> values;
      values.reserve(static_cast<std::size_t>(point_count - first) * 2);
      for (std::uint32_t index = first; index < point_count; ++index) {
        values.push_back(static_cast<float>(x[index]));
        values.push_back(static_cast<float>(y[index]));
      }
      context.device().upload_buffer(
          impl_->vbo,
          std::span<const std::uint8_t>{
              reinterpret_cast<const std::uint8_t *>(values.data()),
              values.size() * sizeof(float)},
          static_cast<std::uint64_t>(first) * 2 * sizeof(float));
    }
    impl_->gpu_count = point_count;
    impl_->full_dirty = false;
    impl_->append_only = false;

    context.bind_shader(impl_->line_vs, impl_->line_fs);
    tc_shader *raw = tc_shader_get(shader_handle(kLineShader));
    context.use_shader_resource_layout(raw);
    tgfx::VertexBufferLayout layout;
    layout.stride = 2 * sizeof(float);
    layout.attributes.push_back({0, tgfx::VertexFormat::Float2, 0, "POSITION"});
    context.set_vertex_layout(layout);
    context.set_topology(tgfx::PrimitiveTopology::LineStrip);
    LinePush push{};
    data_to_clip(frame, transform, viewport, push.matrix);
    push.color[0] = style.color.r;
    push.color[1] = style.color.g;
    push.color[2] = style.color.b;
    push.color[3] = style.color.a * opacity;
    context.bind_uniform_data("tcplot2d_line_draw", &push, sizeof(push));
    context.draw_arrays(impl_->vbo, impl_->gpu_count);
    return true;
  }

  const std::uint32_t vertex_count = point_count * 2;
  if (!impl_->ensure_capacity(context.device(), vertex_count, 10)) {
    tc::Log::error("PlotLineSeriesGpu2D failed to allocate styled VBO");
    return false;
  }
  if (!impl_->styled_uploaded || impl_->full_dirty ||
      impl_->gpu_count != vertex_count) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(vertex_count) * 10);
    double cumulative = 0.0;
    for (std::uint32_t index = 0; index < point_count; ++index) {
      if (index > 0) {
        const double dx = x[index] - x[index - 1];
        const double dy = y[index] - y[index - 1];
        cumulative += std::sqrt(dx * dx + dy * dy);
      }
      const std::uint32_t previous = index == 0 ? 0 : index - 1;
      const std::uint32_t next = index + 1 < point_count ? index + 1 : index;
      const float scalar_value =
          scalar.empty() ? 0.0f : static_cast<float>(scalar[index]);
      for (float side : {-1.0f, 1.0f}) {
        vertices.insert(vertices.end(), {
                                            static_cast<float>(x[previous]),
                                            static_cast<float>(y[previous]),
                                            static_cast<float>(x[index]),
                                            static_cast<float>(y[index]),
                                            static_cast<float>(x[next]),
                                            static_cast<float>(y[next]),
                                            side,
                                            scalar_value,
                                            static_cast<float>(cumulative),
                                            0.0f,
                                        });
      }
    }
    context.device().upload_buffer(
        impl_->vbo, std::span<const std::uint8_t>{
                        reinterpret_cast<const std::uint8_t *>(vertices.data()),
                        vertices.size() * sizeof(float)});
    impl_->gpu_count = vertex_count;
    impl_->styled_uploaded = true;
    impl_->full_dirty = false;
    impl_->append_only = false;
  }

  context.bind_shader(impl_->styled_vs, impl_->styled_fs);
  tc_shader *raw = tc_shader_get(shader_handle(kStyledLineShader));
  context.use_shader_resource_layout(raw);
  tgfx::VertexBufferLayout layout;
  layout.stride = 10 * sizeof(float);
  layout.attributes.push_back({0, tgfx::VertexFormat::Float2, 0, "LINEPREV"});
  layout.attributes.push_back(
      {1, tgfx::VertexFormat::Float2, 2 * sizeof(float), "LINECURR"});
  layout.attributes.push_back(
      {2, tgfx::VertexFormat::Float2, 4 * sizeof(float), "LINENEXT"});
  layout.attributes.push_back(
      {3, tgfx::VertexFormat::Float4, 6 * sizeof(float), "LINEMETA"});
  context.set_vertex_layout(layout);
  context.set_topology(tgfx::PrimitiveTopology::TriangleStrip);
  StyledLinePush push{};
  data_to_clip(frame, transform, viewport, push.matrix);
  push.color[0] = style.color.r;
  push.color[1] = style.color.g;
  push.color[2] = style.color.b;
  push.color[3] = style.color.a * opacity;
  push.params[0] = style.thickness_px;
  push.params[1] = static_cast<float>(style.line_style);
  push.params[2] = scalar.empty() ? static_cast<float>(SurfaceColorMap::Solid)
                                  : static_cast<float>(style.colormap);
  push.params[3] = style.colormap_reversed ? 1.0f : 0.0f;
  push.range[0] = static_cast<float>(style.scalar_min);
  push.range[1] = static_cast<float>(style.scalar_max);
  push.range[2] = projected_length_scale(frame, transform);
  push.viewport[0] = viewport.width;
  push.viewport[1] = viewport.height;
  push.viewport[2] =
      style.line_style == LineStyle::Dot ? style.thickness_px : style.dash_px;
  push.viewport[3] = style.gap_px;
  context.bind_uniform_data("tcplot2d_styled_line_draw", &push, sizeof(push));
  context.draw_arrays(impl_->vbo, impl_->gpu_count);
  return true;
}

struct PlotScatterSeriesGpu2D::Impl {
  tgfx::IRenderDevice *device = nullptr;
  tgfx::ShaderHandle vs{};
  tgfx::ShaderHandle fs{};
  tgfx::BufferHandle corners{};
  tgfx::BufferHandle instances{};
  std::uint32_t capacity = 0;
  std::uint32_t gpu_count = 0;
  bool dirty = true;

  void release() {
    if (device) {
      if (corners)
        device->destroy(corners);
      if (instances)
        device->destroy(instances);
    }
    device = nullptr;
    vs = {};
    fs = {};
    corners = {};
    instances = {};
    capacity = 0;
    gpu_count = 0;
    dirty = true;
  }
};

PlotScatterSeriesGpu2D::PlotScatterSeriesGpu2D()
    : impl_(std::make_unique<Impl>()) {}
PlotScatterSeriesGpu2D::~PlotScatterSeriesGpu2D() { release_gpu_resources(); }

void PlotScatterSeriesGpu2D::invalidate_data(bool) { impl_->dirty = true; }

void PlotScatterSeriesGpu2D::release_gpu_resources() {
  if (impl_)
    impl_->release();
}

bool PlotScatterSeriesGpu2D::render(
    tgfx::RenderContext2 &context, const PlotFrame2D &frame,
    const termin::Affine2f &transform, float opacity,
    std::optional<termin::Rect2f> clip_rect, std::span<const double> x,
    std::span<const double> y, PlotScatterSeriesStyle2D style) {
  if (x.empty())
    return true;
  if (!valid_scatter_data(x, y) || !valid_scatter_style(style) ||
      x.size() > std::numeric_limits<std::uint32_t>::max() ||
      !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
    tc::Log::error("PlotScatterSeriesGpu2D rejected invalid render state");
    return false;
  }
  if (impl_->device != &context.device())
    impl_->release();
  impl_->device = &context.device();
  if (!impl_->vs || !impl_->fs) {
    tc_shader *raw = tc_shader_get(shader_handle(kScatterShader));
    if (!raw || !termin::tc_shader_ensure_tgfx2(raw, impl_->device, &impl_->vs,
                                                &impl_->fs)) {
      tc::Log::error("PlotScatterSeriesGpu2D failed to prepare %s",
                     kScatterShader);
      return false;
    }
  }
  if (!impl_->corners) {
    constexpr std::array<float, 12> corners{-0.5f, -0.5f, 0.5f,  -0.5f,
                                            0.5f,  0.5f,  -0.5f, -0.5f,
                                            0.5f,  0.5f,  -0.5f, 0.5f};
    tgfx::BufferDesc desc;
    desc.size = sizeof(corners);
    desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    impl_->corners = impl_->device->create_buffer(desc);
    impl_->device->upload_buffer(
        impl_->corners,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t *>(corners.data()),
            sizeof(corners)});
  }
  const auto wanted = static_cast<std::uint32_t>(x.size());
  if (wanted > impl_->capacity) {
    std::uint32_t next = impl_->capacity ? impl_->capacity * 2 : 256;
    while (next < wanted)
      next *= 2;
    if (impl_->instances)
      impl_->device->destroy(impl_->instances);
    tgfx::BufferDesc desc;
    desc.size = static_cast<std::uint64_t>(next) * 2 * sizeof(float);
    desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    impl_->instances = impl_->device->create_buffer(desc);
    impl_->capacity = impl_->instances ? next : 0;
    impl_->dirty = true;
  }
  if (!impl_->corners || !impl_->instances) {
    tc::Log::error("PlotScatterSeriesGpu2D failed to allocate VBOs");
    return false;
  }
  if (impl_->dirty || impl_->gpu_count != wanted) {
    std::vector<float> values;
    values.reserve(x.size() * 2);
    for (std::size_t index = 0; index < x.size(); ++index) {
      values.push_back(static_cast<float>(x[index]));
      values.push_back(static_cast<float>(y[index]));
    }
    impl_->device->upload_buffer(
        impl_->instances,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t *>(values.data()),
            values.size() * sizeof(float)});
    impl_->gpu_count = wanted;
    impl_->dirty = false;
  }
  const auto viewport = transformed_viewport(frame, transform);
  set_draw_area(context, viewport, clip_rect);
  context.bind_shader(impl_->vs, impl_->fs);
  tc_shader *raw = tc_shader_get(shader_handle(kScatterShader));
  context.use_shader_resource_layout(raw);
  tgfx::VertexBufferLayout corners_layout;
  corners_layout.stride = 2 * sizeof(float);
  corners_layout.attributes.push_back(
      {0, tgfx::VertexFormat::Float2, 0, "POSITION"});
  tgfx::VertexBufferLayout instance_layout;
  instance_layout.stride = 2 * sizeof(float);
  instance_layout.per_instance = true;
  instance_layout.attributes.push_back(
      {1, tgfx::VertexFormat::Float2, 0, "INSTANCEPOS"});
  context.set_vertex_layouts(std::vector<tgfx::VertexBufferLayout>{
      std::move(corners_layout), std::move(instance_layout)});
  context.set_topology(tgfx::PrimitiveTopology::TriangleList);
  ScatterPush push{};
  data_to_clip(frame, transform, viewport, push.matrix);
  push.color[0] = style.color.r;
  push.color[1] = style.color.g;
  push.color[2] = style.color.b;
  push.color[3] = style.color.a * opacity;
  push.params[0] = viewport.width;
  push.params[1] = viewport.height;
  push.params[2] = style.diameter_px;
  context.bind_uniform_data("tcplot2d_scatter_draw", &push, sizeof(push));
  context.draw_arrays_instanced(impl_->corners, impl_->instances, 6,
                                impl_->gpu_count);
  return true;
}

struct PlotLineSeriesItem2D::State {
  State(PlotProjection2D projection_value, std::vector<double> x_value,
        std::vector<double> y_value, std::vector<double> scalar_value,
        PlotLineSeriesStyle2D style_value)
      : projection(projection_value), x(std::move(x_value)),
        y(std::move(y_value)), scalar(std::move(scalar_value)),
        style(style_value) {}

  PlotProjection2D projection;
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> scalar;
  PlotLineSeriesStyle2D style;
  std::uint64_t revision = 1;
  PlotLineSeriesGpu2D gpu;
};

struct PlotScatterSeriesItem2D::State {
  State(PlotProjection2D projection_value, std::vector<double> x_value,
        std::vector<double> y_value, PlotScatterSeriesStyle2D style_value)
      : projection(projection_value), x(std::move(x_value)),
        y(std::move(y_value)), style(style_value) {}

  PlotProjection2D projection;
  std::vector<double> x;
  std::vector<double> y;
  PlotScatterSeriesStyle2D style;
  std::uint64_t revision = 1;
  PlotScatterSeriesGpu2D gpu;
};

namespace {

class LineRetainedBatch final : public tgfx::RetainedDrawBatch2D {
public:
  explicit LineRetainedBatch(std::shared_ptr<PlotLineSeriesItem2D::State> state)
      : state_(std::move(state)) {}

  bool draw(tgfx::RenderContext2 &context,
            const tgfx::RetainedDrawState2D &draw_state) override {
    if (draw_state.unsupported_clip) {
      tc::Log::error("PlotLineSeriesItem2D requires rectangular retained clip");
      return false;
    }
    const auto projection = state_->projection.snapshot();
    if (!projection) {
      tc::Log::error("PlotLineSeriesItem2D projection is stale");
      return false;
    }
    auto clip = intersect_clip(std::nullopt, draw_state);
    return state_->gpu.render(context, projection->frame, draw_state.transform,
                              draw_state.opacity, clip, state_->x, state_->y,
                              state_->scalar, state_->style);
  }

private:
  std::shared_ptr<PlotLineSeriesItem2D::State> state_;
};

class ScatterRetainedBatch final : public tgfx::RetainedDrawBatch2D {
public:
  explicit ScatterRetainedBatch(
      std::shared_ptr<PlotScatterSeriesItem2D::State> state)
      : state_(std::move(state)) {}

  bool draw(tgfx::RenderContext2 &context,
            const tgfx::RetainedDrawState2D &draw_state) override {
    if (draw_state.unsupported_clip) {
      tc::Log::error(
          "PlotScatterSeriesItem2D requires rectangular retained clip");
      return false;
    }
    const auto projection = state_->projection.snapshot();
    if (!projection) {
      tc::Log::error("PlotScatterSeriesItem2D projection is stale");
      return false;
    }
    auto clip = intersect_clip(std::nullopt, draw_state);
    return state_->gpu.render(context, projection->frame, draw_state.transform,
                              draw_state.opacity, clip, state_->x, state_->y,
                              state_->style);
  }

private:
  std::shared_ptr<PlotScatterSeriesItem2D::State> state_;
};

bool matching_scene(tc_graphic_item_handle item,
                    const PlotProjection2D &projection) {
  return tc_graphic_item_handle_is_invalid(item) ||
         (projection.valid() && item.scene_id == projection.handle().scene_id);
}

} // namespace

PlotLineSeriesItem2D::PlotLineSeriesItem2D(PlotProjection2D projection,
                                           std::vector<double> x,
                                           std::vector<double> y,
                                           std::vector<double> scalar,
                                           PlotLineSeriesStyle2D style)
    : NativeGraphicItem2D(kLineItemType),
      state_(std::make_shared<State>(projection, std::move(x), std::move(y),
                                     std::move(scalar), style)) {
  if (!projection.valid() ||
      !valid_line_data(state_->x, state_->y, state_->scalar) ||
      !valid_line_style(style)) {
    throw std::invalid_argument("invalid PlotLineSeriesItem2D state");
  }
}

PlotLineSeriesItem2D::~PlotLineSeriesItem2D() = default;

bool PlotLineSeriesItem2D::set_projection(PlotProjection2D projection) {
  if (!projection.valid() || !matching_scene(handle(), projection)) {
    tc::Log::error(
        "PlotLineSeriesItem2D rejected stale or cross-scene projection");
    return false;
  }
  state_->projection = projection;
  bump(state_->revision);
  return true;
}

bool PlotLineSeriesItem2D::set_data(std::vector<double> x,
                                    std::vector<double> y,
                                    std::vector<double> scalar) {
  if (!valid_line_data(x, y, scalar)) {
    tc::Log::error("PlotLineSeriesItem2D rejected invalid data");
    return false;
  }
  state_->x = std::move(x);
  state_->y = std::move(y);
  state_->scalar = std::move(scalar);
  state_->gpu.invalidate_data(false);
  bump(state_->revision);
  return true;
}

bool PlotLineSeriesItem2D::append(std::span<const double> x,
                                  std::span<const double> y,
                                  std::span<const double> scalar) {
  if (!valid_line_data(x, y, scalar) ||
      (!state_->scalar.empty() && scalar.size() != x.size()) ||
      (state_->scalar.empty() && !scalar.empty() && !state_->x.empty())) {
    tc::Log::error("PlotLineSeriesItem2D rejected inconsistent append");
    return false;
  }
  state_->x.insert(state_->x.end(), x.begin(), x.end());
  state_->y.insert(state_->y.end(), y.begin(), y.end());
  state_->scalar.insert(state_->scalar.end(), scalar.begin(), scalar.end());
  state_->gpu.invalidate_data(true);
  bump(state_->revision);
  return true;
}

bool PlotLineSeriesItem2D::set_style(PlotLineSeriesStyle2D style) {
  if (!valid_line_style(style)) {
    tc::Log::error("PlotLineSeriesItem2D rejected invalid style");
    return false;
  }
  state_->style = style;
  bump(state_->revision);
  return true;
}

PlotProjection2D PlotLineSeriesItem2D::projection() const {
  return state_->projection;
}
PlotLineSeriesStyle2D PlotLineSeriesItem2D::style() const {
  return state_->style;
}
std::span<const double> PlotLineSeriesItem2D::x() const { return state_->x; }
std::span<const double> PlotLineSeriesItem2D::y() const { return state_->y; }
std::span<const double> PlotLineSeriesItem2D::scalar() const {
  return state_->scalar;
}
std::uint64_t PlotLineSeriesItem2D::revision() const {
  return state_->revision;
}
std::optional<PlotNearestPoint2D>
PlotLineSeriesItem2D::nearest(termin::Vec2f pixel,
                              float max_distance_px) const {
  return nearest_point(state_->projection, state_->x, state_->y, pixel,
                       max_distance_px);
}

std::optional<termin::Bounds2f> PlotLineSeriesItem2D::local_bounds() const {
  if (!matching_scene(handle(), state_->projection))
    return std::nullopt;
  const auto projection = state_->projection.snapshot();
  if (!projection)
    return std::nullopt;
  const auto area = clipped_plot_area(projection->frame);
  if (!area)
    return std::nullopt;
  return termin::Bounds2f{area->x(), area->y(), area->right(), area->bottom()};
}

bool PlotLineSeriesItem2D::paint(
    termin::visual::GraphicItemPaintContext2D &context) const {
  if (!matching_scene(handle(), state_->projection)) {
    tc::Log::error("PlotLineSeriesItem2D detected cross-scene projection");
    return false;
  }
  const auto projection = state_->projection.snapshot();
  if (!projection) {
    tc::Log::error("PlotLineSeriesItem2D projection is stale");
    return false;
  }
  const auto area = clipped_plot_area(projection->frame);
  if (!area || state_->x.size() < 2)
    return true;
  if (!context.push_clip_rect(
          {area->x(), area->y(), area->width(), area->height()})) {
    return false;
  }
  const bool painted =
      context.retained_batch(std::make_shared<LineRetainedBatch>(state_));
  const bool popped = context.pop_clip();
  return painted && popped;
}

bool PlotLineSeriesItem2D::hit_test(termin::Vec2f point,
                                    float tolerance) const {
  return nearest(point, tolerance).has_value();
}

PlotScatterSeriesItem2D::PlotScatterSeriesItem2D(PlotProjection2D projection,
                                                 std::vector<double> x,
                                                 std::vector<double> y,
                                                 PlotScatterSeriesStyle2D style)
    : NativeGraphicItem2D(kScatterItemType),
      state_(std::make_shared<State>(projection, std::move(x), std::move(y),
                                     style)) {
  if (!projection.valid() || !valid_scatter_data(state_->x, state_->y) ||
      !valid_scatter_style(style)) {
    throw std::invalid_argument("invalid PlotScatterSeriesItem2D state");
  }
}

PlotScatterSeriesItem2D::~PlotScatterSeriesItem2D() = default;

bool PlotScatterSeriesItem2D::set_projection(PlotProjection2D projection) {
  if (!projection.valid() || !matching_scene(handle(), projection)) {
    tc::Log::error(
        "PlotScatterSeriesItem2D rejected stale or cross-scene projection");
    return false;
  }
  state_->projection = projection;
  bump(state_->revision);
  return true;
}

bool PlotScatterSeriesItem2D::set_data(std::vector<double> x,
                                       std::vector<double> y) {
  if (!valid_scatter_data(x, y)) {
    tc::Log::error("PlotScatterSeriesItem2D rejected invalid data");
    return false;
  }
  state_->x = std::move(x);
  state_->y = std::move(y);
  state_->gpu.invalidate_data(false);
  bump(state_->revision);
  return true;
}

bool PlotScatterSeriesItem2D::set_style(PlotScatterSeriesStyle2D style) {
  if (!valid_scatter_style(style)) {
    tc::Log::error("PlotScatterSeriesItem2D rejected invalid style");
    return false;
  }
  state_->style = style;
  bump(state_->revision);
  return true;
}

PlotProjection2D PlotScatterSeriesItem2D::projection() const {
  return state_->projection;
}
PlotScatterSeriesStyle2D PlotScatterSeriesItem2D::style() const {
  return state_->style;
}
std::span<const double> PlotScatterSeriesItem2D::x() const { return state_->x; }
std::span<const double> PlotScatterSeriesItem2D::y() const { return state_->y; }
std::uint64_t PlotScatterSeriesItem2D::revision() const {
  return state_->revision;
}
std::optional<PlotNearestPoint2D>
PlotScatterSeriesItem2D::nearest(termin::Vec2f pixel,
                                 float max_distance_px) const {
  return nearest_point(state_->projection, state_->x, state_->y, pixel,
                       max_distance_px);
}

std::optional<termin::Bounds2f> PlotScatterSeriesItem2D::local_bounds() const {
  if (!matching_scene(handle(), state_->projection))
    return std::nullopt;
  const auto projection = state_->projection.snapshot();
  if (!projection)
    return std::nullopt;
  const auto area = clipped_plot_area(projection->frame);
  if (!area)
    return std::nullopt;
  return termin::Bounds2f{area->x(), area->y(), area->right(), area->bottom()};
}

bool PlotScatterSeriesItem2D::paint(
    termin::visual::GraphicItemPaintContext2D &context) const {
  if (!matching_scene(handle(), state_->projection)) {
    tc::Log::error("PlotScatterSeriesItem2D detected cross-scene projection");
    return false;
  }
  const auto projection = state_->projection.snapshot();
  if (!projection) {
    tc::Log::error("PlotScatterSeriesItem2D projection is stale");
    return false;
  }
  const auto area = clipped_plot_area(projection->frame);
  if (!area || state_->x.empty())
    return true;
  if (!context.push_clip_rect(
          {area->x(), area->y(), area->width(), area->height()})) {
    return false;
  }
  const bool painted =
      context.retained_batch(std::make_shared<ScatterRetainedBatch>(state_));
  const bool popped = context.pop_clip();
  return painted && popped;
}

bool PlotScatterSeriesItem2D::hit_test(termin::Vec2f point,
                                       float tolerance) const {
  return nearest(point, tolerance + state_->style.diameter_px * 0.5f)
      .has_value();
}

std::optional<termin::visual::GraphicItemHandle> adopt_plot_line_series_item2d(
    termin::visual::TcVisualScene scene, PlotProjection2D projection,
    std::vector<double> x, std::vector<double> y, std::vector<double> scalar,
    PlotLineSeriesStyle2D style) {
  if (!scene.valid() || !projection.matches_scene(scene)) {
    tc::Log::error("adopt_plot_line_series_item2d requires matching scene");
    return std::nullopt;
  }
  try {
    return scene.adopt(std::make_unique<PlotLineSeriesItem2D>(
        projection, std::move(x), std::move(y), std::move(scalar), style));
  } catch (const std::exception &error) {
    tc::Log::error("adopt_plot_line_series_item2d failed: %s", error.what());
    return std::nullopt;
  }
}

std::optional<termin::visual::GraphicItemHandle>
adopt_plot_scatter_series_item2d(termin::visual::TcVisualScene scene,
                                 PlotProjection2D projection,
                                 std::vector<double> x, std::vector<double> y,
                                 PlotScatterSeriesStyle2D style) {
  if (!scene.valid() || !projection.matches_scene(scene)) {
    tc::Log::error("adopt_plot_scatter_series_item2d requires matching scene");
    return std::nullopt;
  }
  try {
    return scene.adopt(std::make_unique<PlotScatterSeriesItem2D>(
        projection, std::move(x), std::move(y), style));
  } catch (const std::exception &error) {
    tc::Log::error("adopt_plot_scatter_series_item2d failed: %s", error.what());
    return std::nullopt;
  }
}

template <typename T>
T *resolve_item(termin::visual::TcVisualScene &scene,
                termin::visual::GraphicItemHandle handle, const char *type) {
  auto *item = scene.resolve(handle);
  if (!item || !item->body ||
      std::strcmp(tc_graphic_item_type_name(item), type) != 0) {
    return nullptr;
  }
  return static_cast<T *>(item->body);
}

template <typename T>
const T *resolve_item(const termin::visual::TcVisualScene &scene,
                      termin::visual::GraphicItemHandle handle,
                      const char *type) {
  const auto *item = scene.resolve(handle);
  if (!item || !item->body ||
      std::strcmp(tc_graphic_item_type_name(item), type) != 0) {
    return nullptr;
  }
  return static_cast<const T *>(item->body);
}

PlotLineSeriesItem2D *
resolve_plot_line_series_item2d(termin::visual::TcVisualScene &scene,
                                termin::visual::GraphicItemHandle handle) {
  return resolve_item<PlotLineSeriesItem2D>(scene, handle, kLineItemType);
}
const PlotLineSeriesItem2D *
resolve_plot_line_series_item2d(const termin::visual::TcVisualScene &scene,
                                termin::visual::GraphicItemHandle handle) {
  return resolve_item<PlotLineSeriesItem2D>(scene, handle, kLineItemType);
}
PlotScatterSeriesItem2D *
resolve_plot_scatter_series_item2d(termin::visual::TcVisualScene &scene,
                                   termin::visual::GraphicItemHandle handle) {
  return resolve_item<PlotScatterSeriesItem2D>(scene, handle, kScatterItemType);
}
const PlotScatterSeriesItem2D *
resolve_plot_scatter_series_item2d(const termin::visual::TcVisualScene &scene,
                                   termin::visual::GraphicItemHandle handle) {
  return resolve_item<PlotScatterSeriesItem2D>(scene, handle, kScatterItemType);
}

} // namespace tcplot

extern "C" {

tc_graphic_item_handle tc_plot_line_series_item2d_create(
    tc_visual_scene_handle owner_scene, tc_plot_projection_handle2d projection,
    const double *x, const double *y, const double *scalar, size_t point_count,
    tc_plot_line_style_state2d style) {
  if (point_count > 0 && (!x || !y)) {
    tc::Log::error("line series create received null coordinates");
    return tc_graphic_item_handle_invalid();
  }
  auto result = tcplot::adopt_plot_line_series_item2d(
      termin::visual::TcVisualScene{owner_scene},
      tcplot::PlotProjection2D{projection}, tcplot::copy_array(x, point_count),
      tcplot::copy_array(y, point_count),
      scalar ? tcplot::copy_array(scalar, point_count) : std::vector<double>{},
      tcplot::from_c(style));
  return result.value_or(tc_graphic_item_handle_invalid());
}

tc_graphic_item_handle tc_plot_scatter_series_item2d_create(
    tc_visual_scene_handle owner_scene, tc_plot_projection_handle2d projection,
    const double *x, const double *y, size_t point_count,
    tc_plot_scatter_style_state2d style) {
  if (point_count > 0 && (!x || !y)) {
    tc::Log::error("scatter series create received null coordinates");
    return tc_graphic_item_handle_invalid();
  }
  auto result = tcplot::adopt_plot_scatter_series_item2d(
      termin::visual::TcVisualScene{owner_scene},
      tcplot::PlotProjection2D{projection}, tcplot::copy_array(x, point_count),
      tcplot::copy_array(y, point_count), tcplot::from_c(style));
  return result.value_or(tc_graphic_item_handle_invalid());
}

#define TCPLOT_RESOLVE_MUTABLE(function_name, resolver, variable)              \
  termin::visual::TcVisualScene scene{owner_scene};                            \
  auto *variable = tcplot::resolver(scene, item);                              \
  if (!(variable)) {                                                           \
    tc::Log::error(function_name " received stale or wrong item");             \
    return false;                                                              \
  }

bool tc_plot_line_series_item2d_set_projection(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_projection_handle2d projection) {
  TCPLOT_RESOLVE_MUTABLE("line set_projection", resolve_plot_line_series_item2d,
                         series);
  return series->set_projection(tcplot::PlotProjection2D{projection});
}

bool tc_plot_scatter_series_item2d_set_projection(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_projection_handle2d projection) {
  TCPLOT_RESOLVE_MUTABLE("scatter set_projection",
                         resolve_plot_scatter_series_item2d, series);
  return series->set_projection(tcplot::PlotProjection2D{projection});
}

bool tc_plot_line_series_item2d_set_data(tc_visual_scene_handle owner_scene,
                                         tc_graphic_item_handle item,
                                         const double *x, const double *y,
                                         const double *scalar,
                                         size_t point_count) {
  if (point_count > 0 && (!x || !y))
    return false;
  TCPLOT_RESOLVE_MUTABLE("line set_data", resolve_plot_line_series_item2d,
                         series);
  return series->set_data(
      tcplot::copy_array(x, point_count), tcplot::copy_array(y, point_count),
      scalar ? tcplot::copy_array(scalar, point_count) : std::vector<double>{});
}

bool tc_plot_line_series_item2d_append(tc_visual_scene_handle owner_scene,
                                       tc_graphic_item_handle item,
                                       const double *x, const double *y,
                                       const double *scalar,
                                       size_t point_count) {
  if (point_count > 0 && (!x || !y))
    return false;
  TCPLOT_RESOLVE_MUTABLE("line append", resolve_plot_line_series_item2d,
                         series);
  return series->append(std::span<const double>{x, point_count},
                        std::span<const double>{y, point_count},
                        scalar ? std::span<const double>{scalar, point_count}
                               : std::span<const double>{});
}

bool tc_plot_scatter_series_item2d_set_data(tc_visual_scene_handle owner_scene,
                                            tc_graphic_item_handle item,
                                            const double *x, const double *y,
                                            size_t point_count) {
  if (point_count > 0 && (!x || !y))
    return false;
  TCPLOT_RESOLVE_MUTABLE("scatter set_data", resolve_plot_scatter_series_item2d,
                         series);
  return series->set_data(tcplot::copy_array(x, point_count),
                          tcplot::copy_array(y, point_count));
}

bool tc_plot_line_series_item2d_set_style(tc_visual_scene_handle owner_scene,
                                          tc_graphic_item_handle item,
                                          tc_plot_line_style_state2d style) {
  TCPLOT_RESOLVE_MUTABLE("line set_style", resolve_plot_line_series_item2d,
                         series);
  return series->set_style(tcplot::from_c(style));
}

bool tc_plot_scatter_series_item2d_set_style(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_scatter_style_state2d style) {
  TCPLOT_RESOLVE_MUTABLE("scatter set_style",
                         resolve_plot_scatter_series_item2d, series);
  return series->set_style(tcplot::from_c(style));
}

#undef TCPLOT_RESOLVE_MUTABLE

bool tc_plot_line_series_item2d_snapshot(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_series_snapshot2d *out_snapshot,
    tc_plot_line_style_state2d *out_style) {
  if (!out_snapshot || !out_style)
    return false;
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_line_series_item2d(scene, item);
  if (!series)
    return false;
  *out_snapshot = {series->projection().handle(), series->x().size(),
                   !series->scalar().empty(), series->revision()};
  *out_style = tcplot::to_c(series->style());
  return true;
}

bool tc_plot_scatter_series_item2d_snapshot(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    tc_plot_series_snapshot2d *out_snapshot,
    tc_plot_scatter_style_state2d *out_style) {
  if (!out_snapshot || !out_style)
    return false;
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_scatter_series_item2d(scene, item);
  if (!series)
    return false;
  *out_snapshot = {series->projection().handle(), series->x().size(), false,
                   series->revision()};
  *out_style = tcplot::to_c(series->style());
  return true;
}

size_t tc_plot_line_series_item2d_copy_data(tc_visual_scene_handle owner_scene,
                                            tc_graphic_item_handle item,
                                            double *out_x, double *out_y,
                                            double *out_scalar,
                                            size_t capacity) {
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_line_series_item2d(scene, item);
  if (!series)
    return 0;
  const size_t count = series->x().size();
  if (!out_x && !out_y && !out_scalar)
    return count;
  if (capacity < count || !out_x || !out_y ||
      (!series->scalar().empty() && !out_scalar)) {
    tc::Log::error("line copy_data output is too small");
    return 0;
  }
  std::copy(series->x().begin(), series->x().end(), out_x);
  std::copy(series->y().begin(), series->y().end(), out_y);
  if (!series->scalar().empty()) {
    std::copy(series->scalar().begin(), series->scalar().end(), out_scalar);
  }
  return count;
}

size_t tc_plot_scatter_series_item2d_copy_data(
    tc_visual_scene_handle owner_scene, tc_graphic_item_handle item,
    double *out_x, double *out_y, size_t capacity) {
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_scatter_series_item2d(scene, item);
  if (!series)
    return 0;
  const size_t count = series->x().size();
  if (!out_x && !out_y)
    return count;
  if (capacity < count || !out_x || !out_y)
    return 0;
  std::copy(series->x().begin(), series->x().end(), out_x);
  std::copy(series->y().begin(), series->y().end(), out_y);
  return count;
}

bool tc_plot_line_series_item2d_nearest(tc_visual_scene_handle owner_scene,
                                        tc_graphic_item_handle item,
                                        float pixel_x, float pixel_y,
                                        float max_distance_px,
                                        tc_plot_nearest_point2d *out_point) {
  if (!out_point)
    return false;
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_line_series_item2d(scene, item);
  if (!series)
    return false;
  const auto nearest = series->nearest({pixel_x, pixel_y}, max_distance_px);
  if (!nearest)
    return false;
  *out_point = {nearest->index,   nearest->data_x,  nearest->data_y,
                nearest->pixel.x, nearest->pixel.y, nearest->distance_px};
  return true;
}

bool tc_plot_scatter_series_item2d_nearest(tc_visual_scene_handle owner_scene,
                                           tc_graphic_item_handle item,
                                           float pixel_x, float pixel_y,
                                           float max_distance_px,
                                           tc_plot_nearest_point2d *out_point) {
  if (!out_point)
    return false;
  const termin::visual::TcVisualScene scene{owner_scene};
  const auto *series = tcplot::resolve_plot_scatter_series_item2d(scene, item);
  if (!series)
    return false;
  const auto nearest = series->nearest({pixel_x, pixel_y}, max_distance_px);
  if (!nearest)
    return false;
  *out_point = {nearest->index,   nearest->data_x,  nearest->data_y,
                nearest->pixel.x, nearest->pixel.y, nearest->distance_px};
  return true;
}

} // extern "C"
