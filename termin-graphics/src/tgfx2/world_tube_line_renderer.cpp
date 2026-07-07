#include "tgfx2/world_tube_line_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "line_renderer_common.hpp"
#include "tgfx2/render_context.hpp"

#include <tcbase/tc_log.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

namespace tgfx {
namespace {

struct TubeCornerVertex {
    float endpoint;
    float axis0;
    float axis1;
};

struct TubeCapCornerVertex {
    float center;
    float axis0;
    float axis1;
};

struct TubeSegmentInstance {
    float p0[3];
    float width;
    float p1[3];
    float pad;
    float color[4];
};

struct TubeCapInstance {
    float center[3];
    float width;
    float neighbor[3];
    float pad;
    float color[4];
};

struct TubePush {
    float view_projection[16];
    float up_hint[4];
};

VertexAttributeDesc vertex_attr(uint32_t location, VertexFormat format, size_t offset) {
    return {
        location,
        format,
        static_cast<uint32_t>(offset),
        nullptr,
    };
}

VertexLayoutDesc tube_body_corner_layout() {
    VertexLayoutDesc layout;
    layout.stride = sizeof(TubeCornerVertex);
    layout.use_shader_input_locations = true;
    layout.attribute_count = 1;
    layout.attributes[0] = vertex_attr(0, VertexFormat::Float3, 0);
    return layout;
}

VertexLayoutDesc tube_segment_instance_layout() {
    VertexLayoutDesc layout;
    layout.stride = sizeof(TubeSegmentInstance);
    layout.per_instance = true;
    layout.use_shader_input_locations = true;
    layout.attribute_count = 4;
    layout.attributes[0] = vertex_attr(1, VertexFormat::Float3, offsetof(TubeSegmentInstance, p0));
    layout.attributes[1] = vertex_attr(2, VertexFormat::Float, offsetof(TubeSegmentInstance, width));
    layout.attributes[2] = vertex_attr(3, VertexFormat::Float3, offsetof(TubeSegmentInstance, p1));
    layout.attributes[3] = vertex_attr(4, VertexFormat::Float4, offsetof(TubeSegmentInstance, color));
    return layout;
}

VertexLayoutDesc tube_cap_corner_layout() {
    VertexLayoutDesc layout;
    layout.stride = sizeof(TubeCapCornerVertex);
    layout.use_shader_input_locations = true;
    layout.attribute_count = 1;
    layout.attributes[0] = vertex_attr(0, VertexFormat::Float3, 0);
    return layout;
}

VertexLayoutDesc tube_cap_instance_layout() {
    VertexLayoutDesc layout;
    layout.stride = sizeof(TubeCapInstance);
    layout.per_instance = true;
    layout.use_shader_input_locations = true;
    layout.attribute_count = 4;
    layout.attributes[0] = vertex_attr(1, VertexFormat::Float3, offsetof(TubeCapInstance, center));
    layout.attributes[1] = vertex_attr(2, VertexFormat::Float, offsetof(TubeCapInstance, width));
    layout.attributes[2] = vertex_attr(3, VertexFormat::Float3, offsetof(TubeCapInstance, neighbor));
    layout.attributes[3] = vertex_attr(4, VertexFormat::Float4, offsetof(TubeCapInstance, color));
    return layout;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr const char* WORLD_TUBE_LINE_SHADER_UUID = "termin-engine-world-tube-line";
constexpr const char* WORLD_TUBE_LINE_CAP_SHADER_UUID =
    "termin-engine-world-tube-line-cap";
constexpr const char* WORLD_TUBE_LINE_LIT_SHADER_UUID =
    "termin-engine-world-tube-line-lit";
constexpr const char* TUBE_LINE_DRAW_RESOURCE = "tube_line_draw";

std::vector<TubeCornerVertex> build_body_template(int sides) {
    sides = std::clamp(sides, 3, 32);
    std::vector<TubeCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(sides) * 6);

    auto ring = [sides](int index) {
        const float t = (2.0f * kPi * static_cast<float>(index))
            / static_cast<float>(sides);
        return std::array<float, 2>{std::cos(t), std::sin(t)};
    };

    for (int i = 0; i < sides; ++i) {
        const auto a = ring(i);
        const auto b = ring((i + 1) % sides);
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({1.0f, b[0], b[1]});
        vertices.push_back({1.0f, a[0], a[1]});
        vertices.push_back({0.0f, a[0], a[1]});
        vertices.push_back({0.0f, b[0], b[1]});
        vertices.push_back({1.0f, b[0], b[1]});
    }
    return vertices;
}

std::vector<TubeCapCornerVertex> build_cap_template(int sides) {
    sides = std::clamp(sides, 3, 32);
    std::vector<TubeCapCornerVertex> vertices;
    vertices.reserve(static_cast<size_t>(sides) * 3);

    auto ring = [sides](int index) {
        const float t = (2.0f * kPi * static_cast<float>(index))
            / static_cast<float>(sides);
        return std::array<float, 2>{std::cos(t), std::sin(t)};
    };

    for (int i = 0; i < sides; ++i) {
        const auto a = ring(i);
        const auto b = ring((i + 1) % sides);
        vertices.push_back({1.0f, 0.0f, 0.0f});
        vertices.push_back({0.0f, b[0], b[1]});
        vertices.push_back({0.0f, a[0], a[1]});
    }
    return vertices;
}

void bind_tube_line_shader(RenderContext2& ctx,
                           const tc_shader* shader_layout,
                           ShaderHandle vertex_shader,
                           ShaderHandle fragment_shader,
                           const TubePush& draw_data,
                           const std::function<void(RenderContext2&, const tc_shader*)>& bind_resources) {
    ctx.bind_shader(vertex_shader, fragment_shader);
    ctx.use_shader_resource_layout(shader_layout);
    if (bind_resources) {
        bind_resources(ctx, shader_layout);
    }
    ctx.bind_uniform_data(
        TUBE_LINE_DRAW_RESOURCE,
        &draw_data,
        static_cast<uint32_t>(sizeof(draw_data)));
}

} // namespace

bool WorldTubeLineRenderer::ensure_resources(RenderContext2& ctx, int sides) {
    if (resources_failed_) {
        return false;
    }

    sides = std::clamp(sides, 3, 32);
    IRenderDevice& device = ctx.device();

    if (template_sides_ != sides) {
        if (body_corner_vbo_) {
            device.destroy(body_corner_vbo_);
            body_corner_vbo_ = {};
        }
        if (cap_corner_vbo_) {
            device.destroy(cap_corner_vbo_);
            cap_corner_vbo_ = {};
        }
        body_corner_count_ = 0;
        cap_corner_count_ = 0;
        template_sides_ = sides;
    }

    if (!body_corner_vbo_) {
        std::vector<TubeCornerVertex> vertices = build_body_template(sides);
        body_corner_count_ = static_cast<uint32_t>(vertices.size());
        body_corner_vbo_ = line_renderer::create_static_vertex_buffer(
            device,
            vertices.data(),
            sizeof(TubeCornerVertex) * vertices.size());
        if (!body_corner_vbo_) {
            tc::Log::error("[WorldTubeLineRenderer] failed to create body corner buffer");
            resources_failed_ = true;
            return false;
        }
    }

    if (!cap_corner_vbo_) {
        std::vector<TubeCapCornerVertex> vertices = build_cap_template(sides);
        cap_corner_count_ = static_cast<uint32_t>(vertices.size());
        cap_corner_vbo_ = line_renderer::create_static_vertex_buffer(
            device,
            vertices.data(),
            sizeof(TubeCapCornerVertex) * vertices.size());
        if (!cap_corner_vbo_) {
            tc::Log::error("[WorldTubeLineRenderer] failed to create cap corner buffer");
            resources_failed_ = true;
            return false;
        }
    }

    const bool body_ready = line_renderer::ensure_shader_pair(
        device,
        body_shader_handle_,
        WORLD_TUBE_LINE_SHADER_UUID,
        "body",
        "WorldTubeLineRenderer",
        body_vertex_shader_,
        body_fragment_shader_);
    const bool cap_ready = line_renderer::ensure_shader_pair(
        device,
        cap_shader_handle_,
        WORLD_TUBE_LINE_CAP_SHADER_UUID,
        "cap",
        "WorldTubeLineRenderer",
        cap_vertex_shader_,
        cap_fragment_shader_);
    const bool lit_ready = line_renderer::ensure_fragment_shader(
        device,
        lit_shader_handle_,
        WORLD_TUBE_LINE_LIT_SHADER_UUID,
        "lit fragment",
        "WorldTubeLineRenderer",
        lit_fragment_shader_);

    if (!body_ready || !cap_ready || !lit_ready) {
        resources_failed_ = true;
        return false;
    }

    if (!tc_shader_get(body_shader_handle_) || !tc_shader_get(cap_shader_handle_)) {
        tc::Log::error("[WorldTubeLineRenderer] required shader layout is unavailable");
        resources_failed_ = true;
        return false;
    }

    return true;
}

void WorldTubeLineRenderer::draw_polyline(
        RenderContext2& ctx,
        std::span<const LinePoint3> points,
        const WorldTubeLineStyle& style,
        const WorldTubeLineParams& params) {
    if (points.size() < 2 || style.width <= 0.0f) {
        return;
    }

    std::vector<LinePoint3> clean_points = line_renderer::clean_points(points);

    if (clean_points.size() < 2) {
        return;
    }

    std::vector<TubeSegmentInstance> segments;
    std::vector<TubeCapInstance> caps;
    segments.reserve(clean_points.size() - 1);
    for (size_t i = 1; i < clean_points.size(); ++i) {
        const LinePoint3 p0 = clean_points[i - 1];
        const LinePoint3 p1 = clean_points[i];
        if (line_renderer::same_point(p0, p1)) {
            continue;
        }

        TubeSegmentInstance segment{};
        segment.p0[0] = p0.x;
        segment.p0[1] = p0.y;
        segment.p0[2] = p0.z;
        segment.width = style.width;
        segment.p1[0] = p1.x;
        segment.p1[1] = p1.y;
        segment.p1[2] = p1.z;
        std::memcpy(segment.color, style.color.data(), sizeof(segment.color));
        segments.push_back(segment);
    }

    if (segments.empty()) {
        return;
    }

    const LinePoint3 first = clean_points.front();
    const LinePoint3 second = clean_points[1];
    if (!line_renderer::same_point(first, second)) {
        TubeCapInstance cap{};
        cap.center[0] = first.x;
        cap.center[1] = first.y;
        cap.center[2] = first.z;
        cap.width = style.width;
        cap.neighbor[0] = second.x;
        cap.neighbor[1] = second.y;
        cap.neighbor[2] = second.z;
        std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
        caps.push_back(cap);
    }

    const LinePoint3 last = clean_points.back();
    const LinePoint3 prev = clean_points[clean_points.size() - 2];
    if (!line_renderer::same_point(last, prev)) {
        TubeCapInstance cap{};
        cap.center[0] = last.x;
        cap.center[1] = last.y;
        cap.center[2] = last.z;
        cap.width = style.width;
        cap.neighbor[0] = prev.x;
        cap.neighbor[1] = prev.y;
        cap.neighbor[2] = prev.z;
        std::memcpy(cap.color, style.color.data(), sizeof(cap.color));
        caps.push_back(cap);
    }

    if (!ensure_resources(ctx, style.sides)) {
        return;
    }
    const ShaderHandle body_selected_vertex_shader = params.body_vertex_shader
        ? params.body_vertex_shader
        : body_vertex_shader_;
    const ShaderHandle body_selected_fragment_shader = params.body_fragment_shader
        ? params.body_fragment_shader
        : (params.fragment_shader
            ? params.fragment_shader
            : (params.lighting_enabled ? lit_fragment_shader_ : body_fragment_shader_));
    const tc_shader* body_selected_layout = params.body_shader_layout
        ? params.body_shader_layout
        : tc_shader_get(body_shader_handle_);
    if (!body_selected_vertex_shader || !body_selected_fragment_shader || !body_selected_layout) {
        tc::Log::error("[WorldTubeLineRenderer] cannot draw body: shader state is incomplete");
        return;
    }
    if (!body_corner_vbo_ || body_corner_count_ == 0) {
        tc::Log::error("[WorldTubeLineRenderer] cannot draw body: geometry buffer is unavailable");
        return;
    }

    const line_renderer::UploadedInstanceStream segment_stream = line_renderer::upload_instance_stream(
        ctx,
        segments.data(),
        segments.size() * sizeof(TubeSegmentInstance));
    if (!segment_stream.buffer) {
        return;
    }

    TubePush push{};
    std::memcpy(push.view_projection,
                params.view_projection.data(),
                sizeof(push.view_projection));
    push.up_hint[0] = style.up_hint.x;
    push.up_hint[1] = style.up_hint.y;
    push.up_hint[2] = style.up_hint.z;
    push.up_hint[3] = 0.0f;

    const VertexLayoutDesc segment_layouts[2] = {
        tube_body_corner_layout(),
        tube_segment_instance_layout(),
    };

    bind_tube_line_shader(
        ctx,
        body_selected_layout,
        body_selected_vertex_shader,
        body_selected_fragment_shader,
        push,
        params.bind_resources);
    ctx.set_vertex_layouts(segment_layouts, 2);
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.draw_arrays_instanced(
        body_corner_vbo_,
        0,
        segment_stream.buffer,
        segment_stream.offset,
        body_corner_count_,
        static_cast<uint32_t>(segments.size()));

    if (caps.empty()) {
        return;
    }

    const line_renderer::UploadedInstanceStream cap_stream = line_renderer::upload_instance_stream(
        ctx,
        caps.data(),
        caps.size() * sizeof(TubeCapInstance));
    if (!cap_stream.buffer) {
        return;
    }

    const VertexLayoutDesc cap_layouts[2] = {
        tube_cap_corner_layout(),
        tube_cap_instance_layout(),
    };

    const ShaderHandle cap_selected_vertex_shader = params.cap_vertex_shader
        ? params.cap_vertex_shader
        : cap_vertex_shader_;
    const ShaderHandle cap_selected_fragment_shader = params.cap_fragment_shader
        ? params.cap_fragment_shader
        : (params.fragment_shader
            ? params.fragment_shader
            : (params.lighting_enabled ? lit_fragment_shader_ : cap_fragment_shader_));
    const tc_shader* cap_selected_layout = params.cap_shader_layout
        ? params.cap_shader_layout
        : tc_shader_get(cap_shader_handle_);
    if (!cap_selected_vertex_shader || !cap_selected_fragment_shader || !cap_selected_layout) {
        tc::Log::error("[WorldTubeLineRenderer] cannot draw cap: shader state is incomplete");
        return;
    }
    if (!cap_corner_vbo_ || cap_corner_count_ == 0) {
        tc::Log::error("[WorldTubeLineRenderer] cannot draw cap: geometry buffer is unavailable");
        return;
    }

    bind_tube_line_shader(
        ctx,
        cap_selected_layout,
        cap_selected_vertex_shader,
        cap_selected_fragment_shader,
        push,
        params.bind_resources);
    ctx.set_vertex_layouts(cap_layouts, 2);
    ctx.set_topology(PrimitiveTopology::TriangleList);
    ctx.draw_arrays_instanced(
        cap_corner_vbo_,
        0,
        cap_stream.buffer,
        cap_stream.offset,
        cap_corner_count_,
        static_cast<uint32_t>(caps.size()));
}

void WorldTubeLineRenderer::release(RenderContext2& ctx) {
    IRenderDevice& device = ctx.device();
    if (body_corner_vbo_) {
        device.destroy(body_corner_vbo_);
        body_corner_vbo_ = {};
        body_corner_count_ = 0;
    }
    if (cap_corner_vbo_) {
        device.destroy(cap_corner_vbo_);
        cap_corner_vbo_ = {};
        cap_corner_count_ = 0;
    }
    body_vertex_shader_ = {};
    body_fragment_shader_ = {};
    cap_vertex_shader_ = {};
    cap_fragment_shader_ = {};
    lit_fragment_shader_ = {};
    resources_failed_ = false;
    template_sides_ = 0;
}

} // namespace tgfx
