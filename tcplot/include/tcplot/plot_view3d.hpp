// plot_view3d.hpp - 3D plot view for embedding in
// external GUI frameworks (WPF, Qt, SDL, ...).
//
// Wraps a PlotEngine3D together with borrowed tgfx2 runtime objects
// and owned offscreen textures. Exposes a minimal C-style API so callers
// (SWIG-bound C#, raw C) can pass texture handles through platform
// presentation code without raw backend framebuffers.
//
// Usage:
//   GpuHost host(ttf_path, tgfx::BackendType::Vulkan);
//   PlotView3D view(host);
//   // ... data setup via plot/scatter/surface ...
//   // Each frame:
//   tgfx::TextureHandle color = view.render_to_texture(w, h);
//
// render() pipeline:
//   1. ensure RGBA8 + depth offscreen textures of size (w, h).
//   2. open a tgfx2 render pass on those attachments.
//   3. engine.render() draws meshes + billboard labels.
//   4. return the color TextureHandle for a platform presenter.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tgfx2/handles.hpp>

#include "tcplot/orbit_camera.hpp"
#include "tcplot/plot_data.hpp"
#include "tcplot/styles.hpp"
#include "tcplot/tcplot_api.h"

namespace tgfx {
class IRenderDevice;
class PipelineCache;
class RenderContext2;
class FontAtlas;
}

namespace tcplot {

class PlotEngine3D;
class GpuHost;

class TCPLOT_API PlotView3D {
public:
    PlotView3D(tgfx::IRenderDevice& device,
               tgfx::PipelineCache& cache,
               tgfx::RenderContext2& ctx,
               tgfx::FontAtlas& font);
    explicit PlotView3D(GpuHost& host);
    ~PlotView3D();

    PlotView3D(const PlotView3D&) = delete;
    PlotView3D& operator=(const PlotView3D&) = delete;

    // --- Series API (flat-array inputs for easy SWIG wrapping) ---
    //
    // Color is passed as 4 floats; use NaN in any component to mean
    // "no color, use palette cycle" (so SWIG bindings can avoid an
    // optional-color wrapper).
    void plot(SeriesData3DView series, LinePlotOptions options = {});

    void scatter(SeriesData3DView series, ScatterPlotOptions options = {});

    void surface(SurfaceDataView surface, SurfacePlotOptions options = {});
    void surface_colormap(SurfaceDataView surface, SurfacePlotOptions options);

    void clear();
    void set_title(const char* title);
    void set_x_label(const char* label);
    void set_y_label(const char* label);
    void set_z_label(const char* label);
    void set_axis_labels(const char* x_label,
                         const char* y_label,
                         const char* z_label);
    bool set_surface_colormap(int surface_idx, SurfaceColorMap colormap);
    bool set_surface_colormap_reversed(int surface_idx, bool reversed);
    bool set_surface_color(int surface_idx, float r, float g, float b, float a);
    bool set_surface_grid(int surface_idx, SurfaceGridOptions options);
    void toggle_wireframe();
    void toggle_marker_mode();
    void set_z_scale(float s);
    float get_z_scale() const;
    void set_axis_scale(float x, float y, float z);
    float get_x_scale() const;
    float get_y_scale() const;
    void set_surface_shading(bool enabled, float strength = 0.35f);
    void set_surface_light_dir(float x, float y, float z);

    // --- Camera access ---
    // Returned reference lives as long as this view. C# callers can
    // read/write the camera's public state fields directly.
    OrbitCamera& camera();

    // Recenter/fit the camera on current data bounds.
    void fit_camera();

    // --- Input forwarding. Coordinates in window pixel space where
    // (0, 0) is the top-left of the view rectangle. Button and action
    // use the tcbase::MouseButton enum; C# SWIG bindings re-export
    // that enum. ---
    bool on_mouse_down(float x, float y, int button);  // int = tcbase::MouseButton
    void on_mouse_move(float x, float y);
    void on_mouse_up(float x, float y, int button);
    bool on_mouse_wheel(float x, float y, float dy);

    // --- Picking. Returns false + zero-filled out-params when no
    // point is near the cursor. Caller supplies the viewport rect so
    // pick(x, y) coords match what it last drew into. ---
    bool pick(float mx, float my,
              double* out_x, double* out_y, double* out_z,
              double* out_screen_dist_px);

    // --- MSAA ---
    //
    // Multisample sample count for the offscreen color + depth
    // attachments. Legal values: 1 (no AA), 2, 4, 8 depending on GL
    // driver. Default 4.
    void set_msaa_samples(int samples);
    int  msaa_samples() const { return msaa_samples_; }

    // --- Render one frame ---
    tgfx::TextureHandle render_to_texture(int width, int height);
    uint32_t render_to_texture_id(int width, int height);
    tgfx::TextureHandle color_texture() const { return offscreen_color_; }
    tgfx::TextureHandle depth_texture() const { return offscreen_depth_; }

    // Release ALL GPU resources (offscreen FBO, meshes, shaders,
    // font atlas texture). Safe to call after the GL context has
    // been torn down — functions that would issue GL calls early-out
    // when state shows as already cleared.
    void release_gpu();

private:
    void ensure_offscreen_(int w, int h);

    // Font path captured so we can re-create the atlas on release_gpu
    // → reuse cycle if the host re-enters render after a context drop.
    tgfx::IRenderDevice*  device_ = nullptr;
    tgfx::PipelineCache*  cache_  = nullptr;
    tgfx::RenderContext2* ctx_    = nullptr;
    tgfx::FontAtlas*      font_   = nullptr;
    std::unique_ptr<PlotEngine3D> engine_;

    // Offscreen color + depth are plain tgfx2 textures; begin_pass owns
    // the backend render target they compose into.
    tgfx::TextureHandle offscreen_color_{};
    tgfx::TextureHandle offscreen_depth_{};
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
    int msaa_samples_ = 4;
};

}  // namespace tcplot
