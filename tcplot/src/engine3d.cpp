// engine3d.cpp - Host-agnostic 3D plot engine. Port of engine3d.py.
//
// Layout of this file, top to bottom:
//   - Shared shader source (vert/frag with jet colormap).
//   - Internal helpers (vertex layout builder, tgfx2 mesh upload/draw).
//   - Constructors / destructor.
//   - Public add-series API.
//   - Mesh building (lines / scatter / grid / surface / wireframe).
//   - render() + tick-label drawing.
//   - Picking + input handlers.

#include "tcplot/engine3d.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text3d_renderer.hpp>
#include <tgfx2/vertex_layout.hpp>

#include "tcplot/axes.hpp"

namespace tcplot {

namespace {

struct Plot3DPushData {
    float mvp[16];
    float params[4];  // z_min, z_max, surface_mode, colormap_id
    float surface_color[4];
    float axis_shading[4];    // axis_scale.xyz, shading_enabled
    float light_strength[4];  // light_dir.xyz, shading_strength
};
static_assert(sizeof(Plot3DPushData) == 128,
              "Plot3DPushData layout drift — shader + C++ disagree");

constexpr const char* kPC = R"(
struct Plot3DPC {
    mat4 u_mvp;
    vec4 u_params;
    vec4 u_surface_color;
    vec4 u_axis_shading;
    vec4 u_light_strength;
};
#ifdef VULKAN
layout(push_constant) uniform PCBlock { Plot3DPC pc; };
#else
layout(std140, binding = 14) uniform PCBlock { Plot3DPC pc; };
#endif
)";

// Same 3D plot shader as engine3d.py. Position in vec3, per-vertex
// RGBA color in vec4 (loc 1). For filled surfaces, the fragment stage
// derives color from normalized Z using the selected colormap.
static std::string make_vert_src() {
    return std::string("#version 450 core\n") + kPC + R"(
layout(location=0) in vec3 a_position;
layout(location=1) in vec4 a_color;
layout(location=2) in vec4 a_surface_grid;
layout(location=3) in vec4 a_surface_grid_color;
layout(location=4) in vec4 a_surface_grid_opts;
layout(location=0) out vec4 v_color;
layout(location=1) out float v_z_norm;
layout(location=2) out vec3 v_scaled_pos;
layout(location=3) out vec4 v_surface_grid;
layout(location=4) out vec4 v_surface_grid_color;
layout(location=5) out vec4 v_surface_grid_opts;
void main() {
    gl_Position = pc.u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
    v_scaled_pos = a_position * pc.u_axis_shading.xyz;
    v_surface_grid = a_surface_grid;
    v_surface_grid_color = a_surface_grid_color;
    v_surface_grid_opts = a_surface_grid_opts;
    float z_range = pc.u_params.y - pc.u_params.x;
    v_z_norm = (z_range > 0.0) ? (a_position.z - pc.u_params.x) / z_range : 0.5;
}
)";
}

static std::string make_frag_src() {
    return std::string("#version 450 core\n") + kPC + R"(
layout(location=0) in vec4 v_color;
layout(location=1) in float v_z_norm;
layout(location=2) in vec3 v_scaled_pos;
layout(location=3) in vec4 v_surface_grid;
layout(location=4) in vec4 v_surface_grid_color;
layout(location=5) in vec4 v_surface_grid_opts;
layout(location=0) out vec4 frag_color;

vec3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    float r, g, b;
    if (t < 0.125) {
        r = 0.0; g = 0.0; b = 0.5 + t * 4.0;
    } else if (t < 0.375) {
        r = 0.0; g = (t - 0.125) * 4.0; b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) * 4.0; g = 1.0; b = 1.0 - (t - 0.375) * 4.0;
    } else if (t < 0.875) {
        r = 1.0; g = 1.0 - (t - 0.625) * 4.0; b = 0.0;
    } else {
        r = 1.0 - (t - 0.875) * 4.0; g = 0.0; b = 0.0;
    }
    return vec3(r, g, b);
}

vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.267, 0.005, 0.329);
    vec3 c1 = vec3(0.283, 0.141, 0.458);
    vec3 c2 = vec3(0.254, 0.265, 0.530);
    vec3 c3 = vec3(0.207, 0.372, 0.553);
    vec3 c4 = vec3(0.164, 0.471, 0.558);
    vec3 c5 = vec3(0.128, 0.567, 0.551);
    vec3 c6 = vec3(0.135, 0.659, 0.518);
    vec3 c7 = vec3(0.267, 0.749, 0.441);
    vec3 c8 = vec3(0.478, 0.821, 0.318);
    vec3 c9 = vec3(0.741, 0.873, 0.150);
    float x = t * 9.0;
    int i = int(floor(x));
    float f = fract(x);
    if (i <= 0) return mix(c0, c1, f);
    if (i == 1) return mix(c1, c2, f);
    if (i == 2) return mix(c2, c3, f);
    if (i == 3) return mix(c3, c4, f);
    if (i == 4) return mix(c4, c5, f);
    if (i == 5) return mix(c5, c6, f);
    if (i == 6) return mix(c6, c7, f);
    if (i == 7) return mix(c7, c8, f);
    return mix(c8, c9, f);
}

vec3 plasma(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.050, 0.030, 0.528);
    vec3 c1 = vec3(0.362, 0.004, 0.649);
    vec3 c2 = vec3(0.610, 0.090, 0.620);
    vec3 c3 = vec3(0.798, 0.280, 0.470);
    vec3 c4 = vec3(0.928, 0.473, 0.326);
    vec3 c5 = vec3(0.994, 0.704, 0.184);
    vec3 c6 = vec3(0.940, 0.975, 0.131);
    float x = t * 6.0;
    int i = int(floor(x));
    float f = fract(x);
    if (i <= 0) return mix(c0, c1, f);
    if (i == 1) return mix(c1, c2, f);
    if (i == 2) return mix(c2, c3, f);
    if (i == 3) return mix(c3, c4, f);
    if (i == 4) return mix(c4, c5, f);
    return mix(c5, c6, f);
}

vec3 cool_warm(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 cool = vec3(0.230, 0.299, 0.754);
    vec3 mid = vec3(0.865, 0.865, 0.865);
    vec3 warm = vec3(0.706, 0.016, 0.150);
    return (t < 0.5)
        ? mix(cool, mid, t * 2.0)
        : mix(mid, warm, (t - 0.5) * 2.0);
}

vec3 map_surface_color(float t, float map_id) {
    int id = int(map_id + 0.5);
    if (id >= 100) {
        id -= 100;
        t = 1.0 - t;
    }
    if (id == 1) return viridis(t);
    if (id == 2) return plasma(t);
    if (id == 3) return vec3(clamp(t, 0.0, 1.0));
    if (id == 4) return cool_warm(t);
    if (id == 5) return pc.u_surface_color.rgb;
    return jet(t);
}

float grid_line_mask(float coord, float step, float max_coord, float width_px) {
    step = max(step, 1.0);
    float d_step = abs(coord - round(coord / step) * step);
    float d_edge = min(abs(coord), abs(coord - max_coord));
    float d = min(d_step, d_edge);
    float fw = max(fwidth(coord), 1e-6);
    float half_width = max(width_px, 0.1) * fw * 0.5;
    return 1.0 - smoothstep(half_width, half_width + fw, d);
}

void main() {
    if (pc.u_params.z != 0.0) {
        vec3 rgb = map_surface_color(v_z_norm, pc.u_params.w);
        if (pc.u_axis_shading.w != 0.0) {
            vec3 dx = dFdx(v_scaled_pos);
            vec3 dy = dFdy(v_scaled_pos);
            vec3 n = normalize(cross(dx, dy));
            vec3 l = normalize(pc.u_light_strength.xyz);
            float ndl = abs(dot(n, l));
            float strength = pc.u_light_strength.w;
            float shade = 1.0 - strength + strength * ndl;
            shade = clamp(shade, 0.72, 1.08);
            rgb *= shade;
        }
        if (v_surface_grid_opts.x != 0.0) {
            float col_mask = grid_line_mask(
                v_surface_grid.x,
                v_surface_grid.z,
                v_surface_grid_opts.z,
                v_surface_grid_opts.y);
            float row_mask = grid_line_mask(
                v_surface_grid.y,
                v_surface_grid.w,
                v_surface_grid_opts.w,
                v_surface_grid_opts.y);
            float grid_mask = max(col_mask, row_mask);
            rgb = mix(rgb, v_surface_grid_color.rgb,
                      clamp(v_surface_grid_color.a * grid_mask, 0.0, 1.0));
        }
        frag_color = vec4(rgb, pc.u_surface_color.a);
    } else {
        frag_color = v_color;
    }
}
)";
}

constexpr uint32_t kFloatsPerVertex = 19;
constexpr uint32_t kVertexStride = kFloatsPerVertex * sizeof(float);

tgfx::VertexBufferLayout pos_color_layout() {
    tgfx::VertexBufferLayout layout;
    layout.stride = kVertexStride;
    layout.attributes.push_back({0, tgfx::VertexFormat::Float3, 0});
    layout.attributes.push_back({1, tgfx::VertexFormat::Float4, 3 * sizeof(float)});
    layout.attributes.push_back({2, tgfx::VertexFormat::Float4, 7 * sizeof(float)});
    layout.attributes.push_back({3, tgfx::VertexFormat::Float4, 11 * sizeof(float)});
    layout.attributes.push_back({4, tgfx::VertexFormat::Float4, 15 * sizeof(float)});
    return layout;
}

void set_plot3d_push_constants(tgfx::RenderContext2& ctx,
                               const float mvp[16],
                               float z_min,
                               float z_max,
                               bool surface_mode,
                               const PlotEngine3D& engine,
                               SurfaceColorMap colormap = SurfaceColorMap::Jet,
                               bool colormap_reversed = false,
                               Color4 surface_color = {1.0f, 1.0f, 1.0f, 1.0f}) {
    Plot3DPushData pc{};
    std::memcpy(pc.mvp, mvp, sizeof(pc.mvp));
    pc.params[0] = z_min;
    pc.params[1] = z_max;
    pc.params[2] = surface_mode ? 1.0f : 0.0f;
    pc.params[3] = static_cast<float>(colormap)
                 + (colormap_reversed ? 100.0f : 0.0f);
    pc.surface_color[0] = surface_color.r;
    pc.surface_color[1] = surface_color.g;
    pc.surface_color[2] = surface_color.b;
    pc.surface_color[3] = surface_color.a;
    pc.axis_shading[0] = engine.x_scale;
    pc.axis_shading[1] = engine.y_scale;
    pc.axis_shading[2] = engine.z_scale;
    pc.axis_shading[3] = (surface_mode && engine.surface_shading) ? 1.0f : 0.0f;
    pc.light_strength[0] = engine.surface_light_dir[0];
    pc.light_strength[1] = engine.surface_light_dir[1];
    pc.light_strength[2] = engine.surface_light_dir[2];
    pc.light_strength[3] = std::clamp(engine.surface_shading_strength, 0.0f, 1.0f);
    ctx.set_push_constants(&pc, static_cast<uint32_t>(sizeof(pc)));
}

}  // namespace

std::optional<PlotEngine3D::MeshGpu> PlotEngine3D::make_mesh_(
    tgfx::IRenderDevice& device,
    const std::vector<float>& verts,
    const std::vector<uint32_t>& indices,
    tgfx::PrimitiveTopology topology
) {
    if (verts.empty() || indices.empty()) return std::nullopt;
    tgfx::BufferDesc vb_desc;
    vb_desc.size = static_cast<uint64_t>(verts.size()) * sizeof(float);
    vb_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle vbo = device.create_buffer(vb_desc);
    device.upload_buffer(
        vbo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(verts.data()),
            verts.size() * sizeof(float)));

    tgfx::BufferDesc ib_desc;
    ib_desc.size = static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
    ib_desc.usage = tgfx::BufferUsage::Index | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle ibo = device.create_buffer(ib_desc);
    device.upload_buffer(
        ibo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(indices.data()),
            indices.size() * sizeof(uint32_t)));

    return PlotEngine3D::MeshGpu{
        vbo,
        ibo,
        static_cast<uint32_t>(indices.size()),
        topology,
    };
}

void PlotEngine3D::draw_mesh_(tgfx::RenderContext2& ctx, const MeshGpu& mesh) {
    if (mesh.vbo.id == 0 || mesh.ibo.id == 0 || mesh.index_count == 0) return;
    ctx.set_vertex_layout(pos_color_layout());
    ctx.set_topology(mesh.topology);
    ctx.draw(mesh.vbo, mesh.ibo, mesh.index_count, tgfx::IndexType::Uint32);
}

namespace {

// Push the 7-float (pos+color) vertex for a single point.
inline void push_vertex(std::vector<float>& verts,
                        float x, float y, float z, const Color4& c) {
    verts.push_back(x);
    verts.push_back(y);
    verts.push_back(z);
    verts.push_back(c.r);
    verts.push_back(c.g);
    verts.push_back(c.b);
    verts.push_back(c.a);
    for (int i = 0; i < 12; ++i) verts.push_back(0.0f);
}

inline void push_surface_vertex(std::vector<float>& verts,
                                float x, float y, float z,
                                const Color4& c,
                                float col, float row,
                                float row_step, float col_step,
                                const Color4& grid_color,
                                bool grid_visible,
                                float grid_width_px,
                                float max_col,
                                float max_row) {
    verts.push_back(x);
    verts.push_back(y);
    verts.push_back(z);
    verts.push_back(c.r);
    verts.push_back(c.g);
    verts.push_back(c.b);
    verts.push_back(c.a);
    verts.push_back(col);
    verts.push_back(row);
    verts.push_back(col_step);
    verts.push_back(row_step);
    verts.push_back(grid_color.r);
    verts.push_back(grid_color.g);
    verts.push_back(grid_color.b);
    verts.push_back(grid_color.a);
    verts.push_back(grid_visible ? 1.0f : 0.0f);
    verts.push_back(grid_width_px);
    verts.push_back(max_col);
    verts.push_back(max_row);
}

// Resolve a possibly-missing Color4 against a series index. Matches
// Python behaviour of "no color → palette cycle by index".
Color4 resolve_color(const std::optional<Color4>& c, uint32_t palette_idx,
                     Color4 fallback) {
    if (c.has_value()) return *c;
    if (palette_idx != UINT32_MAX) return styles::cycle_color(palette_idx);
    return fallback;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PlotEngine3D::PlotEngine3D()
    : text3d_(std::make_unique<tgfx::Text3DRenderer>()) {}

PlotEngine3D::~PlotEngine3D() {
    release_gpu_resources();
}

void PlotEngine3D::set_viewport(float x, float y, float width, float height) {
    vx_ = x;
    vy_ = y;
    vw_ = width;
    vh_ = height;
}

// ---------------------------------------------------------------------------
// Public add-series
// ---------------------------------------------------------------------------

void PlotEngine3D::plot(std::vector<double> x, std::vector<double> y,
                         std::vector<double> z,
                         std::optional<Color4> color,
                         double thickness,
                         std::string label) {
    data.add_line(std::move(x), std::move(y), std::move(z),
                  color, thickness, std::move(label));
    dirty_ = true;

    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    const float lo_f[3] = {(float)lo[0], (float)lo[1], (float)lo[2]};
    const float hi_f[3] = {(float)hi[0], (float)hi[1], (float)hi[2]};
    camera.fit_bounds(lo_f, hi_f);
}

void PlotEngine3D::scatter(std::vector<double> x, std::vector<double> y,
                            std::vector<double> z,
                            std::optional<Color4> color,
                            double size,
                            std::string label) {
    data.add_scatter(std::move(x), std::move(y), std::move(z),
                     color, size, std::move(label));
    dirty_ = true;

    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    const float lo_f[3] = {(float)lo[0], (float)lo[1], (float)lo[2]};
    const float hi_f[3] = {(float)hi[0], (float)hi[1], (float)hi[2]};
    camera.fit_bounds(lo_f, hi_f);
}

void PlotEngine3D::surface(std::vector<double> X, std::vector<double> Y,
                            std::vector<double> Z,
                            uint32_t rows, uint32_t cols,
                            std::optional<Color4> color,
                            SurfaceColorMap colormap,
                            bool wireframe,
                            std::string label,
                            bool colormap_reversed) {
    SurfaceSeries s;
    s.X = std::move(X);
    s.Y = std::move(Y);
    s.Z = std::move(Z);
    s.rows = rows;
    s.cols = cols;
    if (color.has_value()) {
        s.color = *color;
    } else {
        const uint32_t idx = static_cast<uint32_t>(
            data.lines.size() + data.scatters.size() + data.surfaces.size());
        s.color = styles::cycle_color(idx);
    }
    s.colormap = colormap;
    s.colormap_reversed = colormap_reversed;
    s.wireframe = wireframe;
    s.label = std::move(label);
    data.surfaces.push_back(std::move(s));
    dirty_ = true;

    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    const float lo_f[3] = {(float)lo[0], (float)lo[1], (float)lo[2]};
    const float hi_f[3] = {(float)hi[0], (float)hi[1], (float)hi[2]};
    camera.fit_bounds(lo_f, hi_f);
}

void PlotEngine3D::clear() {
    data = PlotData{};
    release_meshes_();
    dirty_ = true;
}

bool PlotEngine3D::set_surface_colormap(size_t idx, SurfaceColorMap colormap) {
    if (idx >= data.surfaces.size()) return false;
    data.surfaces[idx].colormap = colormap;
    dirty_ = true;
    return true;
}

bool PlotEngine3D::set_surface_colormap_reversed(size_t idx, bool reversed) {
    if (idx >= data.surfaces.size()) return false;
    data.surfaces[idx].colormap_reversed = reversed;
    dirty_ = true;
    return true;
}

bool PlotEngine3D::set_surface_color(size_t idx, Color4 color) {
    if (idx >= data.surfaces.size()) return false;
    data.surfaces[idx].color = color;
    dirty_ = true;
    return true;
}

bool PlotEngine3D::set_surface_grid(size_t idx, bool visible,
                                    uint32_t row_step, uint32_t col_step,
                                    Color4 color,
                                    float width_px) {
    if (idx >= data.surfaces.size()) return false;
    SurfaceSeries& surf = data.surfaces[idx];
    surf.grid_visible = visible;
    surf.grid_row_step = std::max<uint32_t>(1, row_step);
    surf.grid_col_step = std::max<uint32_t>(1, col_step);
    surf.grid_width_px = std::max(width_px, 0.1f);
    surf.grid_color = color;
    dirty_ = true;
    return true;
}

void PlotEngine3D::toggle_marker_mode() {
    marker_mode = !marker_mode;
    if (!marker_mode) {
        has_marker_ = false;
    }
}

void PlotEngine3D::set_surface_shading(bool enabled, float strength) {
    surface_shading = enabled;
    surface_shading_strength = std::clamp(strength, 0.0f, 1.0f);
}

void PlotEngine3D::set_surface_light_dir(float x, float y, float z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 1e-6f) return;
    surface_light_dir[0] = x / len;
    surface_light_dir[1] = y / len;
    surface_light_dir[2] = z / len;
}

// ---------------------------------------------------------------------------
// GPU resource management
// ---------------------------------------------------------------------------

void PlotEngine3D::release_meshes_() {
    auto drop = [this](std::optional<MeshGpu>& m) {
        if (m.has_value()) {
            if (mesh_device_) {
                if (m->vbo.id != 0) mesh_device_->destroy(m->vbo);
                if (m->ibo.id != 0) mesh_device_->destroy(m->ibo);
            }
            m.reset();
        }
    };
    drop(lines_mesh_);
    drop(scatter_mesh_);
    drop(grid_mesh_);
    if (mesh_device_) {
        for (auto& m : surface_meshes_) {
            if (m.vbo.id != 0) mesh_device_->destroy(m.vbo);
            if (m.ibo.id != 0) mesh_device_->destroy(m.ibo);
        }
        for (auto& m : wireframe_meshes_) {
            if (m.vbo.id != 0) mesh_device_->destroy(m.vbo);
            if (m.ibo.id != 0) mesh_device_->destroy(m.ibo);
        }
    }
    surface_meshes_.clear();
    surface_mesh_styles_.clear();
    wireframe_meshes_.clear();
    mesh_device_ = nullptr;
}

void PlotEngine3D::release_gpu_resources() {
    release_meshes_();
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
    if (text3d_) text3d_->release_gpu();
}

void PlotEngine3D::ensure_shader_(tgfx::IRenderDevice& device) {
    if (shader_device_ == &device && shader_vs_id_ != 0 && shader_fs_id_ != 0) {
        return;
    }
    // Device changed or first call — drop old and recompile.
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
    vd.source = make_vert_src();
    shader_vs_id_ = device.create_shader(vd).id;

    tgfx::ShaderDesc fd;
    fd.stage = tgfx::ShaderStage::Fragment;
    fd.source = make_frag_src();
    shader_fs_id_ = device.create_shader(fd).id;

    shader_device_ = &device;
}

// ---------------------------------------------------------------------------
// Mesh building
// ---------------------------------------------------------------------------

void PlotEngine3D::rebuild_meshes_(tgfx::IRenderDevice& device) {
    release_meshes_();
    mesh_device_ = &device;

    // --- Lines ---
    {
        std::vector<float> verts;
        std::vector<uint32_t> indices;
        uint32_t idx = 0;
        uint32_t palette_i = 0;
        for (const auto& s : data.lines) {
            if (s.z.empty() || s.x.size() < 2) {
                palette_i++;
                continue;
            }
            const Color4 c = resolve_color(s.color, palette_i, styles::axis_color());
            for (size_t i = 0; i < s.x.size(); i++) {
                push_vertex(verts, (float)s.x[i], (float)s.y[i], (float)s.z[i], c);
            }
            for (size_t i = 0; i + 1 < s.x.size(); i++) {
                indices.push_back(idx + (uint32_t)i);
                indices.push_back(idx + (uint32_t)i + 1);
            }
            idx += (uint32_t)s.x.size();
            palette_i++;
        }
        if (!verts.empty()) {
            lines_mesh_ = make_mesh_(device, verts, indices,
                                     tgfx::PrimitiveTopology::LineList);
        }
    }

    // --- Data bounds used by scatter cross size and grid extents ---
    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    const double data_span = std::sqrt(
        (hi[0] - lo[0]) * (hi[0] - lo[0]) +
        (hi[1] - lo[1]) * (hi[1] - lo[1]) +
        (hi[2] - lo[2]) * (hi[2] - lo[2]));
    const float cs = (float)(data_span * 0.008);

    // --- Scatter (cross: 3 axis-aligned line segments per point) ---
    {
        std::vector<float> verts;
        std::vector<uint32_t> indices;
        uint32_t idx = 0;
        uint32_t palette_i =
            (uint32_t)(data.lines.size());  // continue palette past lines
        for (const auto& s : data.scatters) {
            if (s.z.empty() || s.x.empty()) {
                palette_i++;
                continue;
            }
            const Color4 c = resolve_color(s.color, palette_i, styles::axis_color());
            for (size_t i = 0; i < s.x.size(); i++) {
                const float px = (float)s.x[i];
                const float py = (float)s.y[i];
                const float pz = (float)s.z[i];
                struct Axis { float dx, dy, dz; };
                const Axis axes[] = {{cs, 0, 0}, {0, cs, 0}, {0, 0, cs}};
                for (const auto& a : axes) {
                    push_vertex(verts, px - a.dx, py - a.dy, pz - a.dz, c);
                    push_vertex(verts, px + a.dx, py + a.dy, pz + a.dz, c);
                    indices.push_back(idx);
                    indices.push_back(idx + 1);
                    idx += 2;
                }
            }
            palette_i++;
        }
        if (!verts.empty()) {
            scatter_mesh_ = make_mesh_(device, verts, indices,
                                       tgfx::PrimitiveTopology::LineList);
        }
    }

    // --- Surfaces ---
    for (const auto& surf : data.surfaces) {
        build_surface_mesh_(device, surf);
    }

    // --- Grid + axes ---
    if (show_grid) {
        build_grid_mesh_(device, lo, hi);
    }

    dirty_ = false;
}

void PlotEngine3D::build_grid_mesh_(tgfx::IRenderDevice& device,
                                     const double bounds_min[3],
                                     const double bounds_max[3]) {
    std::vector<float> verts;
    std::vector<uint32_t> indices;
    uint32_t idx = 0;
    // Brighter 3D grid than the 2D default. 1-pixel GL lines under MSAA
    // get very dim if sampled with low coverage; full alpha + mid-gray
    // keeps the grid legible without drowning the data series.
    // styles::grid_color() = (0.3, 0.3, 0.3, 0.5) stays the 2D choice.
    const Color4 gc{0.55f, 0.55f, 0.55f, 1.0f};

    // For each axis, draw ticks as line segments running along the
    // bounding box's "floor" on one of the other two axes. Matches
    // Python's `_build_grid_mesh` 1:1.
    for (int axis = 0; axis < 3; ++axis) {
        const std::vector<double> ticks =
            axes::nice_ticks(bounds_min[axis], bounds_max[axis], 8);
        const int a1 = (axis == 0) ? 1 : 0;
        const int a2 = (axis == 2) ? 1 : 2;
        // Python picks other_axes = [a for a in 0..2 if a != axis],
        // then a1, a2 = other_axes. Explicitly replicate.
        int oa[2] = {-1, -1};
        int k = 0;
        for (int a = 0; a < 3; ++a) if (a != axis) oa[k++] = a;
        (void)a1; (void)a2;  // suppress unused above

        for (double t : ticks) {
            double p0[3] = {0, 0, 0};
            double p1[3] = {0, 0, 0};
            p0[axis] = t;
            p1[axis] = t;
            p0[oa[0]] = bounds_min[oa[0]];
            p1[oa[0]] = bounds_max[oa[0]];
            p0[oa[1]] = bounds_min[oa[1]];
            p1[oa[1]] = bounds_min[oa[1]];  // along a1, locked at min of a2
            push_vertex(verts, (float)p0[0], (float)p0[1], (float)p0[2], gc);
            push_vertex(verts, (float)p1[0], (float)p1[1], (float)p1[2], gc);
            indices.push_back(idx);
            indices.push_back(idx + 1);
            idx += 2;
        }
    }

    // Axis lines through bounds_min: x=red, y=green, z=blue.
    const Color4 axis_colors[3] = {
        {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1},
    };
    for (int axis = 0; axis < 3; ++axis) {
        double p0[3] = {bounds_min[0], bounds_min[1], bounds_min[2]};
        double p1[3] = {p0[0], p0[1], p0[2]};
        p1[axis] = bounds_max[axis];
        push_vertex(verts, (float)p0[0], (float)p0[1], (float)p0[2], axis_colors[axis]);
        push_vertex(verts, (float)p1[0], (float)p1[1], (float)p1[2], axis_colors[axis]);
        indices.push_back(idx);
        indices.push_back(idx + 1);
        idx += 2;
    }

    if (!verts.empty()) {
        grid_mesh_ = make_mesh_(device, verts, indices,
                                tgfx::PrimitiveTopology::LineList);
    }
}

void PlotEngine3D::build_surface_mesh_(tgfx::IRenderDevice& device,
                                       const SurfaceSeries& surf) {
    const uint32_t rows = surf.rows;
    const uint32_t cols = surf.cols;
    if (rows < 2 || cols < 2) return;

    const Color4 surface_color = surf.color.value_or(Color4{1.0f, 1.0f, 1.0f, 1.0f});
    const float alpha = surface_color.a;

    const Color4 grid_color =
        surf.grid_color.value_or(Color4{0.05f, 0.05f, 0.05f, 1.0f});
    const float row_step = static_cast<float>(std::max<uint32_t>(1, surf.grid_row_step));
    const float col_step = static_cast<float>(std::max<uint32_t>(1, surf.grid_col_step));
    const float grid_width_px = std::max(surf.grid_width_px, 0.1f);

    std::vector<float> verts;
    verts.reserve((size_t)rows * cols * kFloatsPerVertex);
    for (uint32_t j = 0; j < rows; ++j) {
        for (uint32_t i = 0; i < cols; ++i) {
            const size_t idx = (size_t)j * cols + i;
            const double x = surf.X[idx];
            const double y = surf.Y[idx];
            const double z = surf.Z[idx];
            push_surface_vertex(verts, (float)x, (float)y, (float)z,
                                {surface_color.r, surface_color.g, surface_color.b, alpha},
                                static_cast<float>(i), static_cast<float>(j),
                                row_step, col_step,
                                grid_color,
                                surf.grid_visible,
                                grid_width_px,
                                static_cast<float>(cols - 1),
                                static_cast<float>(rows - 1));
        }
    }

    std::vector<uint32_t> tri_indices;
    tri_indices.reserve((size_t)(rows - 1) * (cols - 1) * 6);
    for (uint32_t j = 0; j + 1 < rows; ++j) {
        for (uint32_t i = 0; i + 1 < cols; ++i) {
            const uint32_t v00 = j * cols + i;
            const uint32_t v10 = j * cols + i + 1;
            const uint32_t v01 = (j + 1) * cols + i;
            const uint32_t v11 = (j + 1) * cols + i + 1;
            tri_indices.push_back(v00);
            tri_indices.push_back(v10);
            tri_indices.push_back(v01);
            tri_indices.push_back(v10);
            tri_indices.push_back(v11);
            tri_indices.push_back(v01);
        }
    }

    if (surf.wireframe) {
        std::vector<uint32_t> wire_indices;
        wire_indices.reserve(tri_indices.size() * 2);
        for (size_t k = 0; k + 2 < tri_indices.size(); k += 3) {
            const uint32_t a = tri_indices[k];
            const uint32_t b = tri_indices[k + 1];
            const uint32_t c = tri_indices[k + 2];
            wire_indices.push_back(a); wire_indices.push_back(b);
            wire_indices.push_back(b); wire_indices.push_back(c);
            wire_indices.push_back(c); wire_indices.push_back(a);
        }
        auto mesh = make_mesh_(device, verts, wire_indices,
                               tgfx::PrimitiveTopology::LineList);
        if (mesh.has_value()) wireframe_meshes_.push_back(std::move(*mesh));
    } else {
        auto mesh = make_mesh_(device, verts, tri_indices,
                               tgfx::PrimitiveTopology::TriangleList);
        if (mesh.has_value()) {
            surface_meshes_.push_back(std::move(*mesh));
            surface_mesh_styles_.push_back(surf);
        }
    }
}

// ---------------------------------------------------------------------------
// MVP / rendering
// ---------------------------------------------------------------------------

void PlotEngine3D::compute_mvp_(float aspect, float out16[16]) const {
    float mvp[16];
    camera.mvp(aspect, mvp);

    if (std::abs(x_scale - 1.0f) < 1e-6f &&
        std::abs(y_scale - 1.0f) < 1e-6f &&
        std::abs(z_scale - 1.0f) < 1e-6f) {
        std::memcpy(out16, mvp, sizeof(mvp));
        return;
    }

    // Post-multiply by diag(x_scale, y_scale, z_scale, 1). Data stays
    // in original units; only the view transform changes.
    std::memcpy(out16, mvp, sizeof(mvp));
    for (int r = 0; r < 4; ++r) {
        out16[0 * 4 + r] *= x_scale;
        out16[1 * 4 + r] *= y_scale;
        out16[2 * 4 + r] *= z_scale;
    }
}

void PlotEngine3D::render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font) {
    if (!ctx || vw_ <= 0 || vh_ <= 0) return;

    ensure_shader_(ctx->device());
    if (mesh_device_ != nullptr && mesh_device_ != &ctx->device()) {
        release_meshes_();
        dirty_ = true;
    }
    if (dirty_) rebuild_meshes_(ctx->device());

    ctx->set_depth_test(true);
    ctx->set_blend(true);
    ctx->set_cull(tgfx::CullMode::None);

    const float aspect = vw_ / std::max(vh_, 1.0f);
    float mvp[16];
    compute_mvp_(aspect, mvp);

    tgfx::ShaderHandle vs; vs.id = shader_vs_id_;
    tgfx::ShaderHandle fs; fs.id = shader_fs_id_;
    ctx->bind_shader(vs, fs);

    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    const float z_min = static_cast<float>(lo[2]);
    const float z_max = static_cast<float>(hi[2]);

    // Grid (no jet).
    set_plot3d_push_constants(*ctx, mvp, z_min, z_max, false, *this);
    if (grid_mesh_) draw_mesh_(*ctx, *grid_mesh_);

    // Opaque surfaces. Color mapping is shader-driven to avoid baking
    // colormap transitions into mesh vertex colors.
    ctx->set_blend(false);
    for (size_t i = 0; i < surface_meshes_.size(); ++i) {
        const SurfaceSeries& style = surface_mesh_styles_[i];
        const Color4 surface_color =
            style.color.value_or(Color4{1.0f, 1.0f, 1.0f, 1.0f});
        set_plot3d_push_constants(*ctx, mvp, z_min, z_max, true, *this,
                                  style.colormap, style.colormap_reversed,
                                  surface_color);
        draw_mesh_(*ctx, surface_meshes_[i]);
    }

    // Wireframes on top (no depth, no jet).
    set_plot3d_push_constants(*ctx, mvp, z_min, z_max, false, *this);
    if (show_wireframe) {
        ctx->set_depth_test(false);
        ctx->set_blend(true);
        for (auto& m : wireframe_meshes_) {
            draw_mesh_(*ctx, m);
        }
        ctx->set_depth_test(true);
    }

    // Lines and scatter.
    if (lines_mesh_) draw_mesh_(*ctx, *lines_mesh_);
    if (scatter_mesh_) draw_mesh_(*ctx, *scatter_mesh_);

    // Marker (immediate mode cross at marker pos).
    if (has_marker_ && marker_mode) {
        ctx->bind_shader(vs, fs);
        set_plot3d_push_constants(*ctx, mvp, z_min, z_max, false, *this);
        ctx->set_depth_test(false);

        const double dx = (hi[0] - lo[0]) * x_scale;
        const double dy = (hi[1] - lo[1]) * y_scale;
        const double dz = (hi[2] - lo[2]) * z_scale;
        const double data_size = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float cs = (float)(data_size * 0.015);
        const Color4 c{1.0f, 1.0f, 0.0f, 1.0f};
        std::vector<float> verts;
        const float px = (float)marker_x_;
        const float py = (float)marker_y_;
        const float pz = (float)marker_z_;
        struct Axis { float dx, dy, dz; };
        const Axis axes_[] = {{cs, 0, 0}, {0, cs, 0}, {0, 0, cs}};
        auto push_immediate_vertex = [&](float x, float y, float z) {
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
            verts.push_back(c.r);
            verts.push_back(c.g);
            verts.push_back(c.b);
            verts.push_back(c.a);
        };
        for (const auto& a : axes_) {
            push_immediate_vertex(px - a.dx, py - a.dy, pz - a.dz);
            push_immediate_vertex(px + a.dx, py + a.dy, pz + a.dz);
        }
        ctx->draw_immediate_lines(verts.data(),
                                  (uint32_t)(verts.size() / 7));
        ctx->set_depth_test(true);
    }

    // Tick labels + marker value label via Text3D.
    if (font) {
        float view[16];
        camera.view_matrix(view);
        // view is column-major 4x4. Row 0: world-space camera-right;
        // Row 1: world-space camera-up. Same extraction as in Python.
        const float cr[3] = {view[0 * 4 + 0], view[1 * 4 + 0], view[2 * 4 + 0]};
        const float cu[3] = {view[0 * 4 + 1], view[1 * 4 + 1], view[2 * 4 + 1]};

        // For Text3D we pass the UNSCALED camera MVP: label positions
        // already have z_scale baked into their world-Z (see pos[2]
        // below). If we used the z_scaled mvp here, z would be
        // multiplied twice and — worse — the billboard quad's offset
        // through u_cam_up / u_cam_right would get stretched by z_scale
        // whenever cam_up has any world-Z component.
        float label_mvp[16];
        camera.mvp(aspect, label_mvp);

        // Tick labels on axes.
        ctx->set_depth_test(true);
        ctx->set_blend(true);
        text3d_->begin(ctx, label_mvp, cr, cu, font);

        const Color4 label_color{0.8f, 0.8f, 0.8f, 1.0f};
        const double data_size = std::sqrt(
            (hi[0] - lo[0]) * (hi[0] - lo[0]) +
            (hi[1] - lo[1]) * (hi[1] - lo[1]) +
            (hi[2] - lo[2]) * (hi[2] - lo[2]));
        const float text_size = (float)(data_size * 0.02);
        const double offset = data_size * 0.03;

        for (int axis = 0; axis < 3; ++axis) {
            const double axis_lo = lo[axis];
            const double axis_hi = hi[axis];
            const std::vector<double> ticks = axes::nice_ticks(axis_lo, axis_hi, 6);
            for (double t : ticks) {
                float pos[3] = {
                    (float)(lo[0] * x_scale),
                    (float)(lo[1] * y_scale),
                    (float)(lo[2] * z_scale),
                };
                if (axis == 2) {
                    pos[axis] = (float)(t * z_scale);
                } else if (axis == 1) {
                    pos[axis] = (float)(t * y_scale);
                } else {
                    pos[axis] = (float)(t * x_scale);
                }
                // Offset label away from plot volume.
                if (axis == 0) pos[1] -= (float)offset;  // X axis: offset in -Y
                else if (axis == 1) pos[0] -= (float)offset;  // Y axis: offset in -X
                else pos[0] -= (float)offset;                 // Z axis: offset in -X

                text3d_->draw(axes::format_tick(t), pos,
                              label_color.r, label_color.g,
                              label_color.b, label_color.a,
                              text_size,
                              tgfx::Text3DRenderer::Anchor::Center);
            }
        }

        const std::string* axis_labels[3] = {
            &data.x_label,
            &data.y_label,
            &data.z_label,
        };
        const float label_size = text_size * 1.12f;
        for (int axis = 0; axis < 3; ++axis) {
            if (axis_labels[axis]->empty()) continue;

            float pos[3] = {
                (float)(lo[0] * x_scale),
                (float)(lo[1] * y_scale),
                (float)(lo[2] * z_scale),
            };
            if (axis == 0) {
                pos[0] = (float)(hi[0] * x_scale);
                pos[1] -= (float)(offset * 1.9);
            } else if (axis == 1) {
                pos[1] = (float)(hi[1] * y_scale);
                pos[0] -= (float)(offset * 1.9);
            } else {
                pos[2] = (float)(hi[2] * z_scale);
                pos[0] -= (float)(offset * 1.9);
            }

            text3d_->draw(*axis_labels[axis], pos,
                          label_color.r, label_color.g,
                          label_color.b, label_color.a,
                          label_size,
                          tgfx::Text3DRenderer::Anchor::Center);
        }
        text3d_->end();

        // Marker value label (always on top).
        if (has_marker_ && marker_mode) {
            ctx->set_depth_test(false);
            text3d_->begin(ctx, label_mvp, cr, cu, font);
            char label_buf[64];
            std::snprintf(label_buf, sizeof(label_buf),
                          "(%.3g, %.3g, %.3g)",
                          marker_x_, marker_y_, marker_z_);
            float pos[3] = {
                (float)(marker_x_ * x_scale),
                (float)(marker_y_ * y_scale),
                (float)(marker_z_ * z_scale + data_size * 0.04),
            };
            text3d_->draw(label_buf, pos,
                          1.0f, 1.0f, 0.0f, 1.0f,
                          (float)(data_size * 0.015),
                          tgfx::Text3DRenderer::Anchor::Center);
            text3d_->end();
            ctx->set_depth_test(true);
        }
    }

    // Restore 2D state for subsequent UI rendering.
    ctx->set_depth_test(false);
}

// ---------------------------------------------------------------------------
// Picking (direct loop, no numpy vectorisation)
// ---------------------------------------------------------------------------

std::optional<PickResult3D> PlotEngine3D::pick(float mx, float my) const {
    const float aspect = vw_ / std::max(vh_, 1.0f);
    float mvp[16];
    compute_mvp_(aspect, mvp);

    // Collect all data points (lines + scatter + surface grid) into one
    // flat buffer — allocate once then iterate.
    struct Pt { double x, y, z; };
    std::vector<Pt> pts;
    pts.reserve(128);
    for (const auto& s : data.lines) {
        if (s.z.empty()) continue;
        for (size_t i = 0; i < s.x.size(); i++) {
            pts.push_back({s.x[i], s.y[i], s.z[i]});
        }
    }
    for (const auto& s : data.scatters) {
        if (s.z.empty()) continue;
        for (size_t i = 0; i < s.x.size(); i++) {
            pts.push_back({s.x[i], s.y[i], s.z[i]});
        }
    }
    for (const auto& s : data.surfaces) {
        for (size_t i = 0; i < s.X.size(); i++) {
            pts.push_back({s.X[i], s.Y[i], s.Z[i]});
        }
    }

    if (pts.empty()) return std::nullopt;

    // Project each point, compute screen distance, keep the closest
    // within a threshold — same thresholds as Python.
    constexpr double kNearThresh = 30.0;  // px
    constexpr double kFarLimit = 50.0;    // px

    double best_dist = std::numeric_limits<double>::infinity();
    double best_depth = std::numeric_limits<double>::infinity();
    size_t best_idx = 0;
    size_t fallback_idx = 0;
    double fallback_dist = std::numeric_limits<double>::infinity();
    bool any_candidate = false;

    for (size_t i = 0; i < pts.size(); i++) {
        const double px = pts[i].x;
        const double py = pts[i].y;
        const double pz = pts[i].z;
        // mvp * (px, py, pz, 1) — column-major 4x4 * 4-vec.
        const double clip_x = mvp[0] * px + mvp[4] * py + mvp[8] * pz + mvp[12];
        const double clip_y = mvp[1] * px + mvp[5] * py + mvp[9] * pz + mvp[13];
        const double clip_z = mvp[2] * px + mvp[6] * py + mvp[10] * pz + mvp[14];
        const double clip_w = mvp[3] * px + mvp[7] * py + mvp[11] * pz + mvp[15];

        if (clip_w <= 0.001) continue;
        const double ndc_x = clip_x / clip_w;
        const double ndc_y = clip_y / clip_w;
        const double ndc_z = clip_z / clip_w;

        // NDC → viewport pixel. Clip-space Y points down (Vulkan-native;
        // OpenGL matches via glClipControl(GL_UPPER_LEFT)), so pixel_y
        // grows together with ndc_y — no sign flip here.
        const double spx = vx_ + (ndc_x * 0.5 + 0.5) * vw_;
        const double spy = vy_ + (ndc_y * 0.5 + 0.5) * vh_;
        const double d = std::sqrt((spx - mx) * (spx - mx) + (spy - my) * (spy - my));

        if (d < fallback_dist) {
            fallback_dist = d;
            fallback_idx = i;
        }
        if (d < kNearThresh) {
            any_candidate = true;
            if (ndc_z < best_depth) {
                best_depth = ndc_z;
                best_dist = d;
                best_idx = i;
            }
        }
    }

    if (any_candidate) {
        return PickResult3D{pts[best_idx].x, pts[best_idx].y, pts[best_idx].z,
                            best_dist};
    }
    if (fallback_dist > kFarLimit) return std::nullopt;
    return PickResult3D{pts[fallback_idx].x, pts[fallback_idx].y, pts[fallback_idx].z,
                        fallback_dist};
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PlotEngine3D::on_mouse_down(float x, float y, tcbase::MouseButton button) {
    if (button == tcbase::MouseButton::RIGHT) {
        auto res = pick(x, y);
        if (res) {
            std::cout << "[Pick] x=" << res->x << "  y=" << res->y
                      << "  z=" << res->z
                      << "  (dist=" << res->screen_dist_px << "px)\n";
        } else {
            std::cout << "[Pick] no point nearby\n";
        }
        return true;
    }
    if (button == tcbase::MouseButton::LEFT ||
        button == tcbase::MouseButton::MIDDLE) {
        dragging_ = true;
        drag_button_ = button;
        drag_start_x_ = x;
        drag_start_y_ = y;
        return true;
    }
    return false;
}

void PlotEngine3D::on_mouse_move(float x, float y) {
    if (marker_mode && !dragging_) {
        auto res = pick(x, y);
        if (res) {
            has_marker_ = true;
            marker_x_ = res->x;
            marker_y_ = res->y;
            marker_z_ = res->z;
        } else {
            has_marker_ = false;
        }
    }

    if (!dragging_) return;
    const float dx = x - drag_start_x_;
    const float dy = y - drag_start_y_;
    drag_start_x_ = x;
    drag_start_y_ = y;

    if (drag_button_ == tcbase::MouseButton::LEFT) {
        camera.orbit(-dx * 0.005f, dy * 0.005f);
    } else if (drag_button_ == tcbase::MouseButton::MIDDLE) {
        camera.pan(-dx, dy);
    }
}

void PlotEngine3D::on_mouse_up(float /*x*/, float /*y*/,
                                tcbase::MouseButton /*button*/) {
    dragging_ = false;
}

bool PlotEngine3D::on_mouse_wheel(float /*x*/, float /*y*/, float dy) {
    const float factor = (dy > 0) ? 0.9f : 1.0f / 0.9f;
    camera.zoom(factor);
    return true;
}

}  // namespace tcplot
