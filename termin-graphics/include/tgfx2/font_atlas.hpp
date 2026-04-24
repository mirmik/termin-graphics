// font_atlas.hpp - Dynamic TTF font atlas for tgfx2.
//
// Single grayscale (R8) atlas, default 2048x2048. Glyphs are baked on
// demand per requested display size via stb_truetype and packed into
// shelves. The atlas is backed by one tgfx::TextureHandle; new glyphs
// set a dirty flag and the next ensure_texture() call re-uploads the
// full image.
//
// Sizing model (unlike the previous single-rasterise-size design):
//   - Every lookup takes `display_px` — the pixel height the caller
//     intends to render at. The atlas quantises this to an integer px
//     and either hits a cached bake or produces a fresh one.
//   - GlyphInfo metrics (width_px / height_px / advance_px) are in
//     DISPLAY pixels at the requested size, ready for the renderer to
//     use directly (no `* scale` multiplication needed).
//   - Each size bakes its own cell layout: cell height = ascent +
//     descent at that size, glyph bitmap placed at (0, ascent + y0).
//
// Why this shape survives a future switch to SDF / MSDF:
//   - Renderer always asks for metrics at `display_px` — the atlas is
//     responsible for producing them, however it wants to internally.
//   - Bitmap backend: per-size bake → native metrics per entry.
//   - SDF backend: single bake at a reference size → metrics computed
//     on the fly as `font_units * display_px / reference_size`.
//   - Same GlyphInfo shape, same call pattern; renderer unchanged.
//
// Measurement uses typographic advance (stbtt_GetGlyphHMetrics), not
// ink-bbox width — kept from the previous design. Layouts in tcgui /
// tcplot that expect space glyphs to occupy real width keep working.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class RenderContext2;
class IRenderDevice;

class TGFX2_API FontAtlas {
public:
    // Pixel (width, height) returned by measure_text.
    struct Size2f {
        float width = 0.0f;
        float height = 0.0f;
    };

    // Glyph info, populated lazily by ensure_glyph(cp, display_px).
    // All metrics are in DISPLAY pixels at the baked size — renderer
    // should use them directly without further scaling.
    struct GlyphInfo {
        // Atlas UVs covering the full stored region (includes the
        // horizontal-oversampling skirt column).
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        // Display-pixel size of the drawn quad.
        float width_px = 0.0f;
        float height_px = 0.0f;
        // Horizontal advance in display pixels — what the cursor moves
        // between glyphs. Differs from width_px for anything with
        // sidebearings, and is the *only* sensible value for zero-ink
        // glyphs like space.
        float advance_px = 0.0f;
    };

    // Minimum quantised size. Requests below this are clamped — stb
    // can't rasterise meaningfully smaller than this anyway, and it
    // bounds the per-size cache against pathological inputs.
    static constexpr int kMinPxSize = 4;

private:
    // Per-size font vmetrics, resolved lazily on first request.
    struct SizeMetrics {
        float scale = 0.0f;    // stbtt_ScaleForPixelHeight at this size
        int ascent_px = 0;
        int descent_px = 0;    // positive magnitude (stb returns negative)
        int line_height = 0;   // ascent + descent
    };

    // Shelf-packer result: {x, y} in atlas pixels on success, {-1, -1} on failure.
    struct PackedCell { int x; int y; };

    // TTF / stb_truetype state (opaque here to keep stb_truetype.h
    // out of the public include surface).
    struct Impl;
    Impl* impl_ = nullptr;

    // Size used for preload warm-up and as the "default" for Python
    // property accessors that don't take a size argument.
    int default_preload_size_ = 14;

    // CPU atlas: atlas_w_ * atlas_h_ bytes, R8.
    int atlas_w_ = 2048;
    int atlas_h_ = 2048;
    std::vector<uint8_t> atlas_;

    // Shelf packer state.
    int shelf_x_ = 0;
    int shelf_y_ = 0;
    int shelf_h_ = 0;

    // Glyph table keyed by (codepoint, px_size) — see make_key_.
    std::unordered_map<uint64_t, GlyphInfo> glyphs_;

    // Per-size metrics cache (mutable so const accessors can populate
    // on demand — the cache is a pure function of px_size + loaded TTF).
    mutable std::unordered_map<int, SizeMetrics> size_metrics_;

    // GPU state.
    RenderContext2* gpu_owner_ = nullptr;
    IRenderDevice* gpu_device_ = nullptr;  // cached for destroy on release
    TextureHandle gpu_texture_{};
    bool dirty_ = false;

public:
    // Load a TTF. `default_preload_size_px` controls which size gets
    // the full preload character warm-up (so the first frame of
    // typical-sized UI text doesn't stall). Other sizes populate
    // lazily on demand. 2048x2048 R8 atlas by default.
    //
    // Throws std::runtime_error on file I/O or stbtt_InitFont failure.
    FontAtlas(const std::string& ttf_path,
              int default_preload_size_px = 14,
              int atlas_width = 2048,
              int atlas_height = 2048);
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Rasterise one codepoint at the requested display size. Returns
    // true if the glyph was newly added (caller's signal that a GPU
    // re-upload is needed). Returns false if already present OR if
    // the font does not define this codepoint OR if the atlas is full.
    // `display_px` is quantised to `max(round(display_px), kMinPxSize)`.
    bool ensure_glyph(uint32_t codepoint, float display_px);

    // Rasterise every codepoint in a UTF-8 string at `display_px`. If
    // `ctx` is non-null and any new glyph was added, syncs the GPU
    // texture immediately.
    void ensure_glyphs(std::string_view text_utf8,
                       float display_px,
                       RenderContext2* ctx = nullptr);

    // Measure (width, height) of `text_utf8` at the display size.
    // Purely arithmetic over already-rasterised glyphs at the matching
    // size; missing glyphs contribute 0. Call `ensure_glyphs(text,
    // display_px)` first if you want an accurate measure for arbitrary
    // text.
    Size2f measure_text(std::string_view text_utf8, float display_px) const;

    // Create or refresh the GPU texture. Uploads on first call or when
    // new glyphs have been rasterised. Returns the (cached) handle.
    //
    // On subsequent calls with a different RenderContext2 (e.g. the
    // application re-created its GL context), the old handle is
    // discarded and a fresh one allocated on the new device.
    TextureHandle ensure_texture(RenderContext2* ctx);

    // Drop the GPU handle. Call this when the underlying GL context is
    // being destroyed (Python __del__ / C# IDisposable semantics). Safe
    // to call with no live ctx — no destroy is issued in that case.
    void release_gpu();

    // Look up a glyph's atlas entry at the requested display size.
    // Returns nullptr if not rasterised at this size.
    const GlyphInfo* get_glyph(uint32_t codepoint, float display_px) const;

    // --- Size-aware metrics ---
    // All return values are in display pixels at the requested size.
    // First call at a new size populates a small internal cache; the
    // bake routines use the same cache.
    int ascent_px(float display_px) const;
    int descent_px(float display_px) const;
    int line_height_px(float display_px) const;

    // --- Introspection ---
    // The size used for preload warm-up. Used by renderers / bindings
    // that want a sensible "default" for size-agnostic legacy calls.
    int default_preload_size() const { return default_preload_size_; }
    int atlas_width() const { return atlas_w_; }
    int atlas_height() const { return atlas_h_; }

    // Read-only view of the CPU atlas bitmap (R8, row-major,
    // stride = atlas_width). Primarily for test / debug / dump-to-PNG.
    const uint8_t* cpu_atlas_data() const { return atlas_.data(); }

private:
    // Quantise a request to the integer px size we actually bake at.
    static int quantise_size_(float display_px);

    // Compose / decompose the 64-bit glyph table key.
    static uint64_t make_key_(uint32_t codepoint, int px_size) {
        return (static_cast<uint64_t>(codepoint) << 32) |
               static_cast<uint32_t>(px_size);
    }

    const SizeMetrics& metrics_for_(int px_size) const;

    // Shelf-packer: tries to place (cell_w, cell_h) into the atlas.
    PackedCell pack_(int cell_w, int cell_h);

    // Actually upload the CPU atlas to the GPU handle (full re-upload).
    void sync_gpu_(RenderContext2* ctx);
};

}  // namespace tgfx
