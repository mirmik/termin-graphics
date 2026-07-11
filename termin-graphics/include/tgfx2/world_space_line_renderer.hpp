#pragma once

#include <array>
#include <span>

#include "tgfx2/handles.hpp"
#include "tgfx2/line_mesh_builder.hpp"
#include "tgfx2/tgfx2_api.h"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace tgfx {

class RenderContext2;

struct WorldSpaceLineStyle {
    float width = 0.05f;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    LineCapStyle cap = LineCapStyle::Round;
    LineJoinStyle join = LineJoinStyle::Round;
    int round_segments = 12;
};

struct WorldSpaceLineParams {
    // Column-major view-projection matrix.
    std::array<float, 16> view_projection{};
    LinePoint3 camera_position{0.0f, 0.0f, 5.0f};
    bool lighting_enabled = false;
    ShaderHandle fragment_shader{};
};

class TGFX2_TYPE_API WorldSpaceLineRenderer {
private:
    BufferHandle corner_vbo_;
    BufferHandle cap_corner_vbo_;
    BufferHandle join_corner_vbo_;
    BufferHandle round_join_corner_vbo_;
    ShaderHandle vertex_shader_;
    ShaderHandle fragment_shader_;
    ShaderHandle cap_vertex_shader_;
    ShaderHandle cap_fragment_shader_;
    ShaderHandle join_vertex_shader_;
    ShaderHandle join_fragment_shader_;
    ShaderHandle round_join_vertex_shader_;
    ShaderHandle round_join_fragment_shader_;
    ShaderHandle lit_fragment_shader_;
    tc_shader_handle shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle cap_shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle join_shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle round_join_shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle lit_shader_handle_ = tc_shader_handle_invalid();
    uint32_t cap_corner_count_ = 0;
    uint32_t round_join_corner_count_ = 0;
    int cap_round_segments_ = 0;
    int round_join_segments_ = 0;

public:
    WorldSpaceLineRenderer() = default;
    ~WorldSpaceLineRenderer() = default;

    WorldSpaceLineRenderer(const WorldSpaceLineRenderer&) = delete;
    WorldSpaceLineRenderer& operator=(const WorldSpaceLineRenderer&) = delete;

    void draw_polyline(RenderContext2& ctx,
                       std::span<const LinePoint3> points,
                       const WorldSpaceLineStyle& style,
                       const WorldSpaceLineParams& params);

    void release(RenderContext2& ctx);

private:
    void ensure_resources(RenderContext2& ctx);
    void ensure_cap_template(RenderContext2& ctx, int round_segments);
    void ensure_round_join_template(RenderContext2& ctx, int round_segments);
};

} // namespace tgfx
