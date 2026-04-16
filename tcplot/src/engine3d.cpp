// engine3d.cpp - Host-agnostic 3D plot engine. Port of engine3d.py.
//
// Layout of this file, top to bottom:
//   - Shared shader source (vert/frag with jet colormap).
//   - Internal helpers (vertex layout builder, draw_tc_mesh).
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
#include <utility>

#include <tgfx/tgfx_types.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/tc_gpu.h>
#include <tgfx/tc_gpu_context.h>
#include <tgfx/tc_gpu_share_group.h>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/handles.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/text3d_renderer.hpp>
#include <tgfx2/vertex_layout.hpp>

#include "tcplot/axes.hpp"

namespace tcplot {

namespace {

// Same 3D plot shader as engine3d.py. Position in vec3, per-vertex
// RGBA color in vec4 (loc 1). If u_use_jet != 0, the fragment stage
// replaces the per-vertex RGB with a jet colormap indexed by the
// normalised Z coordinate.
constexpr const char* kVertSrc = R"(#version 330 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec4 a_color;
uniform mat4 u_mvp;
uniform float u_z_min;
uniform float u_z_max;
uniform int u_use_jet;
out vec4 v_color;
out float v_z_norm;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
    float z_range = u_z_max - u_z_min;
    v_z_norm = (z_range > 0.0) ? (a_position.z - u_z_min) / z_range : 0.5;
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
in vec4 v_color;
in float v_z_norm;
uniform int u_use_jet;
out vec4 frag_color;

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

void main() {
    if (u_use_jet != 0) {
        frag_color = vec4(jet(v_z_norm), v_color.a);
    } else {
        frag_color = v_color;
    }
}
)";

// Build the canonical "pos+color" vertex layout (3 floats + 4 floats,
// stride 28). Reused across all meshes built by this engine.
tc_vertex_layout pos_color_layout() {
    tc_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TC_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "color",    4, TC_ATTRIB_FLOAT32, 1);
    return layout;
}

// Draw a TcMesh through a tgfx2 RenderContext. Uploads VBO/EBO on
// first call (via legacy tc_mesh_upload_gpu), wraps them as external
// tgfx2 buffer handles, pushes the vertex layout, and emits one
// indexed draw. Matches the logic in tgfx2_bindings.cpp::draw_tc_mesh.
void draw_tc_mesh(tgfx2::RenderContext2& ctx, termin::TcMesh& mesh_wrapper) {
    tc_mesh* mesh = tc_mesh_get(mesh_wrapper.handle);
    if (!mesh) return;

    auto* gl_dev = dynamic_cast<tgfx2::OpenGLRenderDevice*>(&ctx.device());
    if (!gl_dev) return;

    if (tc_mesh_upload_gpu(mesh) == 0) return;

    tc_gpu_context* gctx = tc_gpu_get_context();
    if (!gctx || !gctx->share_group) return;
    tc_gpu_mesh_data_slot* slot = tc_gpu_share_group_mesh_data_slot(
        gctx->share_group, mesh->header.pool_index);
    if (!slot || slot->vbo == 0 || slot->ebo == 0) return;

    tgfx2::BufferDesc vb_desc;
    vb_desc.size = static_cast<uint64_t>(mesh->vertex_count) *
                   static_cast<uint64_t>(mesh->layout.stride);
    vb_desc.usage = tgfx2::BufferUsage::Vertex;
    tgfx2::BufferHandle vbo = gl_dev->register_external_buffer(slot->vbo, vb_desc);

    tgfx2::BufferDesc ib_desc;
    ib_desc.size = static_cast<uint64_t>(mesh->index_count) * sizeof(uint32_t);
    ib_desc.usage = tgfx2::BufferUsage::Index;
    tgfx2::BufferHandle ibo = gl_dev->register_external_buffer(slot->ebo, ib_desc);

    tgfx2::VertexBufferLayout layout;
    layout.stride = mesh->layout.stride;
    layout.attributes.reserve(mesh->layout.attrib_count);
    for (uint8_t i = 0; i < mesh->layout.attrib_count; i++) {
        const tgfx_vertex_attrib& a = mesh->layout.attribs[i];
        tgfx2::VertexAttribute va;
        va.location = a.location;
        va.offset = a.offset;
        // For engine3d we only ever emit float attributes; keep a
        // minimal switch to cover the sizes we actually use. Anything
        // else would indicate a mesh created outside our rebuild_meshes_.
        switch (a.size) {
            case 1: va.format = tgfx2::VertexFormat::Float; break;
            case 2: va.format = tgfx2::VertexFormat::Float2; break;
            case 3: va.format = tgfx2::VertexFormat::Float3; break;
            case 4: va.format = tgfx2::VertexFormat::Float4; break;
            default: va.format = tgfx2::VertexFormat::Float3; break;
        }
        layout.attributes.push_back(va);
    }

    tgfx2::PrimitiveTopology topo = (mesh->draw_mode == TC_DRAW_LINES)
        ? tgfx2::PrimitiveTopology::LineList
        : tgfx2::PrimitiveTopology::TriangleList;

    ctx.set_vertex_layout(layout);
    ctx.set_topology(topo);
    ctx.draw(vbo, ibo, static_cast<uint32_t>(mesh->index_count),
             tgfx2::IndexType::Uint32);

    gl_dev->destroy(vbo);
    gl_dev->destroy(ibo);
}

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
    : text3d_(std::make_unique<tgfx2::Text3DRenderer>()) {}

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
                            bool wireframe,
                            std::string label) {
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

void PlotEngine3D::toggle_marker_mode() {
    marker_mode = !marker_mode;
    if (!marker_mode) {
        has_marker_ = false;
    }
}

// ---------------------------------------------------------------------------
// GPU resource management
// ---------------------------------------------------------------------------

void PlotEngine3D::release_meshes_() {
    auto drop = [](std::optional<termin::TcMesh>& m) {
        if (m.has_value()) {
            m->delete_gpu();
            m.reset();
        }
    };
    drop(lines_mesh_);
    drop(scatter_mesh_);
    drop(grid_mesh_);
    for (auto& m : surface_meshes_) m.delete_gpu();
    surface_meshes_.clear();
    for (auto& m : wireframe_meshes_) m.delete_gpu();
    wireframe_meshes_.clear();
}

void PlotEngine3D::release_gpu_resources() {
    release_meshes_();
    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }
    shader_vs_id_ = 0;
    shader_fs_id_ = 0;
    shader_device_ = nullptr;
    if (text3d_) text3d_->release_gpu();
}

void PlotEngine3D::ensure_shader_(tgfx2::IRenderDevice& device) {
    if (shader_device_ == &device && shader_vs_id_ != 0 && shader_fs_id_ != 0) {
        return;
    }
    // Device changed or first call — drop old and recompile.
    if (shader_device_) {
        if (shader_vs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_vs_id_;
            shader_device_->destroy(h);
        }
        if (shader_fs_id_ != 0) {
            tgfx2::ShaderHandle h; h.id = shader_fs_id_;
            shader_device_->destroy(h);
        }
    }

    tgfx2::ShaderDesc vd;
    vd.stage = tgfx2::ShaderStage::Vertex;
    vd.source = kVertSrc;
    shader_vs_id_ = device.create_shader(vd).id;

    tgfx2::ShaderDesc fd;
    fd.stage = tgfx2::ShaderStage::Fragment;
    fd.source = kFragSrc;
    shader_fs_id_ = device.create_shader(fd).id;

    shader_device_ = &device;
}

// ---------------------------------------------------------------------------
// Mesh building
// ---------------------------------------------------------------------------

void PlotEngine3D::rebuild_meshes_() {
    release_meshes_();

    const tc_vertex_layout layout = pos_color_layout();

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
            termin::TcMesh m = termin::TcMesh::from_interleaved(
                verts.data(), verts.size() / 7,
                indices.data(), indices.size(),
                layout, "", "", TC_DRAW_LINES);
            m.upload_gpu();
            lines_mesh_ = std::move(m);
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
            termin::TcMesh m = termin::TcMesh::from_interleaved(
                verts.data(), verts.size() / 7,
                indices.data(), indices.size(),
                layout, "", "", TC_DRAW_LINES);
            m.upload_gpu();
            scatter_mesh_ = std::move(m);
        }
    }

    // --- Surfaces ---
    for (const auto& surf : data.surfaces) {
        build_surface_mesh_(surf);
    }

    // --- Grid + axes ---
    if (show_grid) {
        build_grid_mesh_(lo, hi);
    }

    dirty_ = false;
}

void PlotEngine3D::build_grid_mesh_(const double bounds_min[3],
                                     const double bounds_max[3]) {
    std::vector<float> verts;
    std::vector<uint32_t> indices;
    uint32_t idx = 0;
    const Color4 gc = styles::grid_color();

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
        const tc_vertex_layout layout = pos_color_layout();
        termin::TcMesh m = termin::TcMesh::from_interleaved(
            verts.data(), verts.size() / 7,
            indices.data(), indices.size(),
            layout, "", "", TC_DRAW_LINES);
        m.upload_gpu();
        grid_mesh_ = std::move(m);
    }
}

void PlotEngine3D::build_surface_mesh_(const SurfaceSeries& surf) {
    const uint32_t rows = surf.rows;
    const uint32_t cols = surf.cols;
    if (rows < 2 || cols < 2) return;

    const float alpha = surf.color.has_value() ? surf.color->a : 1.0f;
    double z_min = std::numeric_limits<double>::infinity();
    double z_max = -std::numeric_limits<double>::infinity();
    for (double z : surf.Z) {
        if (z < z_min) z_min = z;
        if (z > z_max) z_max = z;
    }
    const double z_range = (z_max > z_min) ? (z_max - z_min) : 1.0;

    std::vector<float> verts;
    verts.reserve((size_t)rows * cols * 7);
    for (uint32_t j = 0; j < rows; ++j) {
        for (uint32_t i = 0; i < cols; ++i) {
            const size_t idx = (size_t)j * cols + i;
            const double x = surf.X[idx];
            const double y = surf.Y[idx];
            const double z = surf.Z[idx];
            const double t = (z - z_min) / z_range;

            float r = 0, g = 0, b = 0;
            if (surf.color.has_value() && surf.wireframe) {
                r = surf.color->r;
                g = surf.color->g;
                b = surf.color->b;
            } else {
                const Color4 jc = styles::jet((float)t);
                r = jc.r; g = jc.g; b = jc.b;
            }
            push_vertex(verts, (float)x, (float)y, (float)z, {r, g, b, alpha});
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

    const tc_vertex_layout layout = pos_color_layout();

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
        termin::TcMesh m = termin::TcMesh::from_interleaved(
            verts.data(), verts.size() / 7,
            wire_indices.data(), wire_indices.size(),
            layout, "", "", TC_DRAW_LINES);
        m.upload_gpu();
        wireframe_meshes_.push_back(std::move(m));
    } else {
        termin::TcMesh m = termin::TcMesh::from_interleaved(
            verts.data(), verts.size() / 7,
            tri_indices.data(), tri_indices.size(),
            layout, "", "", TC_DRAW_TRIANGLES);
        m.upload_gpu();
        surface_meshes_.push_back(std::move(m));
    }
}

// ---------------------------------------------------------------------------
// MVP / rendering
// ---------------------------------------------------------------------------

void PlotEngine3D::compute_mvp_(float aspect, float out16[16]) const {
    float mvp[16];
    camera.mvp(aspect, mvp);

    if (std::abs(z_scale - 1.0f) < 1e-6f) {
        std::memcpy(out16, mvp, sizeof(mvp));
        return;
    }

    // Post-multiply by diag(1, 1, z_scale, 1): model matrix that scales
    // Z. Column-major: mvp * diag(1,1,s,1) = mvp with column 2 scaled.
    std::memcpy(out16, mvp, sizeof(mvp));
    for (int r = 0; r < 4; ++r) {
        out16[2 * 4 + r] *= z_scale;
    }
}

void PlotEngine3D::render(tgfx2::RenderContext2* ctx, tgfx2::FontAtlas* font) {
    if (!ctx || vw_ <= 0 || vh_ <= 0) return;

    ensure_shader_(ctx->device());
    if (dirty_) rebuild_meshes_();

    ctx->set_depth_test(true);
    ctx->set_blend(true);
    ctx->set_cull(tgfx2::CullMode::None);

    const float aspect = vw_ / std::max(vh_, 1.0f);
    float mvp[16];
    compute_mvp_(aspect, mvp);

    tgfx2::ShaderHandle vs; vs.id = shader_vs_id_;
    tgfx2::ShaderHandle fs; fs.id = shader_fs_id_;
    ctx->bind_shader(vs, fs);
    ctx->set_uniform_mat4("u_mvp", mvp, /*transpose=*/false);

    double lo[3], hi[3];
    data.data_bounds_3d(lo, hi);
    ctx->set_uniform_float("u_z_min", (float)lo[2]);
    ctx->set_uniform_float("u_z_max", (float)hi[2]);

    // Grid (no jet).
    ctx->set_uniform_int("u_use_jet", 0);
    if (grid_mesh_) draw_tc_mesh(*ctx, *grid_mesh_);

    // Opaque surfaces (jet).
    ctx->set_uniform_int("u_use_jet", 1);
    ctx->set_blend(false);
    for (auto& m : surface_meshes_) {
        draw_tc_mesh(*ctx, m);
    }

    // Wireframes on top (no depth, no jet).
    ctx->set_uniform_int("u_use_jet", 0);
    if (show_wireframe) {
        ctx->set_depth_test(false);
        ctx->set_blend(true);
        for (auto& m : wireframe_meshes_) {
            draw_tc_mesh(*ctx, m);
        }
        ctx->set_depth_test(true);
    }

    // Lines and scatter.
    if (lines_mesh_) draw_tc_mesh(*ctx, *lines_mesh_);
    if (scatter_mesh_) draw_tc_mesh(*ctx, *scatter_mesh_);

    // Marker (immediate mode cross at marker pos).
    if (has_marker_ && marker_mode) {
        ctx->bind_shader(vs, fs);
        ctx->set_uniform_mat4("u_mvp", mvp, /*transpose=*/false);
        ctx->set_uniform_int("u_use_jet", 0);
        ctx->set_depth_test(false);

        const double data_size = std::sqrt(
            (hi[0] - lo[0]) * (hi[0] - lo[0]) +
            (hi[1] - lo[1]) * (hi[1] - lo[1]) +
            (hi[2] - lo[2]) * (hi[2] - lo[2]));
        const float cs = (float)(data_size * 0.015);
        const Color4 c{1.0f, 1.0f, 0.0f, 1.0f};
        std::vector<float> verts;
        const float px = (float)marker_x_;
        const float py = (float)marker_y_;
        const float pz = (float)marker_z_;
        struct Axis { float dx, dy, dz; };
        const Axis axes_[] = {{cs, 0, 0}, {0, cs, 0}, {0, 0, cs}};
        for (const auto& a : axes_) {
            push_vertex(verts, px - a.dx, py - a.dy, pz - a.dz, c);
            push_vertex(verts, px + a.dx, py + a.dy, pz + a.dz, c);
        }
        ctx->draw_immediate_lines(verts.data(),
                                  (uint32_t)(verts.size() / 7));
        ctx->set_depth_test(true);
    }

    // Tick labels + marker value label via Text3D.
    if (font) {
        const float cam_right[3] = {
            // view rows 0 and 1 give us the camera basis in world space.
            // Build view once and read rows.
            0, 0, 0
        };
        (void)cam_right;  // placeholder — actual extraction below.

        float view[16];
        camera.view_matrix(view);
        // view is column-major 4x4. Row 0: world-space camera-right;
        // Row 1: world-space camera-up. Same extraction as in Python.
        const float cr[3] = {view[0 * 4 + 0], view[1 * 4 + 0], view[2 * 4 + 0]};
        const float cu[3] = {view[0 * 4 + 1], view[1 * 4 + 1], view[2 * 4 + 1]};

        // Tick labels on axes.
        ctx->set_depth_test(true);
        ctx->set_blend(true);
        text3d_->begin(ctx, mvp, cr, cu, font);

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
                    (float)lo[0],
                    (float)lo[1],
                    (float)(lo[2] * z_scale),
                };
                if (axis == 2) {
                    pos[axis] = (float)(t * z_scale);
                } else {
                    pos[axis] = (float)t;
                }
                // Offset label away from plot volume.
                if (axis == 0) pos[1] -= (float)offset;  // X axis: offset in -Y
                else if (axis == 1) pos[0] -= (float)offset;  // Y axis: offset in -X
                else pos[0] -= (float)offset;                 // Z axis: offset in -X

                text3d_->draw(axes::format_tick(t), pos,
                              label_color.r, label_color.g,
                              label_color.b, label_color.a,
                              text_size,
                              tgfx2::Text3DRenderer::Anchor::Center);
            }
        }
        text3d_->end();

        // Marker value label (always on top).
        if (has_marker_ && marker_mode) {
            ctx->set_depth_test(false);
            text3d_->begin(ctx, mvp, cr, cu, font);
            char label_buf[64];
            std::snprintf(label_buf, sizeof(label_buf),
                          "(%.3g, %.3g, %.3g)",
                          marker_x_, marker_y_, marker_z_);
            float pos[3] = {
                (float)marker_x_,
                (float)marker_y_,
                (float)(marker_z_ * z_scale + data_size * 0.04),
            };
            text3d_->draw(label_buf, pos,
                          1.0f, 1.0f, 0.0f, 1.0f,
                          (float)(data_size * 0.015),
                          tgfx2::Text3DRenderer::Anchor::Center);
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

        // NDC → viewport pixel. Matches Python.
        const double spx = vx_ + (ndc_x * 0.5 + 0.5) * vw_;
        const double spy = vy_ + (-ndc_y * 0.5 + 0.5) * vh_;
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
