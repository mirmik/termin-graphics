// font_atlas.hpp - Dynamic TTF font atlas for tgfx2.
//
// Single grayscale (R8) atlas, default 2048x2048. Glyphs are rasterised
// on demand via stb_truetype and packed into shelves. The atlas is
// backed by one tgfx::TextureHandle; new glyphs set a dirty flag and
// the next ensure_texture() call re-uploads the full image.
//
// Data model follows the Python tgfx.font implementation it replaces:
//   - Each glyph cell is (ink_width, line_height) where
//     ink_width = x1 - x0 from stbtt_GetCodepointBitmapBox and
//     line_height = ascent + descent (no line_gap).
//   - The visible bitmap is placed so its top edge sits at y0 + ascent
//     inside the cell (baseline at y = ascent).
//   - Measurement uses ink_width, not typographic advance — same as
//     Python's PIL-based predecessor. Deliberate: lets tcgui / tcplot
//     layouts keep their pixel-accurate tick alignment.
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

    // Glyph info, populated lazily by ensure_glyph().
    struct GlyphInfo {
        // Atlas UVs.
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        // Ink size in pixels at the atlas rasterise_size. Callers scale
        // by (display_size / rasterise_size). Matches the rendered quad
        // dimensions — use these for layout of the glyph rectangle.
        float width_px = 0.0f;
        float height_px = 0.0f;
        // Horizontal advance in pixels at the atlas rasterise_size.
        // This is what the cursor moves between glyphs — differs from
        // width_px for anything with sidebearings, and is the *only*
        // sensible value for zero-ink glyphs like space.
        float advance_px = 0.0f;
    };

    // Load a TTF, default rasterise at 32 px, default atlas 2048x2048.
    // Throws std::runtime_error on file I/O or stbtt_InitFont failure.
    FontAtlas(const std::string& ttf_path,
              int rasterize_size_px = 32,
              int atlas_width = 2048,
              int atlas_height = 2048);
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Rasterise one codepoint into the CPU atlas. Returns true if the
    // glyph was newly added (caller's signal that a GPU re-upload is
    // needed). Returns false if already present OR if the font does
    // not define this codepoint OR if the atlas is full.
    bool ensure_glyph(uint32_t codepoint);

    // Rasterise every codepoint in a UTF-8 string. If `ctx` is non-null
    // and any new glyph was added, syncs the GPU texture immediately.
    void ensure_glyphs(std::string_view text_utf8, RenderContext2* ctx = nullptr);

    // Measure (width, height) of `text_utf8` at the display `font_size`
    // in pixels. Purely arithmetic over already-rasterised glyphs;
    // missing glyphs contribute 0. Call `ensure_glyphs(text)` first if
    // you want an accurate measure for arbitrary text.
    Size2f measure_text(std::string_view text_utf8, float font_size) const;

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

    // Look up a glyph's atlas entry. Returns nullptr if not rasterised.
    const GlyphInfo* get_glyph(uint32_t codepoint) const;

    // --- Metrics / introspection ---
    int rasterize_size() const { return rasterize_size_; }
    int ascent_px() const { return ascent_px_; }
    int descent_px() const { return descent_px_; }
    int line_height() const { return line_height_; }
    int atlas_width() const { return atlas_w_; }
    int atlas_height() const { return atlas_h_; }

    // Read-only view of the CPU atlas bitmap (R8, row-major,
    // stride = atlas_width). Primarily for test / debug / dump-to-PNG.
    const uint8_t* cpu_atlas_data() const { return atlas_.data(); }

private:
    // Shelf-packer: tries to place (cell_w, cell_h) into the atlas.
    // Returns {x, y} in atlas pixels on success, {-1, -1} on failure.
    struct PackedCell { int x; int y; };
    PackedCell pack_(int cell_w, int cell_h);

    // Actually upload the CPU atlas to the GPU handle (full re-upload).
    void sync_gpu_(RenderContext2* ctx);

    // TTF / stb_truetype state (opaque here to keep stb_truetype.h
    // out of the public include surface).
    struct Impl;
    Impl* impl_ = nullptr;

    // Rasterisation size: the single pixel size at which every glyph
    // is baked. Visual scaling at draw time is `display_size / this`.
    int rasterize_size_ = 32;
    int ascent_px_ = 0;
    int descent_px_ = 0;
    int line_height_ = 0;

    // CPU atlas: atlas_w_ * atlas_h_ bytes, R8.
    int atlas_w_ = 2048;
    int atlas_h_ = 2048;
    std::vector<uint8_t> atlas_;

    // Shelf packer state.
    int shelf_x_ = 0;
    int shelf_y_ = 0;
    int shelf_h_ = 0;

    // Glyph table keyed by codepoint.
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;

    // GPU state.
    RenderContext2* gpu_owner_ = nullptr;
    IRenderDevice* gpu_device_ = nullptr;  // cached for destroy on release
    TextureHandle gpu_texture_{};
    bool dirty_ = false;
};

}  // namespace tgfx
