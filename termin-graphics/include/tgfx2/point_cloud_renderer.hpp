#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <termin/geom/color.hpp>
#include <termin/geom/vec3.hpp>

#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

    class IRenderDevice;
    class RenderContext2;

    /** One GPU instance in a point cloud.
     *
     * color is renderer-working-space linear RGBA. size_scale multiplies the
     * draw-wide PointCloudStyle::size_px value.
     */
    struct PointCloudPoint {
        termin::Vec3f position{};
        float size_scale = 1.0f;
        termin::LinearColor color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    static_assert(std::is_standard_layout_v<PointCloudPoint>);
    static_assert(std::is_trivially_copyable_v<PointCloudPoint>);
    static_assert(sizeof(PointCloudPoint) == 8 * sizeof(float));

    enum class PointCloudShape : uint8_t {
        Square,
        Circle,
    };

    struct PointCloudStyle {
        float size_px = 3.0f;
        termin::LinearColor tint{1.0f, 1.0f, 1.0f, 1.0f};
        PointCloudShape shape = PointCloudShape::Circle;
        bool depth_test = true;
        bool depth_write = true;
    };

    struct PointCloudDrawParams {
        // Column-major view-projection matrix.
        std::array<float, 16> view_projection{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    };

    /** Persistent GPU point data.
     *
     * Upload when a generated cloud changes, then reuse it for every draw.
     * release() must be called while the owning graphics device is alive.
     */
    class TGFX2_TYPE_API PointCloud {
    private:
        BufferHandle instance_buffer_{};
        IRenderDevice* device_ = nullptr;
        uint64_t capacity_bytes_ = 0;
        uint32_t point_count_ = 0;
        termin::Vec3f bounds_min_{};
        termin::Vec3f bounds_max_{};
        bool has_bounds_ = false;

    public:
        PointCloud() = default;
        ~PointCloud() = default;

        PointCloud(const PointCloud&) = delete;
        PointCloud& operator=(const PointCloud&) = delete;
        PointCloud(PointCloud&&) = delete;
        PointCloud& operator=(PointCloud&&) = delete;

        bool upload(RenderContext2& ctx, std::span<const PointCloudPoint> points);
        void clear() noexcept;
        void release(RenderContext2& ctx);
        void release(IRenderDevice& device);

        uint32_t point_count() const noexcept {
            return point_count_;
        }
        bool empty() const noexcept {
            return point_count_ == 0;
        }
        bool has_bounds() const noexcept {
            return has_bounds_;
        }
        termin::Vec3f bounds_min() const noexcept {
            return bounds_min_;
        }
        termin::Vec3f bounds_max() const noexcept {
            return bounds_max_;
        }

    private:
        friend class PointCloudRenderer;
    };

    /** Cross-backend point renderer using instanced screen-space quads.
     *
     * Hardware point primitives are deliberately avoided: portable point size
     * support is inconsistent, and WebGPU commonly restricts them to one pixel.
     */
    class TGFX2_TYPE_API PointCloudRenderer {
    private:
        BufferHandle corner_buffer_{};
        ShaderHandle vertex_shader_{};
        ShaderHandle fragment_shader_{};
        tc_shader_handle shader_handle_ = tc_shader_handle_invalid();
        IRenderDevice* device_ = nullptr;

    public:
        PointCloudRenderer() = default;
        ~PointCloudRenderer() = default;

        PointCloudRenderer(const PointCloudRenderer&) = delete;
        PointCloudRenderer& operator=(const PointCloudRenderer&) = delete;
        PointCloudRenderer(PointCloudRenderer&&) = delete;
        PointCloudRenderer& operator=(PointCloudRenderer&&) = delete;

        void draw(RenderContext2& ctx,
                  const PointCloud& cloud,
                  const PointCloudStyle& style,
                  const PointCloudDrawParams& params);
        void release(RenderContext2& ctx);
        void release(IRenderDevice& device);

    private:
        bool ensure_resources(RenderContext2& ctx);
    };

} // namespace tgfx
