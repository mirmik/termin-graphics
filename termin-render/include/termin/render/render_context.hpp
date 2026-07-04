#pragma once

#include <functional>
#include <string>

#include <termin/geom/mat44.hpp>
#include <termin/geom/vec3.hpp>
#include <termin/geom/vec4.hpp>
#include <termin/render/material_pipeline_shader_assembler.hpp>
#include <termin/render/render_camera.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

struct tc_material_phase;
struct tc_shader;
namespace tgfx { class RenderContext2; }

namespace termin {

struct RenderContext {
    Mat44f view;
    Mat44f projection;
    std::string phase = "main";
    MaterialPipelinePassContract pass_contract;
    Mat44f model = Mat44f::identity();
    TcShader current_tc_shader;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFF;
    Vec3 camera_position{0.0, 0.0, 0.0};
    bool has_override_color = false;
    Vec4 override_color{0.0, 0.0, 0.0, 0.0};
    int viewport_width = 0;
    int viewport_height = 0;
    TcSceneRef scene;
    RenderCamera* camera = nullptr;
    std::function<void(tgfx::RenderContext2&, const tc_shader*, tc_material_phase*)>
        prepare_tgfx2_material_resources;

    void set_model(const Mat44f& m) { model = m; }

    Mat44f mvp() const {
        return projection * view * model;
    }
};

} // namespace termin
