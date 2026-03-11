#pragma once

#include <string>

#include <termin/geom/mat44.hpp>
#include <termin/tc_scene.hpp>
#include <tgfx/tgfx_shader_handle.hpp>

namespace termin {

class CameraComponent;
class GraphicsBackend;

struct RenderContext {
    Mat44f view;
    Mat44f projection;
    GraphicsBackend* graphics = nullptr;
    std::string phase = "main";
    Mat44f model = Mat44f::identity();
    TcShader current_tc_shader;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFF;
    TcSceneRef scene;
    CameraComponent* camera = nullptr;

    void set_model(const Mat44f& m) { model = m; }

    Mat44f mvp() const {
        return projection * view * model;
    }
};

} // namespace termin
