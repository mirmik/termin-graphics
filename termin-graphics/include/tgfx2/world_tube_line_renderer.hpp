#pragma once

#include <array>
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
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    LinePoint3 up_hint{0.0f, 1.0f, 0.0f};
    int sides = 6;
};

struct WorldTubeLineParams {
    // Column-major view-projection matrix.
    std::array<float, 16> view_projection{};
    bool lighting_enabled = false;
    ShaderHandle fragment_shader{};

    ShaderHandle body_vertex_shader{};
    ShaderHandle body_fragment_shader{};
    const tc_shader* body_shader_layout = nullptr;

    ShaderHandle cap_vertex_shader{};
    ShaderHandle cap_fragment_shader{};
    const tc_shader* cap_shader_layout = nullptr;

    std::function<void(RenderContext2&, const tc_shader*)> bind_resources;
};

class TGFX2_TYPE_API WorldTubeLineRenderer {
public:
    WorldTubeLineRenderer() = default;
    ~WorldTubeLineRenderer() = default;

    WorldTubeLineRenderer(const WorldTubeLineRenderer&) = delete;
    WorldTubeLineRenderer& operator=(const WorldTubeLineRenderer&) = delete;

    void draw_polyline(RenderContext2& ctx,
                       std::span<const LinePoint3> points,
                       const WorldTubeLineStyle& style,
                       const WorldTubeLineParams& params);

    void release(RenderContext2& ctx);

private:
    BufferHandle body_corner_vbo_;
    BufferHandle cap_corner_vbo_;
    ShaderHandle body_vertex_shader_;
    ShaderHandle body_fragment_shader_;
    ShaderHandle cap_vertex_shader_;
    ShaderHandle cap_fragment_shader_;
    ShaderHandle lit_fragment_shader_;
    tc_shader_handle body_shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle cap_shader_handle_ = tc_shader_handle_invalid();
    tc_shader_handle lit_shader_handle_ = tc_shader_handle_invalid();
    uint32_t body_corner_count_ = 0;
    uint32_t cap_corner_count_ = 0;
    int template_sides_ = 0;
    bool resources_failed_ = false;

    bool ensure_resources(RenderContext2& ctx, int sides);
};

} // namespace tgfx
