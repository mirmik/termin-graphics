// plot_view3d.hpp - Self-contained 3D plot view for embedding in
// external GUI frameworks (WPF, Qt, SDL, ...).
//
// Wraps a PlotEngine3D together with its own tgfx2 render context,
// font atlas and offscreen FBO. Exposes a minimal C-style API so
// callers (SWIG-bound C#, raw C) don't need to know about tgfx2.
//
// Usage:
//   PlotView3D view(ttf_path);
//   // ... data setup via plot/scatter/surface ...
//   // Each frame, with a GL context current:
//   view.render(w, h, gl_dst_fbo);
//
// render() pipeline:
//   1. ensure an RGBA8 + D32F offscreen FBO of size (w, h).
//   2. open a tgfx2 render pass on those attachments.
//   3. engine.render() draws meshes + billboard labels.
//   4. blit the offscreen color buffer to `gl_dst_fbo` via
//      glBlitFramebuffer (raw GL, backend-agnostic).
//
// The caller is responsible for:
//   - Initialising GL (e.g. via tc_opengl_init) before first render().
//   - Making the GL context current on the thread that calls render().
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

namespace tgfx2 {
class OpenGLRenderDevice;
class PipelineCache;
class RenderContext2;
class FontAtlas;
}

namespace tcplot {

class PlotEngine3D;

class TCPLOT_API PlotView3D {
public:
    // `ttf_path` - path to a TrueType font file used for axis tick
    // labels and picker readouts. Required (a font-less 3D plot would
    // have no axis labels).
    explicit PlotView3D(const std::string& ttf_path);
    ~PlotView3D();

    PlotView3D(const PlotView3D&) = delete;
    PlotView3D& operator=(const PlotView3D&) = delete;

    // --- Series API (flat-array inputs for easy SWIG wrapping) ---
    //
    // Color is passed as 4 floats; use NaN in any component to mean
    // "no color, use palette cycle" (so SWIG bindings can avoid an
    // optional-color wrapper).
    void plot(const double* x, const double* y, const double* z,
              size_t n,
              float cr, float cg, float cb, float ca,
              double thickness = 1.5,
              const char* label = "");

    void scatter(const double* x, const double* y, const double* z,
                 size_t n,
                 float cr, float cg, float cb, float ca,
                 double size = 4.0,
                 const char* label = "");

    void surface(const double* X, const double* Y, const double* Z,
                 uint32_t rows, uint32_t cols,
                 float cr, float cg, float cb, float ca,
                 bool wireframe = false,
                 const char* label = "");

    void clear();
    void toggle_wireframe();
    void toggle_marker_mode();
    void set_z_scale(float s);
    float get_z_scale() const;

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
    // driver. The FBO resolves down to the host's single-sample FBO
    // inside blit_to_external_fbo. Default 4.
    void set_msaa_samples(int samples);
    int  msaa_samples() const { return msaa_samples_; }

    // --- Render one frame ---
    //
    // `dst_gl_fbo` - target framebuffer object id (0 = default FB).
    // After return, both the offscreen FBO and the destination FBO
    // are unbound.
    void render(int width, int height, uint32_t dst_gl_fbo);

    // Release ALL GPU resources (offscreen FBO, meshes, shaders,
    // font atlas texture). Safe to call after the GL context has
    // been torn down — functions that would issue GL calls early-out
    // when state shows as already cleared.
    void release_gpu();

private:
    void ensure_offscreen_(int w, int h);
    void blit_to_dst_(int w, int h, uint32_t dst_gl_fbo);

    // Font path captured so we can re-create the atlas on release_gpu
    // → reuse cycle if the host re-enters render after a context drop.
    std::string ttf_path_;

    std::unique_ptr<tgfx2::OpenGLRenderDevice> device_;
    std::unique_ptr<tgfx2::PipelineCache> cache_;
    std::unique_ptr<tgfx2::RenderContext2> ctx_;
    std::unique_ptr<tgfx2::FontAtlas> font_;
    std::unique_ptr<PlotEngine3D> engine_;

    // Offscreen color + depth are plain tgfx2 textures; begin_pass
    // owns the FBO they compose into. When msaa_samples_ > 1 both
    // attachments are multisample, and glBlitFramebuffer inside
    // blit_to_external_fbo resolves down to the single-sample host FBO.
    tgfx2::TextureHandle offscreen_color_{};
    tgfx2::TextureHandle offscreen_depth_{};
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
    int msaa_samples_ = 4;
};

}  // namespace tcplot
