#include "tgfx2/world_tube_line_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "line_renderer_common.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
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
            LinePoint3 p0;
            float width;
            LinePoint3 p1;
            float pad;
        };

        struct TubeCapInstance {
            LinePoint3 center;
            float width;
            LinePoint3 neighbor;
            float pad;
        };

        static_assert(sizeof(TubeSegmentInstance) == 8 * sizeof(float),
                      "TubeSegmentInstance layout drift - shader and C++ disagree");
        static_assert(sizeof(TubeCapInstance) == 8 * sizeof(float),
                      "TubeCapInstance layout drift - shader and C++ disagree");

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
            layout.attribute_count = 3;
            layout.attributes[0] = vertex_attr(1, VertexFormat::Float3, offsetof(TubeSegmentInstance, p0));
            layout.attributes[1] = vertex_attr(2, VertexFormat::Float, offsetof(TubeSegmentInstance, width));
            layout.attributes[2] = vertex_attr(3, VertexFormat::Float3, offsetof(TubeSegmentInstance, p1));
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
            layout.attribute_count = 3;
            layout.attributes[0] = vertex_attr(1, VertexFormat::Float3, offsetof(TubeCapInstance, center));
            layout.attributes[1] = vertex_attr(2, VertexFormat::Float, offsetof(TubeCapInstance, width));
            layout.attributes[2] = vertex_attr(3, VertexFormat::Float3, offsetof(TubeCapInstance, neighbor));
            return layout;
        }

        constexpr float kPi = 3.14159265358979323846f;
        std::vector<TubeCornerVertex> build_body_template(int sides) {
            sides = std::clamp(sides, 3, 32);
            std::vector<TubeCornerVertex> vertices;
            vertices.reserve(static_cast<size_t>(sides) * 6);

            auto ring = [sides](int index) {
                const float t = (2.0f * kPi * static_cast<float>(index)) / static_cast<float>(sides);
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
                const float t = (2.0f * kPi * static_cast<float>(index)) / static_cast<float>(sides);
                return std::array<float, 2>{std::cos(t), std::sin(t)};
            };

            for (int i = 0; i < sides; ++i) {
                const auto a = ring(i);
                const auto b = ring((i + 1) % sides);
                // Body and cap use one planned vertex shader. Endpoint values
                // 2/3 identify cap centre/rim vertices to the tube provider.
                vertices.push_back({2.0f, 0.0f, 0.0f});
                vertices.push_back({3.0f, b[0], b[1]});
                vertices.push_back({3.0f, a[0], a[1]});
            }
            return vertices;
        }

        bool bind_tube_line_shader(RenderContext2& ctx,
                                   const tc_shader* shader_layout,
                                   ShaderHandle vertex_shader,
                                   ShaderHandle fragment_shader,
                                   const std::function<bool(RenderContext2&, const tc_shader*)>& bind_resources) {
            ctx.bind_shader(vertex_shader, fragment_shader);
            ctx.use_shader_resource_layout(shader_layout);
            if (bind_resources && !bind_resources(ctx, shader_layout)) {
                return false;
            }
            return true;
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
                device, vertices.data(), sizeof(TubeCornerVertex) * vertices.size());
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
                device, vertices.data(), sizeof(TubeCapCornerVertex) * vertices.size());
            if (!cap_corner_vbo_) {
                tc::Log::error("[WorldTubeLineRenderer] failed to create cap corner buffer");
                resources_failed_ = true;
                return false;
            }
        }

        return true;
    }

    bool WorldTubeLineRenderer::draw_polyline(RenderContext2& ctx,
                                              std::span<const LinePoint3> points,
                                              const WorldTubeLineStyle& style,
                                              const WorldTubeLineParams& params) {
        if (points.size() < 2 || style.width <= 0.0f) {
            return true;
        }

        std::vector<LinePoint3> clean_points = line_renderer::clean_points(points);

        if (clean_points.size() < 2) {
            return true;
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
            segment.p0 = p0;
            segment.width = style.width;
            segment.p1 = p1;
            segments.push_back(segment);
        }

        if (segments.empty()) {
            return true;
        }

        const LinePoint3 first = clean_points.front();
        const LinePoint3 second = clean_points[1];
        if (!line_renderer::same_point(first, second)) {
            TubeCapInstance cap{};
            cap.center = first;
            cap.width = style.width;
            cap.neighbor = second;
            caps.push_back(cap);
        }

        const LinePoint3 last = clean_points.back();
        const LinePoint3 prev = clean_points[clean_points.size() - 2];
        if (!line_renderer::same_point(last, prev)) {
            TubeCapInstance cap{};
            cap.center = last;
            cap.width = style.width;
            cap.neighbor = prev;
            caps.push_back(cap);
        }

        if (!ensure_resources(ctx, style.sides)) {
            return false;
        }
        const ShaderHandle selected_vertex_shader = params.vertex_shader;
        const ShaderHandle selected_fragment_shader = params.fragment_shader;
        const tc_shader* selected_layout = params.shader_layout;
        if (!selected_vertex_shader || !selected_fragment_shader || !selected_layout) {
            tc::Log::error("[WorldTubeLineRenderer] cannot draw body: shader state is incomplete");
            return false;
        }
        if (!body_corner_vbo_ || body_corner_count_ == 0) {
            tc::Log::error("[WorldTubeLineRenderer] cannot draw body: geometry buffer is unavailable");
            return false;
        }

        const line_renderer::UploadedInstanceStream segment_stream =
            line_renderer::upload_instance_stream(ctx, segments.data(), segments.size() * sizeof(TubeSegmentInstance));
        if (!segment_stream.buffer) {
            return false;
        }

        const VertexLayoutDesc segment_layouts[2] = {
            tube_body_corner_layout(),
            tube_segment_instance_layout(),
        };

        if (!bind_tube_line_shader(ctx,
                                   selected_layout,
                                   selected_vertex_shader,
                                   selected_fragment_shader,
                                   params.bind_resources)) {
            tc::Log::error("[WorldTubeLineRenderer] cannot draw body: resource binding failed");
            return false;
        }
        ctx.set_vertex_layouts(segment_layouts, 2);
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(body_corner_vbo_,
                                  0,
                                  segment_stream.buffer,
                                  segment_stream.offset,
                                  body_corner_count_,
                                  static_cast<uint32_t>(segments.size()));

        if (caps.empty()) {
            return true;
        }

        const line_renderer::UploadedInstanceStream cap_stream =
            line_renderer::upload_instance_stream(ctx, caps.data(), caps.size() * sizeof(TubeCapInstance));
        if (!cap_stream.buffer) {
            return false;
        }

        const VertexLayoutDesc cap_layouts[2] = {
            tube_cap_corner_layout(),
            tube_cap_instance_layout(),
        };

        if (!cap_corner_vbo_ || cap_corner_count_ == 0) {
            tc::Log::error("[WorldTubeLineRenderer] cannot draw cap: geometry buffer is unavailable");
            return false;
        }

        if (!bind_tube_line_shader(ctx,
                                   selected_layout,
                                   selected_vertex_shader,
                                   selected_fragment_shader,
                                   params.bind_resources)) {
            tc::Log::error("[WorldTubeLineRenderer] cannot draw cap: resource binding failed");
            return false;
        }
        ctx.set_vertex_layouts(cap_layouts, 2);
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(cap_corner_vbo_,
                                  0,
                                  cap_stream.buffer,
                                  cap_stream.offset,
                                  cap_corner_count_,
                                  static_cast<uint32_t>(caps.size()));
        return true;
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
        resources_failed_ = false;
        template_sides_ = 0;
    }

} // namespace tgfx
