#pragma once

#include <functional>
#include <span>

#include "tgfx2/handles.hpp"
#include "tgfx2/line_mesh_builder.hpp"
#include "tgfx2/tgfx2_api.h"

extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

    class RenderContext2;

    struct WorldTubeLineStyle {
        float width = 0.05f;
        int sides = 6;
    };

    struct WorldTubeLineParams {
        ShaderHandle vertex_shader{};
        ShaderHandle fragment_shader{};
        const tc_shader* shader_layout = nullptr;

        std::function<bool(RenderContext2&, const tc_shader*)> bind_resources;
    };

    class TGFX2_TYPE_API WorldTubeLineRenderer {
    private:
        BufferHandle body_corner_vbo_;
        BufferHandle cap_corner_vbo_;
        uint32_t body_corner_count_ = 0;
        uint32_t cap_corner_count_ = 0;
        int template_sides_ = 0;
        bool resources_failed_ = false;

    public:
        WorldTubeLineRenderer() = default;
        ~WorldTubeLineRenderer() = default;

        WorldTubeLineRenderer(const WorldTubeLineRenderer&) = delete;
        WorldTubeLineRenderer& operator=(const WorldTubeLineRenderer&) = delete;

        bool draw_polyline(RenderContext2& ctx,
                           std::span<const LinePoint3> points,
                           const WorldTubeLineStyle& style,
                           const WorldTubeLineParams& params);

        void release(RenderContext2& ctx);

    private:
        bool ensure_resources(RenderContext2& ctx, int sides);
    };

} // namespace tgfx
