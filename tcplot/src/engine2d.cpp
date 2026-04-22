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

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text2d_renderer.hpp>

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

// Pos + color shader used by rects, grid and series lines.
// Pixel→clip ortho projection matching the rest of termin-env:
// y+ down in pixel coords → y+ down in clip space (Vulkan-native).
// OpenGL reaches the same convention via glClipControl(UPPER_LEFT).
static std::string make_vert() {
    return std::string("#version 450 core\n") + kPC + R"(
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_color;
layout(location=0) out vec4 v_color;
void main() {
    gl_Position = pc.u_matrix * vec4(a_pos.xy, 0.0, 1.0);
    v_color = a_color;
}
)";
}

static std::string make_frag() {
    return std::string("#version 450 core\n") + R"(
layout(location=0) in vec4 v_color;
layout(location=0) out vec4 frag_color;
void main() { frag_color = v_color; }
)";
}

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

// Ortho pixel→clip (y+down everywhere). Pixel (0,0) → clip (-1,-1).
void build_ortho(float w, float h, float out[16]) {
    if (w <= 0.0f || h <= 0.0f) {
        std::memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    // Row-major here (transposed into column-major when we copy into
    // the push-constant struct below).
    const float m[16] = {
        2.0f / w,  0.0f,      0.0f, -1.0f,
        0.0f,      2.0f / h,  0.0f, -1.0f,
        0.0f,      0.0f,      1.0f,  0.0f,
        0.0f,      0.0f,      0.0f,  1.0f,
    };
    std::memcpy(out, m, sizeof(m));
}

// Copy row-major m[16] → column-major dst[16] (what shaders receive).
inline void transpose4x4(const float src[16], float dst[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            dst[c * 4 + r] = src[r * 4 + c];
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PlotEngine2D::PlotEngine2D()
    : text2d_(std::make_unique<tgfx::Text2DRenderer>()) {}

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
    if (!view_x_min_.has_value()) fit();
}

void PlotEngine2D::scatter(std::vector<double> x, std::vector<double> y,
                            std::optional<Color4> color,
                            double size,
                            std::string label) {
    data.add_scatter(std::move(x), std::move(y), {}, color, size, std::move(label));
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

// ---------------------------------------------------------------------------
// Shader lifecycle
// ---------------------------------------------------------------------------

void PlotEngine2D::ensure_shader_(tgfx::IRenderDevice& device) {
    if (shader_device_ == &device && shader_vs_id_ != 0) return;

    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }

    tgfx::ShaderDesc vd;
    vd.stage = tgfx::ShaderStage::Vertex;
    vd.source = make_vert();
    shader_vs_id_ = device.create_shader(vd).id;

    tgfx::ShaderDesc fd;
    fd.stage = tgfx::ShaderStage::Fragment;
    fd.source = make_frag();
    shader_fs_id_ = device.create_shader(fd).id;

    shader_device_ = &device;
}

void PlotEngine2D::release_gpu_resources() {
    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }
    shader_vs_id_ = 0;
    shader_fs_id_ = 0;
    shader_device_ = nullptr;

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
    line_shader_vs_id_ = 0;
    line_shader_fs_id_ = 0;
    line_shader_device_ = nullptr;
    line_gpu_.clear();

    if (text2d_) text2d_->release_gpu();
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
// Vertex emitters (scratch-buffer style)
// ---------------------------------------------------------------------------

void PlotEngine2D::emit_rect_tris_(std::vector<float>& v,
                                    float x, float y, float w, float h,
                                    const Color4& c) const {
    // 2 triangles. CCW in pixel y+down visual → after ortho y-flip →
    // CCW in NDC y+up (survives default CullMode::Back).
    // Triangle 1: TL, BL, BR   Triangle 2: TL, BR, TR
    auto push = [&](float px, float py) {
        v.push_back(px);  v.push_back(py);  v.push_back(0.0f);
        v.push_back(c.r); v.push_back(c.g); v.push_back(c.b); v.push_back(c.a);
    };
    const float x0 = x, x1 = x + w;
    const float y0 = y, y1 = y + h;
    push(x0, y0); push(x0, y1); push(x1, y1);
    push(x0, y0); push(x1, y1); push(x1, y0);
}

void PlotEngine2D::emit_rect_outline_lines_(std::vector<float>& v,
                                              float x, float y, float w, float h,
                                              const Color4& c) const {
    // 4 edges. thickness is 1px — no quad expansion.
    const float x0 = x, x1 = x + w;
    const float y0 = y, y1 = y + h;
    emit_line_(v, x0, y0, x1, y0, c);  // top
    emit_line_(v, x1, y0, x1, y1, c);  // right
    emit_line_(v, x1, y1, x0, y1, c);  // bottom
    emit_line_(v, x0, y1, x0, y0, c);  // left
}

void PlotEngine2D::emit_line_(std::vector<float>& v,
                                float x1, float y1, float x2, float y2,
                                const Color4& c) const {
    auto push = [&](float px, float py) {
        v.push_back(px);  v.push_back(py);  v.push_back(0.0f);
        v.push_back(c.r); v.push_back(c.g); v.push_back(c.b); v.push_back(c.a);
    };
    push(x1, y1);
    push(x2, y2);
}

void PlotEngine2D::flush_triangles_(tgfx::RenderContext2& ctx,
                                      std::vector<float>& v) {
    if (v.empty()) return;
    ctx.draw_immediate_triangles(v.data(), (uint32_t)(v.size() / 7));
    v.clear();
}

void PlotEngine2D::flush_lines_(tgfx::RenderContext2& ctx,
                                  std::vector<float>& v) {
    if (v.empty()) return;
    ctx.draw_immediate_lines(v.data(), (uint32_t)(v.size() / 7));
    v.clear();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void PlotEngine2D::render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font) {
    if (!ctx || vw_ <= 0 || vh_ <= 0) return;

    ensure_shader_(ctx->device());

    // Restrict the render target to our strip. Input y is top-left
    // origin (tcgui convention) and tgfx2's set_viewport / set_scissor
    // agree — no Y-flip, no fbo_height_ fallback needed anymore.
    ctx->set_viewport((int)vx_, (int)vy_, (int)vw_, (int)vh_);

    // Render state.
    ctx->set_depth_test(false);
    ctx->set_blend(true);
    ctx->set_cull(tgfx::CullMode::None);

    // Build an ortho that maps GLOBAL pixel coords (pa.x = vx_ +
    // margin_left, etc.) into the LOCAL viewport we just set. set_viewport
    // makes clip space cover pixels (0..vw_, 0..vh_) of the strip; the
    // ortho pre-subtracts (vx_, vy_) so callers can keep passing global
    // coords everywhere.
    Plot2DPushData pc_ui{};
    pc_ui.color[0] = pc_ui.color[1] = pc_ui.color[2] = pc_ui.color[3] = 0.0f;
    {
        float proj_rm[16];
        build_ortho((float)vw_, (float)vh_, proj_rm);
        const float tx = -vx_;
        const float ty = -vy_;
        // y-down ortho: proj_rm[3]=-1, proj_rm[7]=-1. Shifting the
        // input origin by (tx, ty) adds 2*tx/vw to column 3 of row 0
        // and -2*ty/vh to column 3 of row 1 (same sign on both axes
        // because the diagonal is positive in both).
        proj_rm[3]  += 2.0f * tx / std::max(vw_, 1.0f);
        proj_rm[7]  += 2.0f * ty / std::max(vh_, 1.0f);
        transpose4x4(proj_rm, pc_ui.matrix);
    }

    auto bind_ui = [&](tgfx::RenderContext2& c) {
        tgfx::ShaderHandle vs; vs.id = shader_vs_id_;
        tgfx::ShaderHandle fs; fs.id = shader_fs_id_;
        c.bind_shader(vs, fs);
        c.set_push_constants(&pc_ui, static_cast<uint32_t>(sizeof(pc_ui)));
    };
    bind_ui(*ctx);

    // Batch #1 — background + plot-area fill (two rects).
    {
        std::vector<float> verts;
        emit_rect_tris_(verts, vx_, vy_, vw_, vh_, bg_color);
        const Rect pa = plot_area_();
        emit_rect_tris_(verts, pa.x, pa.y, pa.w, pa.h, plot_bg_color);
        flush_triangles_(*ctx, verts);
    }

    const Rect pa = plot_area_();
    const ViewRange v = view_range_();

    // Batch #2 — grid inside clip. Scissor takes top-left origin
    // coords (Vulkan-native; OpenGL coerced into the same convention
    // by glClipControl(GL_UPPER_LEFT)).
    if (show_grid) {
        ctx->set_scissor((int)pa.x, (int)pa.y, (int)pa.w, (int)pa.h);
        bind_ui(*ctx);  // scissor sometimes drops bound state on some drivers

        const int max_x_ticks = std::max(int(pa.w / 80.0f), 3);
        const int max_y_ticks = std::max(int(pa.h / 50.0f), 3);
        const std::vector<double> x_ticks = axes::nice_ticks(v.x_min, v.x_max, max_x_ticks);
        const std::vector<double> y_ticks = axes::nice_ticks(v.y_min, v.y_max, max_y_ticks);

        std::vector<float> line_verts;
        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            emit_line_(line_verts, sx, pa.y, sx, pa.y + pa.h, grid_color);
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            emit_line_(line_verts, pa.x, sy, pa.x + pa.w, sy, grid_color);
        }
        flush_lines_(*ctx, line_verts);
    } else {
        ctx->set_scissor((int)pa.x, (int)pa.y, (int)pa.w, (int)pa.h);
        bind_ui(*ctx);
    }

    // Batch #3 — line series via persistent VBOs + VS-side data→clip.
    // Each series' points live in its own GPU buffer; append_to_line
    // only uploads the new tail. Pan/zoom is a uniform change, no
    // vertex re-upload. Scaling to time-series with 100k+ points is
    // trivial here — data never returns to the CPU per-frame.
    {
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

        // Restore the ui shader for subsequent scatter/outline batches.
        bind_ui(*ctx);
    }

    // Batch #4 — scatter (squares via triangle pairs).
    {
        uint32_t palette_i = (uint32_t)data.lines.size();
        std::vector<float> verts;
        for (const auto& s : data.scatters) {
            const Color4 c = s.color.has_value()
                ? *s.color : styles::cycle_color(palette_i);
            const float half = (float)s.size * 0.5f;
            for (size_t i = 0; i < s.x.size(); i++) {
                float sx, sy;
                data_to_pixel_(s.x[i], s.y[i], sx, sy);
                emit_rect_tris_(verts, sx - half, sy - half,
                                (float)s.size, (float)s.size, c);
            }
            palette_i++;
        }
        flush_triangles_(*ctx, verts);
    }

    // End inner-area clip.
    ctx->clear_scissor();
    bind_ui(*ctx);

    // Batch #5 — axes border (outside the inner scissor).
    {
        std::vector<float> verts;
        emit_rect_outline_lines_(verts, pa.x, pa.y, pa.w, pa.h, axis_color);
        flush_lines_(*ctx, verts);
    }

    // --- Text: tick labels, title, axis labels ---
    if (font) {
        text2d_->begin(ctx, (int)vw_, (int)vh_, font);

        // Tick labels outside the clip.
        const int max_x_ticks = std::max(int(pa.w / 80.0f), 3);
        const int max_y_ticks = std::max(int(pa.h / 50.0f), 3);
        const std::vector<double> x_ticks = axes::nice_ticks(v.x_min, v.x_max, max_x_ticks);
        const std::vector<double> y_ticks = axes::nice_ticks(v.y_min, v.y_max, max_y_ticks);
        // Ticks a notch smaller than axis labels — keeps the axis-label
        // / tick-label hierarchy visible even at similar sizes.
        const float tick_sz = font_size - 2.0f;

        // Our Text2DRenderer expects viewport-pixel coords starting at
        // (0, 0) top-left of the viewport. Our engine2d may be offset
        // by (vx_, vy_), but the render-pass' color attachment was
        // already (vw_ x vh_) sized, so (vx_, vy_) is effectively (0,0)
        // within the FBO. Translate accordingly by subtracting.
        auto T_x = [&](float x) { return x - vx_; };
        auto T_y = [&](float y) { return y - vy_; };

        for (double tx : x_ticks) {
            float sx, _sy;
            data_to_pixel_(tx, 0.0, sx, _sy);
            text2d_->draw(axes::format_tick(tx),
                          T_x(sx), T_y(pa.y + pa.h + 14.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          tick_sz,
                          tgfx::Text2DRenderer::Anchor::Center);
        }
        for (double ty : y_ticks) {
            float _sx, sy;
            data_to_pixel_(0.0, ty, _sx, sy);
            const std::string lab = axes::format_tick(ty);
            const auto m = font->measure_text(lab, tick_sz);
            const float tw = m.width;
            text2d_->draw(lab,
                          T_x(pa.x - tw - 6.0f), T_y(sy + 4.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          tick_sz,
                          tgfx::Text2DRenderer::Anchor::Left);
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
            // Pick colour from the first line series if any, so
            // stacked panels read at a glance as "sin is blue, cos is
            // orange, ..." instead of drowning every title in the
            // same label_color grey.
            Color4 tc = label_color;
            if (!data.lines.empty()) {
                const auto& s = data.lines.front();
                tc = s.color.has_value() ? *s.color : styles::cycle_color(0u);
            }
            const int title_lh = font->line_height_px(title_font_size);
            // Clamp to viewport top so a title bigger than margin_top
            // gets cut off at the top instead of drawing at a negative
            // Y — caller's cue to bump margin_top.
            const float title_top_y = std::max(
                static_cast<float>(vy_),
                pa.y - static_cast<float>(title_lh) - title_pad);
            text2d_->draw(data.title,
                          T_x(pa.x),
                          T_y(title_top_y),
                          tc.r, tc.g, tc.b, tc.a,
                          title_font_size,
                          tgfx::Text2DRenderer::Anchor::Left);
        }
        if (!data.x_label.empty()) {
            text2d_->draw(data.x_label,
                          T_x(pa.x + pa.w * 0.5f),
                          T_y(pa.y + pa.h + margin_bottom - 4.0f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          font_size,
                          tgfx::Text2DRenderer::Anchor::Center);
        }
        if (!data.y_label.empty()) {
            text2d_->draw(data.y_label,
                          T_x(vx_ + margin_left * 0.5f),
                          T_y(pa.y + pa.h * 0.5f),
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          font_size,
                          tgfx::Text2DRenderer::Anchor::Center);
        }

        text2d_->end();
    }
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
