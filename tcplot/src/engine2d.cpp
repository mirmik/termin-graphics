// engine2d.cpp - 2D plot engine. Port of engine2d.py.
//
// Architectural change vs Python: we draw every line segment of a
// series into ONE flat vertex buffer and issue a single
// draw_immediate_lines(). The Python path issued one draw per segment
// which made 400-point sines visibly lag during pan. All other
// coordinate / bounds / pick math stays 1:1.

#include "tcplot/engine2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/axes.hpp"

namespace tcplot {

namespace {

// Shared push-constant layout: one 4x4 matrix + one vec4 colour.
// Covers both shader pairs — rects/grid/series and persistent-VBO
// lines — so the pipeline-layout stays uniform across the engine.
struct Plot2DPushData {
    float matrix[16];   // column-major (sent as-is)
    float color[4];
};
static_assert(sizeof(Plot2DPushData) == 80,
              "Plot2DPushData layout drift — shader + C++ disagree");

// Shared push-constant block. On Vulkan the shader compiler defines
// `VULKAN=1`; on OpenGL we fall back to a std140 UBO at binding 14,
// which is the slot tgfx2 uses for its push-constants ring buffer.
constexpr const char* kPC = R"(
struct Plot2DPC { mat4 u_matrix; vec4 u_color; };
#ifdef VULKAN
layout(push_constant) uniform PCBlock { Plot2DPC pc; };
#else
layout(std140, binding = 14) uniform PCBlock { Plot2DPC pc; };
#endif
)";

// --- Persistent-VBO line series shader ---
//
// Vertex input is a raw data-space vec2. pc.u_matrix converts directly
// to clip space; panning/zooming only changes this push-constant, the
// VBO never needs re-upload. Colour is per-series via pc.u_color.
static std::string make_line_vert() {
    return std::string("#version 450 core\n") + kPC + R"(
layout(location=0) in vec2 a_data_pos;
void main() {
    gl_Position = pc.u_matrix * vec4(a_data_pos, 0.0, 1.0);
}
)";
}

static std::string make_line_frag() {
    return std::string("#version 450 core\n") + kPC + R"(
layout(location=0) out vec4 frag_color;
void main() { frag_color = pc.u_color; }
)";
}

struct StyledLinePushData {
    float matrix[16];
    float color[4];
    float params[4];    // thickness_px, style_id, colormap_id, colormap_reversed
    float range[4];     // scalar_min, scalar_max, length_scale, unused
    float viewport[4];  // width, height, dash_px, gap_px
};
static_assert(sizeof(StyledLinePushData) == 128,
              "StyledLinePushData must fit tgfx2 push constants");

static std::string make_styled_line_vert() {
    return std::string("#version 450 core\n") + R"(
struct StyledLinePC {
    mat4 u_matrix;
    vec4 u_color;
    vec4 u_params;
    vec4 u_range;
    vec4 u_viewport;
};
#ifdef VULKAN
layout(push_constant) uniform PCBlock { StyledLinePC pc; };
#else
layout(std140, binding = 14) uniform PCBlock { StyledLinePC pc; };
#endif

layout(location=0) in vec2 a_prev;
layout(location=1) in vec2 a_curr;
layout(location=2) in vec2 a_next;
layout(location=3) in vec4 a_meta; // side, scalar, cumulative_length, unused

layout(location=0) out vec4 v_color;
layout(location=1) out vec4 v_meta; // cumulative_length, style_id, dash_px, gap_px

vec2 clip_to_px(vec4 c) {
    vec2 ndc = c.xy / c.w;
    return (ndc * 0.5 + 0.5) * pc.u_viewport.xy;
}

void main() {
    vec4 cp = pc.u_matrix * vec4(a_prev, 0.0, 1.0);
    vec4 cc = pc.u_matrix * vec4(a_curr, 0.0, 1.0);
    vec4 cn = pc.u_matrix * vec4(a_next, 0.0, 1.0);

    vec2 pp = clip_to_px(cp);
    vec2 pcurr = clip_to_px(cc);
    vec2 pn = clip_to_px(cn);
    vec2 dir = pn - pp;
    float len = length(dir);
    if (len < 0.001) dir = vec2(1.0, 0.0);
    else dir /= len;
    vec2 normal = vec2(-dir.y, dir.x);

    vec2 px = pcurr + normal * a_meta.x * pc.u_params.x * 0.5;
    vec2 ndc = px / pc.u_viewport.xy * 2.0 - 1.0;
    gl_Position = vec4(ndc * cc.w, cc.z, cc.w);

    v_color = pc.u_color;
    v_meta = vec4(a_meta.z * pc.u_range.z, pc.u_params.y,
                  pc.u_viewport.z, pc.u_viewport.w);
    v_meta.x = max(v_meta.x, 0.0);
    v_color.a *= pc.u_params.w;
    v_color.r = a_meta.y;
}
)";
}

static std::string make_styled_line_frag() {
    return std::string("#version 450 core\n") + R"(
struct StyledLinePC {
    mat4 u_matrix;
    vec4 u_color;
    vec4 u_params;
    vec4 u_range;
    vec4 u_viewport;
};
#ifdef VULKAN
layout(push_constant) uniform PCBlock { StyledLinePC pc; };
#else
layout(std140, binding = 14) uniform PCBlock { StyledLinePC pc; };
#endif

layout(location=0) in vec4 v_color;
layout(location=1) in vec4 v_meta;
layout(location=0) out vec4 frag_color;

vec3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    float r = clamp(1.5 - abs(4.0 * t - 3.0), 0.0, 1.0);
    float g = clamp(1.5 - abs(4.0 * t - 2.0), 0.0, 1.0);
    float b = clamp(1.5 - abs(4.0 * t - 1.0), 0.0, 1.0);
    return vec3(r, g, b);
}
vec3 viridis(float t) {
    vec3 c0 = vec3(0.277, 0.005, 0.334);
    vec3 c1 = vec3(0.128, 0.567, 0.551);
    vec3 c2 = vec3(0.741, 0.873, 0.150);
    return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
}
vec3 plasma(float t) {
    vec3 c0 = vec3(0.050, 0.030, 0.528);
    vec3 c1 = vec3(0.798, 0.280, 0.470);
    vec3 c2 = vec3(0.940, 0.975, 0.131);
    return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
}
vec3 colormap(float id, float t) {
    if (id < 0.5) return jet(t);
    if (id < 1.5) return viridis(t);
    if (id < 2.5) return plasma(t);
    if (id < 3.5) return vec3(t);
    if (id < 4.5) return vec3(t, 0.25 + 0.5 * (1.0 - abs(2.0 * t - 1.0)), 1.0 - t);
    return v_color.rgb;
}

void main() {
    float style_id = v_meta.y;
    if (style_id > 0.5) {
        float dash_px = max(v_meta.z, 1.0);
        float gap_px = max(v_meta.w, 1.0);
        float period = dash_px + gap_px;
        float p = mod(v_meta.x, period);
        if (p > dash_px) discard;
        if (style_id > 1.5 && p > min(dash_px, pc.u_params.x)) discard;
    }

    vec4 c = pc.u_color;
    if (pc.u_params.z < 5.5) {
        float denom = max(pc.u_range.y - pc.u_range.x, 1e-12);
        float t = clamp((v_color.r - pc.u_range.x) / denom, 0.0, 1.0);
        if (pc.u_params.w != 0.0) t = 1.0 - t;
        c.rgb = colormap(pc.u_params.z, t);
    }
    frag_color = c;
}
)";
}

tgfx::CanvasColor canvas_color(const Color4& c) {
    return tgfx::CanvasColor{c.r, c.g, c.b, c.a};
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PlotEngine2D::PlotEngine2D()
    : canvas_(std::make_unique<tgfx::Canvas2DRenderer>()) {}

PlotEngine2D::~PlotEngine2D() {
    release_gpu_resources();
}

void PlotEngine2D::set_viewport(float x, float y, float width, float height) {
    vx_ = x;
    vy_ = y;
    vw_ = width;
    vh_ = height;
}

void PlotEngine2D::set_fbo_height(float h) {
    fbo_height_ = (h > 0.0f) ? h : 0.0f;
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

void PlotEngine2D::plot(std::vector<double> x, std::vector<double> y,
                         std::optional<Color4> color,
                         double thickness,
                         std::string label) {
    data.add_line(std::move(x), std::move(y), {}, color, thickness, std::move(label));
    ++data_version_;
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::plot_colormap(std::vector<double> x,
                                  std::vector<double> y,
                                  std::vector<double> scalar,
                                  SurfaceColorMap colormap,
                                  double scalar_min,
                                  double scalar_max,
                                  double thickness,
                                  std::string label,
                                  bool colormap_reversed) {
    LineSeries& s = data.add_line(std::move(x), std::move(y), {}, std::nullopt,
                                  thickness, std::move(label));
    s.scalar = std::move(scalar);
    s.colormap = colormap;
    s.colormap_reversed = colormap_reversed;
    s.scalar_min = scalar_min;
    s.scalar_max = scalar_max;
    if (s.scalar_max <= s.scalar_min) {
        s.scalar_max = s.scalar_min + 1.0;
    }
    ++data_version_;
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::scatter(std::vector<double> x, std::vector<double> y,
                            std::optional<Color4> color,
                            double size,
                            std::string label) {
    data.add_scatter(std::move(x), std::move(y), {}, color, size, std::move(label));
    ++data_version_;
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::clear() {
    data = PlotData{};
    view_x_min_.reset();
    view_x_max_.reset();
    view_y_min_.reset();
    view_y_max_.reset();

    // Release each series' persistent VBO — data is gone, no point
    // keeping GPU storage around. The shader handles stay so the
    // next frame after a re-plot doesn't pay a shader-compile.
    if (line_shader_device_) {
        for (auto& gs : line_gpu_) {
            if (gs.vbo.id != 0) line_shader_device_->destroy(gs.vbo);
        }
    }
    line_gpu_.clear();
    if (styled_line_shader_device_) {
        for (auto& gs : styled_line_gpu_) {
            if (gs.vbo.id != 0) styled_line_shader_device_->destroy(gs.vbo);
        }
    }
    styled_line_gpu_.clear();
    ++data_version_;
}

void PlotEngine2D::fit() {
    const auto bounds = data.data_bounds_2d();
    const double x0 = bounds[0], x1 = bounds[1];
    const double y0 = bounds[2], y1 = bounds[3];
    const double dx = (x1 > x0) ? (x1 - x0) : 1.0;
    const double dy = (y1 > y0) ? (y1 - y0) : 1.0;
    constexpr double pad = 0.05;
    view_x_min_ = x0 - dx * pad;
    view_x_max_ = x1 + dx * pad;
    view_y_min_ = y0 - dy * pad;
    view_y_max_ = y1 + dy * pad;
}

void PlotEngine2D::set_view(double x_min, double x_max, double y_min, double y_max) {
    view_x_min_ = x_min;
    view_x_max_ = x_max;
    view_y_min_ = y_min;
    view_y_max_ = y_max;
}

// ---------------------------------------------------------------------------
// Coord helpers
// ---------------------------------------------------------------------------

PlotEngine2D::Rect PlotEngine2D::plot_area_() const {
    Rect r;
    r.x = vx_ + margin_left;
    r.y = vy_ + margin_top;
    r.w = std::max(vw_ - margin_left - margin_right, 1.0f);
    r.h = std::max(vh_ - margin_top - margin_bottom, 1.0f);
    return r;
}

PlotEngine2D::ViewRange PlotEngine2D::view_range_() {
    if (!view_x_min_.has_value()) fit();
    return {*view_x_min_, *view_x_max_, *view_y_min_, *view_y_max_};
}

void PlotEngine2D::data_to_pixel_(double dx, double dy,
                                    float& out_x, float& out_y) {
    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    const double sx = (v.x_max != v.x_min)
        ? (dx - v.x_min) / (v.x_max - v.x_min) : 0.5;
    const double sy = (v.y_max != v.y_min)
        ? (dy - v.y_min) / (v.y_max - v.y_min) : 0.5;
    out_x = pa.x + (float)sx * pa.w;
    out_y = pa.y + (1.0f - (float)sy) * pa.h;  // Y flipped (data y+up → pixel y+down)
}

void PlotEngine2D::pixel_to_data_(float wx, float wy,
                                    double& out_x, double& out_y) {
    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    const double sx = (wx - pa.x) / pa.w;
    const double sy = 1.0 - (wy - pa.y) / pa.h;
    out_x = v.x_min + sx * (v.x_max - v.x_min);
    out_y = v.y_min + sy * (v.y_max - v.y_min);
}

void PlotEngine2D::release_gpu_resources() {
    if (line_shader_device_) {
        if (line_shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = line_shader_vs_id_;
            line_shader_device_->destroy(h);
        }
        if (line_shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = line_shader_fs_id_;
            line_shader_device_->destroy(h);
        }
        for (auto& gs : line_gpu_) {
            if (gs.vbo.id != 0) line_shader_device_->destroy(gs.vbo);
        }
    }
    if (styled_line_shader_device_) {
        if (styled_line_shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = styled_line_shader_vs_id_;
            styled_line_shader_device_->destroy(h);
        }
        if (styled_line_shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = styled_line_shader_fs_id_;
            styled_line_shader_device_->destroy(h);
        }
        for (auto& gs : styled_line_gpu_) {
            if (gs.vbo.id != 0) styled_line_shader_device_->destroy(gs.vbo);
        }
    }
    line_shader_vs_id_ = 0;
    line_shader_fs_id_ = 0;
    line_shader_device_ = nullptr;
    line_gpu_.clear();
    styled_line_shader_vs_id_ = 0;
    styled_line_shader_fs_id_ = 0;
    styled_line_shader_device_ = nullptr;
    styled_line_gpu_.clear();

    if (canvas_) canvas_->release_gpu();
}

// ---------------------------------------------------------------------------
// Line persistent-VBO helpers
// ---------------------------------------------------------------------------

void PlotEngine2D::ensure_line_shader_(tgfx::IRenderDevice& device) {
    if (line_shader_device_ == &device
        && line_shader_vs_id_ != 0 && line_shader_fs_id_ != 0) {
        return;
    }
    if (line_shader_device_) {
        if (line_shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = line_shader_vs_id_;
            line_shader_device_->destroy(h);
        }
        if (line_shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = line_shader_fs_id_;
            line_shader_device_->destroy(h);
        }
        // Device change invalidates all VBOs too.
        for (auto& gs : line_gpu_) {
            if (gs.vbo.id != 0) line_shader_device_->destroy(gs.vbo);
            gs = LineGpuState{};
        }
    }

    tgfx::ShaderDesc vd;
    vd.stage = tgfx::ShaderStage::Vertex;
    vd.source = make_line_vert();
    line_shader_vs_id_ = device.create_shader(vd).id;

    tgfx::ShaderDesc fd;
    fd.stage = tgfx::ShaderStage::Fragment;
    fd.source = make_line_frag();
    line_shader_fs_id_ = device.create_shader(fd).id;

    line_shader_device_ = &device;
}

void PlotEngine2D::ensure_styled_line_shader_(tgfx::IRenderDevice& device) {
    if (styled_line_shader_device_ == &device
        && styled_line_shader_vs_id_ != 0 && styled_line_shader_fs_id_ != 0) {
        return;
    }
    if (styled_line_shader_device_) {
        if (styled_line_shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = styled_line_shader_vs_id_;
            styled_line_shader_device_->destroy(h);
        }
        if (styled_line_shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = styled_line_shader_fs_id_;
            styled_line_shader_device_->destroy(h);
        }
        for (auto& gs : styled_line_gpu_) {
            if (gs.vbo.id != 0) styled_line_shader_device_->destroy(gs.vbo);
            gs = StyledLineGpuState{};
        }
    }

    tgfx::ShaderDesc vd;
    vd.stage = tgfx::ShaderStage::Vertex;
    vd.source = make_styled_line_vert();
    styled_line_shader_vs_id_ = device.create_shader(vd).id;

    tgfx::ShaderDesc fd;
    fd.stage = tgfx::ShaderStage::Fragment;
    fd.source = make_styled_line_frag();
    styled_line_shader_fs_id_ = device.create_shader(fd).id;

    styled_line_shader_device_ = &device;
}

void PlotEngine2D::ensure_line_gpu_(tgfx::IRenderDevice& device, size_t idx) {
    if (idx >= data.lines.size()) return;
    LineGpuState& gs = line_gpu_[idx];
    const LineSeries& s = data.lines[idx];
    const uint32_t want = static_cast<uint32_t>(s.x.size());

    // Grow (or allocate) the VBO when capacity is insufficient. Double
    // the capacity each time — amortised O(1) append per point.
    if (want > gs.capacity) {
        uint32_t new_cap = gs.capacity ? gs.capacity * 2 : 256u;
        while (new_cap < want) new_cap *= 2;

        if (gs.vbo.id != 0) device.destroy(gs.vbo);

        tgfx::BufferDesc desc;
        desc.size = static_cast<uint64_t>(new_cap) * 2 * sizeof(float);
        desc.usage = tgfx::BufferUsage::Vertex
                   | tgfx::BufferUsage::CopyDst;
        gs.vbo = device.create_buffer(desc);
        gs.capacity = new_cap;
        gs.gpu_count = 0;  // force full re-upload into new buffer
    }

    if (gs.gpu_count >= want || gs.vbo.id == 0) return;

    // Convert tail [gpu_count..want) from double pairs to float pairs
    // and upload to the VBO at the right byte offset.
    const uint32_t n_new = want - gs.gpu_count;
    std::vector<float> tail;
    tail.reserve(static_cast<size_t>(n_new) * 2);
    for (uint32_t i = gs.gpu_count; i < want; ++i) {
        tail.push_back(static_cast<float>(s.x[i]));
        tail.push_back(static_cast<float>(s.y[i]));
    }
    const uint64_t byte_offset =
        static_cast<uint64_t>(gs.gpu_count) * 2 * sizeof(float);

    std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(tail.data()),
        tail.size() * sizeof(float));
    device.upload_buffer(gs.vbo, bytes, byte_offset);
    gs.gpu_count = want;
}

void PlotEngine2D::ensure_styled_line_gpu_(tgfx::IRenderDevice& device, size_t idx) {
    if (idx >= data.lines.size()) return;
    styled_line_gpu_.resize(data.lines.size());
    StyledLineGpuState& gs = styled_line_gpu_[idx];
    const LineSeries& s = data.lines[idx];
    const uint32_t point_count = static_cast<uint32_t>(s.x.size());
    const uint32_t want = point_count * 2u;
    if (point_count < 2) return;
    if (gs.data_version == data_version_ && gs.gpu_count == want && gs.vbo.id != 0) {
        return;
    }

    if (want > gs.capacity) {
        uint32_t new_cap = gs.capacity ? gs.capacity * 2 : 512u;
        while (new_cap < want) new_cap *= 2;
        if (gs.vbo.id != 0) device.destroy(gs.vbo);

        tgfx::BufferDesc desc;
        desc.size = static_cast<uint64_t>(new_cap) * 10 * sizeof(float);
        desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
        gs.vbo = device.create_buffer(desc);
        gs.capacity = new_cap;
    }

    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(want) * 10);
    double cumulative = 0.0;
    for (uint32_t i = 0; i < point_count; ++i) {
        if (i > 0) {
            const double dx = s.x[i] - s.x[i - 1];
            const double dy = s.y[i] - s.y[i - 1];
            cumulative += std::sqrt(dx * dx + dy * dy);
        }

        const uint32_t ip = (i == 0) ? 0 : i - 1;
        const uint32_t in = (i + 1 < point_count) ? i + 1 : i;
        const float scalar = (!s.scalar.empty() && i < s.scalar.size())
            ? static_cast<float>(s.scalar[i])
            : 0.0f;
        const float sides[2] = {-1.0f, 1.0f};
        for (float side : sides) {
            verts.push_back(static_cast<float>(s.x[ip]));
            verts.push_back(static_cast<float>(s.y[ip]));
            verts.push_back(static_cast<float>(s.x[i]));
            verts.push_back(static_cast<float>(s.y[i]));
            verts.push_back(static_cast<float>(s.x[in]));
            verts.push_back(static_cast<float>(s.y[in]));
            verts.push_back(side);
            verts.push_back(scalar);
            verts.push_back(static_cast<float>(cumulative));
            verts.push_back(0.0f);
        }
    }

    std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(verts.data()),
        verts.size() * sizeof(float));
    device.upload_buffer(gs.vbo, bytes, 0);
    gs.gpu_count = want;
    gs.data_version = data_version_;
}

void PlotEngine2D::compute_data_to_clip_(float out16[16]) {
    // Linear transform:
    //   x_clip = S_x * x_data + C_x
    //   y_clip = S_y * y_data + C_y
    // See engine2d.hpp for the full derivation. We also normalise
    // pixel coords against the CURRENT viewport rect (vw_, vh_), so
    // the transform works regardless of how the host positioned the
    // engine within a bigger render target.
    std::memset(out16, 0, sizeof(float) * 16);
    out16[10] = 1.0f;  // pass Z through
    out16[15] = 1.0f;

    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    const double span_x = v.x_max - v.x_min;
    const double span_y = v.y_max - v.y_min;

    const double vw = std::max(vw_, 1.0f);
    const double vh = std::max(vh_, 1.0f);

    // Pixel-coord of plot area relative to viewport origin.
    const double pa_x_local = pa.x - vx_;
    const double pa_y_local = pa.y - vy_;

    // Derivation: fx = 2/vw * (pa_x + (x - x_min)/span_x * pa_w) - 1.
    const double S_x = (span_x != 0.0)
        ? 2.0 * pa.w / vw / span_x : 0.0;
    const double C_x = 2.0 * pa_x_local / vw - 1.0 - S_x * v.x_min;

    // Y-down clip space: data y+up should map to clip y-down (top of
    // plot = y_max, bottom = y_min). Derivation:
    //   pixel_y  = pa_y + (y_max - y) / span_y * pa.h
    //   fy       = 2 * pixel_y / vh - 1
    //            = (-2*pa.h / vh / span_y) * y
    //            + (-1 + 2*pa_y/vh + 2*pa.h*y_max / span_y / vh)
    // Rewriting the constant in terms of y_min = y_max - span_y so we
    // stay symmetric with the X derivation above.
    const double S_y = (span_y != 0.0)
        ? -2.0 * pa.h / vh / span_y : 0.0;
    const double C_y = -1.0 + 2.0 * pa_y_local / vh + 2.0 * pa.h / vh
                     - S_y * v.y_min;

    // Column-major 4x4 storage: out16[col*4 + row].
    // (col 0, row 0) = S_x; (col 3, row 0) = C_x.
    out16[0 * 4 + 0] = static_cast<float>(S_x);
    out16[3 * 4 + 0] = static_cast<float>(C_x);
    out16[1 * 4 + 1] = static_cast<float>(S_y);
    out16[3 * 4 + 1] = static_cast<float>(C_y);
}

// ---------------------------------------------------------------------------
// Time-series / streaming API
// ---------------------------------------------------------------------------

void PlotEngine2D::append_to_line(size_t idx,
                                    const double* x, const double* y,
                                    size_t n) {
    if (idx >= data.lines.size() || n == 0 || !x || !y) return;
    LineSeries& s = data.lines[idx];
    s.x.reserve(s.x.size() + n);
    s.y.reserve(s.y.size() + n);
    for (size_t i = 0; i < n; ++i) {
        s.x.push_back(x[i]);
        s.y.push_back(y[i]);
    }
    ++data_version_;
    // The VBO is topped up with the tail on the next render() via
    // ensure_line_gpu_ — no GPU work happens here.
}

bool PlotEngine2D::last_x_of_line(size_t idx, double& out_x) const {
    if (idx >= data.lines.size()) return false;
    const LineSeries& s = data.lines[idx];
    if (s.x.empty()) return false;
    out_x = s.x.back();
    return true;
}

bool PlotEngine2D::set_line_color(size_t idx, Color4 color) {
    if (idx >= data.lines.size()) return false;
    data.lines[idx].color = color;
    ++data_version_;
    return true;
}

bool PlotEngine2D::set_scatter_color(size_t idx, Color4 color) {
    if (idx >= data.scatters.size()) return false;
    data.scatters[idx].color = color;
    ++data_version_;
    return true;
}

bool PlotEngine2D::set_line_style(size_t idx, LineStyle style,
                                  float dash_px, float gap_px) {
    if (idx >= data.lines.size()) return false;
    LineSeries& s = data.lines[idx];
    s.line_style = style;
    s.dash_px = std::max(1.0f, dash_px);
    s.gap_px = std::max(1.0f, gap_px);
    ++data_version_;
    return true;
}

bool PlotEngine2D::set_line_colormap_reversed(size_t idx, bool reversed) {
    if (idx >= data.lines.size()) return false;
    data.lines[idx].colormap_reversed = reversed;
    ++data_version_;
    return true;
}

void PlotEngine2D::get_view(double& x_min, double& x_max,
                              double& y_min, double& y_max) {
    const ViewRange v = view_range_();
    x_min = v.x_min; x_max = v.x_max;
    y_min = v.y_min; y_max = v.y_max;
}

void PlotEngine2D::set_view_x(double x_min, double x_max) {
    view_x_min_ = x_min;
    view_x_max_ = x_max;
    // Y preserved as-is (auto-fitted on first use if still unset).
}

void PlotEngine2D::set_view_y(double y_min, double y_max) {
    view_y_min_ = y_min;
    view_y_max_ = y_max;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void PlotEngine2D::render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font) {
    if (!ctx || vw_ <= 0 || vh_ <= 0) return;

    const int canvas_w = static_cast<int>(std::ceil(std::max(vw_, vx_ + vw_)));
    const float target_h = fbo_height_ > 0.0f ? fbo_height_ : (vy_ + vh_);
    const int canvas_h = static_cast<int>(std::ceil(std::max(vh_, target_h)));

    canvas_->set_default_font(font);
    canvas_->begin(*ctx, canvas_w, canvas_h);

    const Rect pa = plot_area_();
    const ViewRange v = view_range_();
    canvas_->draw_rect(vx_, vy_, vw_, vh_, canvas_color(bg_color));
    canvas_->draw_rect(pa.x, pa.y, pa.w, pa.h, canvas_color(plot_bg_color));

    const int max_x_ticks = std::max(int(pa.w / 80.0f), 3);
    const int max_y_ticks = std::max(int(pa.h / 50.0f), 3);
    const std::vector<double> x_ticks = axes::nice_ticks(v.x_min, v.x_max, max_x_ticks);
    const std::vector<double> y_ticks = axes::nice_ticks(v.y_min, v.y_max, max_y_ticks);

    canvas_->begin_clip(pa.x, pa.y, pa.w, pa.h);
    if (show_grid) {
        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            canvas_->draw_line(sx, pa.y, sx, pa.y + pa.h,
                               canvas_color(grid_color));
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            canvas_->draw_line(pa.x, sy, pa.x + pa.w, sy,
                               canvas_color(grid_color));
        }
    }
    canvas_->end();

    // Batch #3 — line series via persistent VBOs + VS-side data→clip.
    // Each series' points live in its own GPU buffer; append_to_line
    // only uploads the new tail. Pan/zoom is a uniform change, no
    // vertex re-upload. Scaling to time-series with 100k+ points is
    // trivial here — data never returns to the CPU per-frame.
    {
        ctx->set_viewport(static_cast<int>(vx_), static_cast<int>(vy_),
                          static_cast<int>(vw_), static_cast<int>(vh_));
        ctx->set_scissor(static_cast<int>(pa.x), static_cast<int>(pa.y),
                         static_cast<int>(pa.w), static_cast<int>(pa.h));
        ctx->set_depth_test(false);
        ctx->set_blend(true);
        ctx->set_cull(tgfx::CullMode::None);

        ensure_line_shader_(ctx->device());
        line_gpu_.resize(data.lines.size());

        tgfx::ShaderHandle lvs; lvs.id = line_shader_vs_id_;
        tgfx::ShaderHandle lfs; lfs.id = line_shader_fs_id_;
        ctx->bind_shader(lvs, lfs);

        Plot2DPushData pc_line{};
        compute_data_to_clip_(pc_line.matrix);  // already column-major

        tgfx::VertexBufferLayout line_layout;
        line_layout.stride = 2 * sizeof(float);
        {
            tgfx::VertexAttribute a;
            a.location = 0;
            a.format = tgfx::VertexFormat::Float2;
            a.offset = 0;
            line_layout.attributes.push_back(a);
        }
        ctx->set_vertex_layout(line_layout);
        ctx->set_topology(tgfx::PrimitiveTopology::LineStrip);

        for (size_t i = 0; i < data.lines.size(); ++i) {
            const LineSeries& s = data.lines[i];
            if (s.x.size() < 2) continue;
            if (!s.scalar.empty() || s.line_style != LineStyle::Solid) continue;

            ensure_line_gpu_(ctx->device(), i);
            const LineGpuState& gs = line_gpu_[i];
            if (gs.gpu_count < 2 || gs.vbo.id == 0) continue;

            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color((uint32_t)i);
            pc_line.color[0] = c.r;
            pc_line.color[1] = c.g;
            pc_line.color[2] = c.b;
            pc_line.color[3] = c.a;
            ctx->set_push_constants(&pc_line,
                                    static_cast<uint32_t>(sizeof(pc_line)));

            ctx->draw_arrays(gs.vbo, gs.gpu_count);
        }
    }

    // Styled/colormapped lines use a persistent ribbon VBO. The vertex shader
    // expands the centerline to pixel-width quads; the fragment shader handles
    // dash and colormap. This keeps large routes to one draw call per series.
    {
        ctx->set_viewport(static_cast<int>(vx_), static_cast<int>(vy_),
                          static_cast<int>(vw_), static_cast<int>(vh_));
        ctx->set_scissor(static_cast<int>(pa.x), static_cast<int>(pa.y),
                         static_cast<int>(pa.w), static_cast<int>(pa.h));
        ctx->set_depth_test(false);
        ctx->set_blend(true);
        ctx->set_blend_func(tgfx::BlendFactor::SrcAlpha,
                            tgfx::BlendFactor::OneMinusSrcAlpha);
        ctx->set_cull(tgfx::CullMode::None);

        ensure_styled_line_shader_(ctx->device());

        tgfx::ShaderHandle slvs; slvs.id = styled_line_shader_vs_id_;
        tgfx::ShaderHandle slfs; slfs.id = styled_line_shader_fs_id_;
        ctx->bind_shader(slvs, slfs);

        tgfx::VertexBufferLayout layout;
        layout.stride = 10 * sizeof(float);
        layout.attributes.push_back({0, tgfx::VertexFormat::Float2, 0});
        layout.attributes.push_back({1, tgfx::VertexFormat::Float2, 2 * sizeof(float)});
        layout.attributes.push_back({2, tgfx::VertexFormat::Float2, 4 * sizeof(float)});
        layout.attributes.push_back({3, tgfx::VertexFormat::Float4, 6 * sizeof(float)});
        ctx->set_vertex_layout(layout);
        ctx->set_topology(tgfx::PrimitiveTopology::TriangleStrip);

        StyledLinePushData pc{};
        compute_data_to_clip_(pc.matrix);
        const double span_x = std::max(v.x_max - v.x_min, 1e-12);
        const double span_y = std::max(v.y_max - v.y_min, 1e-12);
        const float sx_px = static_cast<float>(pa.w / span_x);
        const float sy_px = static_cast<float>(pa.h / span_y);
        const float length_scale = std::sqrt((sx_px * sx_px + sy_px * sy_px) * 0.5f);

        for (size_t i = 0; i < data.lines.size(); ++i) {
            const LineSeries& s = data.lines[i];
            if (s.x.size() < 2) continue;
            if (s.scalar.empty() && s.line_style == LineStyle::Solid) continue;

            ensure_styled_line_gpu_(ctx->device(), i);
            const StyledLineGpuState& gs = styled_line_gpu_[i];
            if (gs.gpu_count < 4 || gs.vbo.id == 0) continue;

            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color(static_cast<uint32_t>(i));
            pc.color[0] = c.r;
            pc.color[1] = c.g;
            pc.color[2] = c.b;
            pc.color[3] = c.a;
            pc.params[0] = static_cast<float>(std::max(1.0, s.thickness));
            pc.params[1] = static_cast<float>(s.line_style);
            pc.params[2] = s.scalar.empty() ? 6.0f : static_cast<float>(s.colormap);
            pc.params[3] = s.colormap_reversed ? 1.0f : 0.0f;
            pc.range[0] = static_cast<float>(s.scalar_min);
            pc.range[1] = static_cast<float>(s.scalar_max);
            pc.range[2] = length_scale;
            pc.viewport[0] = std::max(vw_, 1.0f);
            pc.viewport[1] = std::max(vh_, 1.0f);
            pc.viewport[2] = s.line_style == LineStyle::Dot
                ? pc.params[0]
                : std::max(1.0f, s.dash_px);
            pc.viewport[3] = std::max(1.0f, s.gap_px);
            ctx->set_push_constants(&pc, static_cast<uint32_t>(sizeof(pc)));
            ctx->draw_arrays(gs.vbo, gs.gpu_count);
        }
    }

    canvas_->begin(*ctx, canvas_w, canvas_h);
    canvas_->begin_clip(pa.x, pa.y, pa.w, pa.h);

    // Scatter uses Canvas2DRenderer; line series stay on the
    // persistent-VBO path above.
    {
        uint32_t palette_i = (uint32_t)data.lines.size();
        for (const auto& s : data.scatters) {
            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color(palette_i);
            const float half = (float)s.size * 0.5f;
            for (size_t i = 0; i < s.x.size(); i++) {
                float sx, sy;
                data_to_pixel_(s.x[i], s.y[i], sx, sy);
                canvas_->draw_circle(sx, sy, half, canvas_color(c));
            }
            palette_i++;
        }
    }

    canvas_->end_clip();
    canvas_->draw_rect_outline(pa.x, pa.y, pa.w, pa.h,
                               canvas_color(axis_color));

    // --- Text: tick labels, title, axis labels ---
    if (font) {
        // Ticks a notch smaller than axis labels — keeps the axis-label
        // / tick-label hierarchy visible even at similar sizes.
        const float tick_sz = font_size - 2.0f;

        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            canvas_->draw_text(axes::format_tick(tx),
                               sx, pa.y + pa.h + 14.0f,
                               tick_sz, canvas_color(label_color),
                               font, tgfx::Text2DRenderer::Anchor::Center);
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            const std::string lab = axes::format_tick(ty);
            const auto m = font->measure_text(lab, tick_sz);
            const float tw = m.width;
            canvas_->draw_text(lab,
                               pa.x - tw - 6.0f, sy + 4.0f,
                               tick_sz, canvas_color(label_color),
                               font, tgfx::Text2DRenderer::Anchor::Left);
        }

        if (!data.title.empty()) {
            // Title: left-anchored, bottom sitting just above the plot
            // area with a small gap. Earlier revisions placed it at
            // `margin_top * 0.5f` (vertical centre of the margin),
            // which left only `margin_top - title_font_size` px of
            // clearance — for Segoe UI's ~1.3× line-height ratio and
            // title_font_size=22 the title bottom landed ~6 px INSIDE
            // the plot area. Anchoring the bottom to pa.y instead
            // keeps the gap visually stable regardless of the font's
            // metric ratio.
            //
            // Colour: `title_color` if set, else `label_color`. Earlier
            // revisions auto-picked the first line series' colour —
            // removed in favour of an explicit override because the
            // implicit coupling made theme switching and per-panel
            // recolouring unpredictable.
            const Color4 tc = title_color.value_or(label_color);
            const int title_lh = font->line_height_px(title_font_size);
            // Clamp to viewport top so a title bigger than margin_top
            // gets cut off at the top instead of drawing at a negative
            // Y — caller's cue to bump margin_top.
            const float title_top_y = std::max(
                static_cast<float>(vy_),
                pa.y - static_cast<float>(title_lh) - title_pad);
            canvas_->draw_text(data.title,
                               pa.x, title_top_y,
                               title_font_size, canvas_color(tc),
                               font, tgfx::Text2DRenderer::Anchor::Left);
        }
        if (!data.x_label.empty()) {
            canvas_->draw_text(data.x_label,
                               pa.x + pa.w * 0.5f,
                               pa.y + pa.h + margin_bottom - 4.0f,
                               font_size, canvas_color(label_color),
                               font, tgfx::Text2DRenderer::Anchor::Center);
        }
        if (!data.y_label.empty()) {
            canvas_->draw_text(data.y_label,
                               vx_ + margin_left * 0.5f,
                               pa.y + pa.h * 0.5f,
                               font_size, canvas_color(label_color),
                               font, tgfx::Text2DRenderer::Anchor::Center);
        }
    }

    canvas_->end();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PlotEngine2D::on_mouse_down(float x, float y, tcbase::MouseButton button) {
    if (button == tcbase::MouseButton::MIDDLE) {
        panning_ = true;
        pan_start_mx_ = x;
        pan_start_my_ = y;
        const ViewRange v = view_range_();
        pan_start_view_[0] = v.x_min;
        pan_start_view_[1] = v.x_max;
        pan_start_view_[2] = v.y_min;
        pan_start_view_[3] = v.y_max;
        return true;
    }
    return false;
}

void PlotEngine2D::on_mouse_move(float x, float y) {
    if (!panning_) return;
    const Rect pa = plot_area_();
    const double vx0 = pan_start_view_[0];
    const double vx1 = pan_start_view_[1];
    const double vy0 = pan_start_view_[2];
    const double vy1 = pan_start_view_[3];
    const double dx_px = x - pan_start_mx_;
    const double dy_px = y - pan_start_my_;
    // Pan follows the cursor: the data point under the mouse stays
    // glued to it. pixel_x = pa_x + (x - x_min)/span_x * pa_w, so
    // dx_data = -dx_px/pa.w * span_x keeps the point fixed on X.
    // pixel_y = pa_y + (y_max - y)/span_y * pa_h grows downward as
    // data y shrinks — the opposite direction, so the Y sign is
    // positive here.
    const double dx_data = -dx_px / pa.w * (vx1 - vx0);
    const double dy_data =  dy_px / pa.h * (vy1 - vy0);
    view_x_min_ = vx0 + dx_data;
    view_x_max_ = vx1 + dx_data;
    view_y_min_ = vy0 + dy_data;
    view_y_max_ = vy1 + dy_data;
}

void PlotEngine2D::on_mouse_up(float /*x*/, float /*y*/,
                                 tcbase::MouseButton /*button*/) {
    panning_ = false;
}

bool PlotEngine2D::on_mouse_wheel_x(float x, float y, float dy) {
    const Rect pa = plot_area_();
    if (x < pa.x || x > pa.x + pa.w || y < pa.y || y > pa.y + pa.h) return false;

    const float factor = (dy > 0) ? 0.85f : 1.0f / 0.85f;

    double cx, cy;
    pixel_to_data_(x, y, cx, cy);
    const ViewRange v = view_range_();

    view_x_min_ = cx + (v.x_min - cx) * factor;
    view_x_max_ = cx + (v.x_max - cx) * factor;
    return true;
}

bool PlotEngine2D::on_mouse_wheel(float x, float y, float dy) {
    const Rect pa = plot_area_();
    if (x < pa.x || x > pa.x + pa.w || y < pa.y || y > pa.y + pa.h) return false;

    const float factor = (dy > 0) ? 0.85f : 1.0f / 0.85f;

    double cx, cy;
    pixel_to_data_(x, y, cx, cy);
    const ViewRange v = view_range_();

    view_x_min_ = cx + (v.x_min - cx) * factor;
    view_x_max_ = cx + (v.x_max - cx) * factor;
    view_y_min_ = cy + (v.y_min - cy) * factor;
    view_y_max_ = cy + (v.y_max - cy) * factor;
    return true;
}

}  // namespace tcplot
