// engine3d.hpp - Host-agnostic 3D plot engine for tcplot.
//
// Port of tcplot/tcplot/engine3d.py. Owns data, camera, GPU meshes
// and input state. Renders through a tgfx::RenderContext2 supplied
// by the host. Has no dependency on tcgui — host provides:
//   - viewport rect (pixel coords)
//   - RenderContext2 pointer at render time
//   - FontAtlas pointer at render time (for billboard tick labels)
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tcbase/input_enums.hpp>
#include <tgfx/tgfx_mesh_handle.hpp>

#include "tcplot/orbit_camera.hpp"
#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
class RenderContext2;
class IRenderDevice;
class FontAtlas;
class Text3DRenderer;
}  // namespace tgfx

namespace tcplot {

// Result of a 3D pick query. `x/y/z` are the data-space coordinates
// of the nearest series point; `screen_dist_px` is the screen-space
// distance from the cursor to that point (useful to show "no match"
// when it exceeds a threshold).
struct PickResult3D {
    double x = 0.0, y = 0.0, z = 0.0;
    double screen_dist_px = 0.0;
};

class TCPLOT_API PlotEngine3D {
public:
    // --- Scene ---
    PlotData data;
    OrbitCamera camera;

    // --- Style / mode flags (public; toggle_* methods also exist) ---
    bool show_grid = true;
    bool show_wireframe = true;
    float z_scale = 1.0f;
    bool marker_mode = false;

    PlotEngine3D();
    ~PlotEngine3D();

    PlotEngine3D(const PlotEngine3D&) = delete;
    PlotEngine3D& operator=(const PlotEngine3D&) = delete;

    // --- Viewport (host-supplied pixel rect) ---
    void set_viewport(float x, float y, float width, float height);

    // --- Public add-series API ---
    void plot(std::vector<double> x, std::vector<double> y, std::vector<double> z,
              std::optional<Color4> color = std::nullopt,
              double thickness = 1.5,
              std::string label = "");

    void scatter(std::vector<double> x, std::vector<double> y, std::vector<double> z,
                 std::optional<Color4> color = std::nullopt,
                 double size = 4.0,
                 std::string label = "");

    void surface(std::vector<double> X, std::vector<double> Y, std::vector<double> Z,
                 uint32_t rows, uint32_t cols,
                 std::optional<Color4> color = std::nullopt,
                 bool wireframe = false,
                 std::string label = "");

    void clear();

    void toggle_wireframe() { show_wireframe = !show_wireframe; }
    void toggle_marker_mode();

    // --- Rendering ---
    //
    // Host calls render() inside its own tgfx2 pass. The engine leaves
    // ctx in an unspecified state afterwards — host should re-assert
    // any state it relies on for subsequent draws (depth, blend, cull).
    void render(tgfx::RenderContext2* ctx, tgfx::FontAtlas* font);

    // Release all GPU-owned resources. Safe to call after GL context
    // teardown — no GL calls issued if owner device is not set.
    void release_gpu_resources();

    // --- Picking ---
    //
    // `mx, my` are in viewport-pixel coords (same space as set_viewport
    // origin). Returns nullopt if no point is within ~50 px on screen.
    std::optional<PickResult3D> pick(float mx, float my) const;

    // --- Input ---
    // Coordinates in viewport pixel space. Return true if the event
    // was consumed.
    bool on_mouse_down(float x, float y, tcbase::MouseButton button);
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, tcbase::MouseButton button);
    bool on_mouse_wheel(float x, float y, float dy);

private:
    // Build a (4x4) MVP from camera state + z_scale into `out16`.
    void compute_mvp_(float aspect, float out16[16]) const;

    // Build the GPU-side line / scatter / grid / surface / wireframe
    // meshes from PlotData. Called when `dirty_` is set.
    void rebuild_meshes_();
    void build_grid_mesh_(const double bounds_min[3], const double bounds_max[3]);
    void build_surface_mesh_(const SurfaceSeries& surf);
    void release_meshes_();

    // Ensure the 3D plot shader is compiled for the current device.
    void ensure_shader_(tgfx::IRenderDevice& device);

    // --- Viewport rect ---
    float vx_ = 0.0f, vy_ = 0.0f, vw_ = 0.0f, vh_ = 0.0f;

    // --- GPU resources (lazy) ---
    // Shader handles live with this device; dropped on device change.
    tgfx::IRenderDevice* shader_device_ = nullptr;
    // ShaderHandle is a plain id in tgfx2; use uint32_t storage to keep
    // the header free of tgfx2/handles.hpp dependency cycles.
    uint32_t shader_vs_id_ = 0;
    uint32_t shader_fs_id_ = 0;

    // Meshes. TcMesh owns its own GPU lifecycle via tc_mesh_upload_gpu.
    std::optional<termin::TcMesh> lines_mesh_;
    std::optional<termin::TcMesh> scatter_mesh_;
    std::optional<termin::TcMesh> grid_mesh_;
    std::vector<termin::TcMesh> surface_meshes_;
    std::vector<termin::TcMesh> wireframe_meshes_;

    // Text renderer for billboard tick/marker labels. Owned here; the
    // pimpl-style unique_ptr keeps the engine header free of Text3D
    // header includes.
    std::unique_ptr<tgfx::Text3DRenderer> text3d_;

    bool dirty_ = true;

    // --- Interaction ---
    bool dragging_ = false;
    tcbase::MouseButton drag_button_{tcbase::MouseButton::LEFT};
    float drag_start_x_ = 0.0f;
    float drag_start_y_ = 0.0f;

    // --- Marker ---
    bool has_marker_ = false;
    double marker_x_ = 0.0, marker_y_ = 0.0, marker_z_ = 0.0;
};

}  // namespace tcplot
