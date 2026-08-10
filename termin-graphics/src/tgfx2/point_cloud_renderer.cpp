#include "tgfx2/point_cloud_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include <tcbase/tc_log.hpp>

#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tgfx2/vertex_layout.hpp"

extern "C" {
#include <tgfx/resources/tc_shader.h>
}

namespace tgfx {
    namespace {

        struct CornerVertex {
            float x;
            float y;
        };

        struct PointCloudDrawData {
            float view_projection[16];
            float viewport_size_shape[4];
            float tint[4];
        };

        static_assert(sizeof(PointCloudDrawData) == 24 * sizeof(float));

        constexpr std::array<CornerVertex, 6> kCorners{{
            {-1.0f, -1.0f},
            {1.0f, -1.0f},
            {1.0f, 1.0f},
            {-1.0f, -1.0f},
            {1.0f, 1.0f},
            {-1.0f, 1.0f},
        }};

        constexpr const char* kShaderUuid = "termin-engine-point-cloud";
        constexpr const char* kDrawResource = "point_cloud_draw";

        VertexAttributeDesc vertex_attr(uint32_t location,
                                        VertexFormat format,
                                        size_t offset,
                                        const char* semantic) {
            return {location, format, static_cast<uint32_t>(offset), intern_vertex_semantic(semantic)};
        }

        VertexLayoutDesc corner_layout() {
            VertexLayoutDesc layout;
            layout.stride = sizeof(CornerVertex);
            layout.attribute_count = 1;
            layout.attributes[0] = vertex_attr(0, VertexFormat::Float2, 0, "texcoord0");
            return layout;
        }

        VertexLayoutDesc point_layout() {
            VertexLayoutDesc layout;
            layout.stride = sizeof(PointCloudPoint);
            layout.per_instance = true;
            layout.attribute_count = 3;
            layout.attributes[0] =
                vertex_attr(1, VertexFormat::Float3, offsetof(PointCloudPoint, position), "position0");
            layout.attributes[1] =
                vertex_attr(2, VertexFormat::Float, offsetof(PointCloudPoint, size_scale), "texcoord1");
            layout.attributes[2] =
                vertex_attr(3, VertexFormat::Float4, offsetof(PointCloudPoint, color), "color0");
            return layout;
        }

        bool finite_point(const PointCloudPoint& point) {
            return std::isfinite(point.position.x) && std::isfinite(point.position.y) &&
                   std::isfinite(point.position.z) && std::isfinite(point.size_scale) && point.size_scale >= 0.0f &&
                   std::isfinite(point.color.r) && std::isfinite(point.color.g) &&
                   std::isfinite(point.color.b) && std::isfinite(point.color.a);
        }

        bool finite_draw_params(const PointCloudStyle& style, const PointCloudDrawParams& params) {
            const bool finite_tint = std::isfinite(style.tint.r) && std::isfinite(style.tint.g) &&
                                     std::isfinite(style.tint.b) && std::isfinite(style.tint.a);
            const bool finite_matrix = std::all_of(
                params.view_projection.begin(), params.view_projection.end(), [](float value) {
                    return std::isfinite(value);
                });
            return std::isfinite(style.size_px) && finite_tint && finite_matrix;
        }

    } // namespace

    bool PointCloud::upload(RenderContext2& ctx, std::span<const PointCloudPoint> points) {
        if (points.size() > std::numeric_limits<uint32_t>::max()) {
            tc::Log::error("PointCloud::upload: %zu points exceed the 32-bit draw limit", points.size());
            return false;
        }

        IRenderDevice& device = ctx.device();
        if (device_ && device_ != &device) {
            tc::Log::error("PointCloud::upload: cloud belongs to a different render device; release it first");
            return false;
        }

        if (points.empty()) {
            clear();
            device_ = &device;
            return true;
        }

        termin::Vec3f min_point = points.front().position;
        termin::Vec3f max_point = points.front().position;
        for (const PointCloudPoint& point : points) {
            if (!finite_point(point)) {
                tc::Log::error("PointCloud::upload: point data contains non-finite values or a negative size");
                return false;
            }
            min_point.x = std::min(min_point.x, point.position.x);
            min_point.y = std::min(min_point.y, point.position.y);
            min_point.z = std::min(min_point.z, point.position.z);
            max_point.x = std::max(max_point.x, point.position.x);
            max_point.y = std::max(max_point.y, point.position.y);
            max_point.z = std::max(max_point.z, point.position.z);
        }

        const uint64_t byte_size = points.size_bytes();
        if (!instance_buffer_ || byte_size > capacity_bytes_) {
            BufferDesc desc;
            desc.size = byte_size;
            desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
            BufferHandle replacement = device.create_buffer(desc);
            if (!replacement) {
                tc::Log::error("PointCloud::upload: failed to allocate a %llu-byte instance buffer",
                               static_cast<unsigned long long>(byte_size));
                return false;
            }
            if (instance_buffer_) {
                ctx.defer_destroy(instance_buffer_);
            }
            instance_buffer_ = replacement;
            capacity_bytes_ = byte_size;
        }

        device.upload_buffer(
            instance_buffer_,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(points.data()), points.size_bytes()));
        device_ = &device;
        point_count_ = static_cast<uint32_t>(points.size());
        bounds_min_ = min_point;
        bounds_max_ = max_point;
        has_bounds_ = true;
        return true;
    }

    void PointCloud::clear() noexcept {
        point_count_ = 0;
        bounds_min_ = {};
        bounds_max_ = {};
        has_bounds_ = false;
    }

    void PointCloud::release(RenderContext2& ctx) {
        IRenderDevice& device = ctx.device();
        if (device_ && device_ != &device) {
            tc::Log::error("PointCloud::release: cloud belongs to a different render device");
            return;
        }
        if (instance_buffer_) {
            device.destroy(instance_buffer_);
        }
        instance_buffer_ = {};
        device_ = nullptr;
        capacity_bytes_ = 0;
        clear();
    }

    bool PointCloudRenderer::ensure_resources(RenderContext2& ctx) {
        IRenderDevice& device = ctx.device();
        if (device_ && device_ != &device) {
            tc::Log::error("PointCloudRenderer: renderer belongs to a different render device; release it first");
            return false;
        }
        device_ = &device;

        if (!corner_buffer_) {
            BufferDesc desc;
            desc.size = sizeof(kCorners);
            desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
            corner_buffer_ = device.create_buffer(desc);
            if (!corner_buffer_) {
                tc::Log::error("PointCloudRenderer: failed to allocate the billboard corner buffer");
                return false;
            }
            device.upload_buffer(
                corner_buffer_,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(kCorners.data()), sizeof(kCorners)));
        }

        if (!vertex_shader_ || !fragment_shader_) {
            if (tc_shader_handle_is_invalid(shader_handle_)) {
                shader_handle_ = register_builtin_shader_from_catalog(kShaderUuid);
            }
            tc_shader* shader = tc_shader_get(shader_handle_);
            if (!shader || !termin::tc_shader_ensure_tgfx2(shader, &device, &vertex_shader_, &fragment_shader_)) {
                tc::Log::error("PointCloudRenderer: failed to create the built-in shader");
                vertex_shader_ = {};
                fragment_shader_ = {};
                return false;
            }
        }
        return true;
    }

    void PointCloudRenderer::draw(RenderContext2& ctx,
                                  const PointCloud& cloud,
                                  const PointCloudStyle& style,
                                  const PointCloudDrawParams& params) {
        if (cloud.empty() || style.size_px <= 0.0f) {
            return;
        }
        if (cloud.device_ != &ctx.device() || !cloud.instance_buffer_) {
            tc::Log::error("PointCloudRenderer::draw: cloud is not uploaded to this render device");
            return;
        }
        if (!finite_draw_params(style, params)) {
            tc::Log::error("PointCloudRenderer::draw: size_px, tint, and view_projection must be finite");
            return;
        }
        if (!ensure_resources(ctx)) {
            return;
        }

        PointCloudDrawData draw_data{};
        std::memcpy(draw_data.view_projection, params.view_projection.data(), sizeof(draw_data.view_projection));
        draw_data.viewport_size_shape[0] = static_cast<float>(std::max(ctx.viewport_width(), 1));
        draw_data.viewport_size_shape[1] = static_cast<float>(std::max(ctx.viewport_height(), 1));
        draw_data.viewport_size_shape[2] = style.size_px;
        draw_data.viewport_size_shape[3] = style.shape == PointCloudShape::Circle ? 1.0f : 0.0f;
        draw_data.tint[0] = style.tint.r;
        draw_data.tint[1] = style.tint.g;
        draw_data.tint[2] = style.tint.b;
        draw_data.tint[3] = style.tint.a;

        ctx.set_depth_test(style.depth_test);
        ctx.set_depth_write(style.depth_test && style.depth_write);
        ctx.set_blend(false);
        ctx.set_cull(CullMode::None);
        ctx.bind_shader(vertex_shader_, fragment_shader_);
        ctx.use_shader_resource_layout(tc_shader_get(shader_handle_));
        ctx.bind_uniform_data(kDrawResource, &draw_data, static_cast<uint32_t>(sizeof(draw_data)));

        const VertexLayoutDesc layouts[2] = {corner_layout(), point_layout()};
        ctx.set_vertex_layouts(layouts, 2);
        ctx.set_topology(PrimitiveTopology::TriangleList);
        ctx.draw_arrays_instanced(corner_buffer_, cloud.instance_buffer_, 6, cloud.point_count_);
    }

    void PointCloudRenderer::release(RenderContext2& ctx) {
        IRenderDevice& device = ctx.device();
        if (device_ && device_ != &device) {
            tc::Log::error("PointCloudRenderer::release: renderer belongs to a different render device");
            return;
        }
        if (corner_buffer_) {
            device.destroy(corner_buffer_);
        }
        corner_buffer_ = {};
        vertex_shader_ = {};
        fragment_shader_ = {};
        device_ = nullptr;
    }

} // namespace tgfx
